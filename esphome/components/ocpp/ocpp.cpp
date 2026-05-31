#include "ocpp.h"

#ifdef USE_OCPP

#include "esphome/core/alloc_helpers.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static constexpr size_t MAX_RX_BUFFER = 4096;
static constexpr size_t MAX_WS_PAYLOAD = 2048;
static constexpr const char *CURRENT_TIME = "1970-01-01T00:00:00Z";
static constexpr const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

uint32_t rol(uint32_t value, uint8_t bits) { return (value << bits) | (value >> (32 - bits)); }

std::array<uint8_t, 20> sha1(const std::string &input) {
  uint64_t bit_len = static_cast<uint64_t>(input.size()) * 8;
  std::string data(input);
  data.push_back(static_cast<char>(0x80));
  while ((data.size() % 64) != 56)
    data.push_back(0);
  for (int i = 7; i >= 0; i--)
    data.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));

  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
    uint32_t w[80];
    for (size_t i = 0; i < 16; i++) {
      size_t j = chunk + i * 4;
      w[i] = (uint32_t(static_cast<uint8_t>(data[j])) << 24) |
             (uint32_t(static_cast<uint8_t>(data[j + 1])) << 16) |
             (uint32_t(static_cast<uint8_t>(data[j + 2])) << 8) | uint32_t(static_cast<uint8_t>(data[j + 3]));
    }
    for (size_t i = 16; i < 80; i++)
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (size_t i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t temp = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<uint8_t, 20> out{};
  uint32_t h[5] = {h0, h1, h2, h3, h4};
  for (size_t i = 0; i < 5; i++) {
    out[i * 4] = h[i] >> 24;
    out[i * 4 + 1] = h[i] >> 16;
    out[i * 4 + 2] = h[i] >> 8;
    out[i * 4 + 3] = h[i];
  }
  return out;
}

bool equals_ignore_case(const std::string &a, const char *b) {
  size_t len = std::strlen(b);
  if (a.size() != len)
    return false;
  for (size_t i = 0; i < len; i++) {
    auto ca = std::tolower(static_cast<unsigned char>(a[i]));
    auto cb = std::tolower(static_cast<unsigned char>(b[i]));
    if (ca != cb)
      return false;
  }
  return true;
}

std::string trim(const std::string &value) {
  size_t begin = value.find_first_not_of(" \t");
  if (begin == std::string::npos)
    return "";
  size_t end = value.find_last_not_of(" \t");
  return value.substr(begin, end - begin + 1);
}

std::string header_value(const std::string &request, const char *name) {
  size_t pos = request.find("\r\n");
  while (pos != std::string::npos) {
    size_t next = request.find("\r\n", pos + 2);
    if (next == std::string::npos)
      break;
    std::string line = request.substr(pos + 2, next - pos - 2);
    size_t colon = line.find(':');
    if (colon != std::string::npos && equals_ignore_case(line.substr(0, colon), name))
      return trim(line.substr(colon + 1));
    pos = next;
  }
  return "";
}

std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (char c : value) {
    if (c == '"' || c == '\\')
      out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

}  // namespace

void OcppServer::set_path(std::string path) {
  if (path.empty() || path[0] != '/')
    path.insert(path.begin(), '/');
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  this->path_ = std::move(path);
}

void OcppServer::add_charger(std::string charge_point_id) {
  if (this->find_charger_(charge_point_id) != nullptr)
    return;
  this->chargers_.push_back(ConfiguredCharger{std::move(charge_point_id), {}});
}

void OcppServer::add_connector(std::string charge_point_id, uint8_t connector_id, float max_current) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr) {
    this->add_charger(charge_point_id);
    charger = this->find_charger_(charge_point_id);
  }
  if (charger == nullptr)
    return;
  charger->connectors.push_back(ConfiguredConnector{connector_id, max_current});
}

void OcppServer::setup() {
  this->server_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (this->server_ == nullptr) {
    ESP_LOGE(TAG, "Could not create OCPP listen socket");
    this->mark_failed();
    return;
  }

  int enable = 1;
  this->server_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  sockaddr_storage addr{};
  socklen_t addr_len = socket::set_sockaddr_any(reinterpret_cast<sockaddr *>(&addr), sizeof(addr), this->port_);
  if (addr_len == 0 || this->server_->bind(reinterpret_cast<sockaddr *>(&addr), addr_len) != 0 ||
      this->server_->listen(1) != 0 || this->server_->setblocking(false) != 0) {
    ESP_LOGE(TAG, "Could not start OCPP listener on port %u", this->port_);
    this->mark_failed();
  }
}

void OcppServer::loop() {
  if (this->server_ != nullptr && this->server_->ready())
    this->accept_client_();
  if (this->client_ != nullptr && this->client_->ready())
    this->read_client_();
}

void OcppServer::dump_config() {
  ESP_LOGCONFIG(TAG, "OCPP server:");
  ESP_LOGCONFIG(TAG, "  Listen: 0.0.0.0:%u%s", this->port_, this->path_.c_str());
  ESP_LOGCONFIG(TAG, "  Configured chargers: %u", static_cast<unsigned>(this->chargers_.size()));
  for (const auto &charger : this->chargers_) {
    ESP_LOGCONFIG(TAG, "    Charge point: %s", charger.charge_point_id.c_str());
    for (const auto &connector : charger.connectors) {
      ESP_LOGCONFIG(TAG, "      Connector %u max_current=%.1f A", connector.id, connector.max_current);
    }
  }
  ESP_LOGCONFIG(TAG, "  Implemented messages: BootNotification, Heartbeat, Authorize, StatusNotification, StartTransaction");
}

float OcppServer::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

void OcppServer::disconnect() {
  if (this->client_ == nullptr) {
    ESP_LOGI(TAG, "No OCPP wallbox connection to disconnect");
    return;
  }
  ESP_LOGI(TAG, "Disconnecting OCPP wallbox '%s' on request", this->charge_point_id_.c_str());
  this->close_client_();
}

void OcppServer::remote_start(uint8_t connector_id, std::string id_tag, float current_limit) {
  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; no OCPP wallbox is connected");
    return;
  }
  if (current_limit <= 0) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction with non-positive current limit %.1f A", current_limit);
    return;
  }

  float limit = current_limit;
  const auto *connector = this->find_connector_(connector_id);
  if (!this->chargers_.empty() && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; connector %u is not configured for charge point '%s'",
             connector_id, this->charge_point_id_.c_str());
    return;
  }
  if (connector != nullptr && limit > connector->max_current) {
    ESP_LOGW(TAG, "Clamping RemoteStartTransaction current limit %.1f A to configured connector maximum %.1f A",
             limit, connector->max_current);
    limit = connector->max_current;
  }

  char limit_buf[16];
  std::snprintf(limit_buf, sizeof(limit_buf), "%.1f", limit);
  std::string unique_id = "remote-start-" + to_string(this->next_message_id_++);
  std::string payload = "[2,\"" + json_escape(unique_id) + "\",\"RemoteStartTransaction\",{";
  payload += "\"connectorId\":" + to_string(connector_id);
  payload += ",\"idTag\":\"" + json_escape(id_tag) + "\"";
  payload += ",\"chargingProfile\":{\"chargingProfileId\":1,\"stackLevel\":0,";
  payload += "\"chargingProfilePurpose\":\"TxProfile\",\"chargingProfileKind\":\"Absolute\",";
  payload += "\"chargingSchedule\":{\"chargingRateUnit\":\"A\",\"chargingSchedulePeriod\":[{";
  payload += "\"startPeriod\":0,\"limit\":";
  payload += limit_buf;
  payload += "}]}}}]";

  ESP_LOGI(TAG, "Sending RemoteStartTransaction: charge_point='%s' connectorId=%u idTag='%s' current_limit=%.1f A",
           this->charge_point_id_.c_str(), connector_id, id_tag.c_str(), limit);
  this->send_ws_text_(payload);
}

void OcppServer::accept_client_() {
  sockaddr_storage addr{};
  socklen_t addr_len = sizeof(addr);
  auto client = this->server_->accept_loop_monitored(reinterpret_cast<sockaddr *>(&addr), &addr_len);
  if (client == nullptr)
    return;
  if (this->client_ != nullptr) {
    ESP_LOGW(TAG, "Rejecting additional OCPP connection; minimal server supports one wallbox at a time");
    client->close();
    return;
  }
  client->setblocking(false);
  this->client_ = std::move(client);
  this->rx_buffer_.clear();
  this->charge_point_id_.clear();
  this->handshake_done_ = false;
  ESP_LOGI(TAG, "OCPP wallbox connected");
}

void OcppServer::close_client_() {
  if (this->client_ != nullptr)
    this->client_->close();
  this->client_.reset();
  this->rx_buffer_.clear();
  this->handshake_done_ = false;
  this->charge_point_id_.clear();
}

void OcppServer::read_client_() {
  uint8_t buffer[512];
  while (true) {
    ssize_t read = this->client_->read(buffer, sizeof(buffer));
    if (read > 0) {
      this->rx_buffer_.append(reinterpret_cast<const char *>(buffer), read);
      if (this->rx_buffer_.size() > MAX_RX_BUFFER) {
        ESP_LOGW(TAG, "Closing OCPP connection with oversized receive buffer");
        this->close_client_();
        return;
      }
      continue;
    }
    if (read == 0) {
      ESP_LOGI(TAG, "OCPP wallbox disconnected");
      this->close_client_();
      return;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGW(TAG, "OCPP socket read failed: errno=%d", errno);
      this->close_client_();
      return;
    }
    break;
  }
  if (!this->handshake_done_)
    this->handle_http_handshake_();
  if (this->handshake_done_)
    this->handle_ws_frames_();
}

bool OcppServer::request_matches_path_(const std::string &uri) {
  if (uri == this->path_) {
    this->charge_point_id_ = "";
    return true;
  }
  std::string prefix = this->path_ + "/";
  if (uri.rfind(prefix, 0) != 0)
    return false;
  this->charge_point_id_ = uri.substr(prefix.size());
  return true;
}

ConfiguredCharger *OcppServer::find_charger_(const std::string &charge_point_id) {
  for (auto &charger : this->chargers_) {
    if (charger.charge_point_id == charge_point_id)
      return &charger;
  }
  return nullptr;
}

const ConfiguredCharger *OcppServer::find_charger_(const std::string &charge_point_id) const {
  for (const auto &charger : this->chargers_) {
    if (charger.charge_point_id == charge_point_id)
      return &charger;
  }
  return nullptr;
}

const ConfiguredConnector *OcppServer::find_connector_(int connector_id) const {
  const auto *charger = this->find_charger_(this->charge_point_id_);
  if (charger == nullptr)
    return nullptr;
  for (const auto &connector : charger->connectors) {
    if (connector.id == connector_id)
      return &connector;
  }
  return nullptr;
}

void OcppServer::handle_http_handshake_() {
  size_t header_end = this->rx_buffer_.find("\r\n\r\n");
  if (header_end == std::string::npos)
    return;
  std::string request = this->rx_buffer_.substr(0, header_end + 4);
  this->rx_buffer_.erase(0, header_end + 4);

  size_t first_space = request.find(' ');
  size_t second_space = first_space == std::string::npos ? std::string::npos : request.find(' ', first_space + 1);
  std::string uri = second_space == std::string::npos ? "" : request.substr(first_space + 1, second_space - first_space - 1);
  size_t query = uri.find('?');
  if (query != std::string::npos)
    uri.erase(query);

  std::string key = header_value(request, "Sec-WebSocket-Key");
  if (request.rfind("GET ", 0) != 0 || key.empty() || !this->request_matches_path_(uri)) {
    static constexpr const char *BAD_REQUEST = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
    this->client_->write(BAD_REQUEST, std::strlen(BAD_REQUEST));
    this->close_client_();
    return;
  }
  const auto *configured_charger = this->find_charger_(this->charge_point_id_);
  if (!this->chargers_.empty() && configured_charger == nullptr) {
    ESP_LOGW(TAG, "Rejecting unknown OCPP charge point '%s'", this->charge_point_id_.c_str());
    static constexpr const char *FORBIDDEN = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n";
    this->client_->write(FORBIDDEN, std::strlen(FORBIDDEN));
    this->close_client_();
    return;
  }

  std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
  response += "Sec-WebSocket-Accept: " + this->websocket_accept_key_(key) + "\r\n";
  if (header_value(request, "Sec-WebSocket-Protocol").find("ocpp1.6") != std::string::npos)
    response += "Sec-WebSocket-Protocol: ocpp1.6\r\n";
  response += "\r\n";
  this->client_->write(response.data(), response.size());
  this->handshake_done_ = true;
  ESP_LOGI(TAG, "OCPP WebSocket accepted for charge point '%s'", this->charge_point_id_.c_str());
  if (configured_charger != nullptr) {
    ESP_LOGI(TAG, "Using configured charger '%s' with %u connector(s)", configured_charger->charge_point_id.c_str(),
             static_cast<unsigned>(configured_charger->connectors.size()));
  }
}

std::string OcppServer::websocket_accept_key_(const std::string &client_key) {
  auto digest = sha1(client_key + WS_GUID);
  return base64_encode(digest.data(), digest.size());
}

void OcppServer::handle_ws_frames_() {
  while (this->rx_buffer_.size() >= 2) {
    const uint8_t *data = reinterpret_cast<const uint8_t *>(this->rx_buffer_.data());
    uint8_t opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    uint64_t payload_len = data[1] & 0x7F;
    size_t pos = 2;
    if (payload_len == 126) {
      if (this->rx_buffer_.size() < 4)
        return;
      payload_len = (uint16_t(data[2]) << 8) | data[3];
      pos = 4;
    } else if (payload_len == 127) {
      ESP_LOGW(TAG, "Closing OCPP connection with unsupported large WebSocket frame");
      this->close_client_();
      return;
    }
    if (!masked || payload_len > MAX_WS_PAYLOAD) {
      ESP_LOGW(TAG, "Closing OCPP connection with invalid WebSocket frame");
      this->close_client_();
      return;
    }
    if (this->rx_buffer_.size() < pos + 4 + payload_len)
      return;

    uint8_t mask[4] = {data[pos], data[pos + 1], data[pos + 2], data[pos + 3]};
    pos += 4;
    std::string payload;
    payload.resize(payload_len);
    for (size_t i = 0; i < payload_len; i++)
      payload[i] = static_cast<char>(data[pos + i] ^ mask[i % 4]);
    this->rx_buffer_.erase(0, pos + payload_len);

    if (opcode == 0x8) {
      this->close_client_();
      return;
    }
    if (opcode == 0x9) {
      std::string pong;
      pong.push_back(static_cast<char>(0x8A));
      pong.push_back(static_cast<char>(payload.size()));
      pong += payload;
      this->client_->write(pong.data(), pong.size());
      continue;
    }
    if (opcode == 0x1)
      this->handle_ws_text_(payload);
  }
}

void OcppServer::handle_ws_text_(const std::string &message) {
  auto doc = json::parse_json(message);
  if (doc.isNull() || doc.overflowed() || !doc.is<JsonArray>()) {
    ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
    return;
  }
  JsonArray root = doc.as<JsonArray>();
  int message_type = root[0] | 0;
  if (message_type == 3) {
    if (root.size() < 3) {
      ESP_LOGW(TAG, "Ignoring invalid OCPP CALLRESULT message: %s", message.c_str());
      return;
    }
    this->handle_call_result_(root[1] | "", root[2].as<JsonObject>());
    return;
  }
  if (message_type == 4) {
    if (root.size() < 5) {
      ESP_LOGW(TAG, "Ignoring invalid OCPP CALLERROR message: %s", message.c_str());
      return;
    }
    this->handle_call_error_(root[1] | "", root[2] | "", root[3] | "");
    return;
  }
  if (root.size() < 4 || message_type != 2) {
    ESP_LOGW(TAG, "Ignoring unsupported OCPP message: %s", message.c_str());
    return;
  }

  std::string unique_id = root[1] | "";
  std::string action = root[2] | "";
  ESP_LOGD(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", this->charge_point_id_.c_str(),
           action.c_str(), unique_id.c_str());
  if (action == "BootNotification") {
    this->handle_boot_notification_(unique_id, root[3].as<JsonObject>());
  } else if (action == "Heartbeat") {
    this->handle_heartbeat_(unique_id);
  } else if (action == "Authorize") {
    this->handle_authorize_(unique_id, root[3].as<JsonObject>());
  } else if (action == "StatusNotification") {
    this->handle_status_notification_(unique_id, root[3].as<JsonObject>());
  } else if (action == "StartTransaction") {
    this->handle_start_transaction_(unique_id, root[3].as<JsonObject>());
  } else {
    ESP_LOGW(TAG, "Unsupported OCPP action '%s' from charge point '%s'", action.c_str(), this->charge_point_id_.c_str());
    this->send_ocpp_error_(unique_id, "NotImplemented", "This OCPP action is not implemented");
  }
}

void OcppServer::handle_boot_notification_(const std::string &unique_id, JsonObject payload) {
  const char *vendor = payload["chargePointVendor"] | "";
  const char *model = payload["chargePointModel"] | "";
  const char *serial = payload["chargePointSerialNumber"] | "";
  const char *firmware = payload["firmwareVersion"] | "";

  ESP_LOGI(TAG, "BootNotification accepted: charge_point='%s' vendor='%s' model='%s' serial='%s' firmware='%s'",
           this->charge_point_id_.c_str(), vendor, model, serial, firmware);

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME +
                         "\",\"interval\":300,\"status\":\"Accepted\"}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_heartbeat_(const std::string &unique_id) {
  ESP_LOGD(TAG, "Heartbeat from charge point '%s'", this->charge_point_id_.c_str());
  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME + "\"}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_authorize_(const std::string &unique_id, JsonObject payload) {
  const char *id_tag = payload["idTag"] | "";
  ESP_LOGI(TAG, "Authorize accepted: charge_point='%s' idTag='%s'", this->charge_point_id_.c_str(), id_tag);

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"idTagInfo\":{\"status\":\"Accepted\"}}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_status_notification_(const std::string &unique_id, JsonObject payload) {
  int connector_id = payload["connectorId"] | -1;
  const char *status = payload["status"] | "";
  const char *error_code = payload["errorCode"] | "";
  const char *timestamp = payload["timestamp"] | "";
  const char *info = payload["info"] | "";
  const char *vendor_id = payload["vendorId"] | "";
  const char *vendor_error_code = payload["vendorErrorCode"] | "";

  ESP_LOGI(TAG,
           "StatusNotification: charge_point='%s' connectorId=%d status='%s' errorCode='%s' timestamp='%s' "
           "info='%s' vendorId='%s' vendorErrorCode='%s'",
           this->charge_point_id_.c_str(), connector_id, status, error_code, timestamp, info, vendor_id,
           vendor_error_code);
  if (!this->chargers_.empty() && connector_id > 0 && this->find_connector_(connector_id) == nullptr) {
    ESP_LOGW(TAG, "StatusNotification referenced unconfigured connector %d for charge point '%s'", connector_id,
             this->charge_point_id_.c_str());
  }

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_start_transaction_(const std::string &unique_id, JsonObject payload) {
  int connector_id = payload["connectorId"] | -1;
  const char *id_tag = payload["idTag"] | "";
  int meter_start = payload["meterStart"] | 0;
  const char *timestamp = payload["timestamp"] | "";
  uint32_t transaction_id = this->next_transaction_id_++;
  const auto *connector = this->find_connector_(connector_id);
  if (!this->chargers_.empty() && connector == nullptr) {
    ESP_LOGW(TAG, "StartTransaction referenced unconfigured connector %d for charge point '%s'", connector_id,
             this->charge_point_id_.c_str());
  }

  if (payload["reservationId"].is<int>()) {
    ESP_LOGI(TAG,
             "StartTransaction accepted: charge_point='%s' connectorId=%d idTag='%s' meterStart=%d timestamp='%s' "
             "reservationId=%d transactionId=%u",
             this->charge_point_id_.c_str(), connector_id, id_tag, meter_start, timestamp,
             payload["reservationId"].as<int>(), transaction_id);
  } else {
    ESP_LOGI(TAG,
             "StartTransaction accepted: charge_point='%s' connectorId=%d idTag='%s' meterStart=%d timestamp='%s' "
             "transactionId=%u",
             this->charge_point_id_.c_str(), connector_id, id_tag, meter_start, timestamp, transaction_id);
  }

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"transactionId\":" + to_string(transaction_id) +
                         ",\"idTagInfo\":{\"status\":\"Accepted\"}}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_call_result_(const std::string &unique_id, JsonObject payload) {
  const char *status = payload["status"] | "";
  if (status[0] != '\0') {
    ESP_LOGI(TAG, "OCPP CALLRESULT: charge_point='%s' uniqueId='%s' status='%s'", this->charge_point_id_.c_str(),
             unique_id.c_str(), status);
  } else {
    ESP_LOGI(TAG, "OCPP CALLRESULT: charge_point='%s' uniqueId='%s'", this->charge_point_id_.c_str(),
             unique_id.c_str());
  }
}

void OcppServer::handle_call_error_(const std::string &unique_id, const std::string &error_code,
                                    const std::string &description) {
  ESP_LOGW(TAG, "OCPP CALLERROR: charge_point='%s' uniqueId='%s' errorCode='%s' description='%s'",
           this->charge_point_id_.c_str(), unique_id.c_str(), error_code.c_str(), description.c_str());
}

void OcppServer::send_ws_text_(const std::string &message) {
  if (this->client_ == nullptr)
    return;
  std::string frame;
  frame.push_back(static_cast<char>(0x81));
  if (message.size() < 126) {
    frame.push_back(static_cast<char>(message.size()));
  } else {
    frame.push_back(126);
    frame.push_back(static_cast<char>((message.size() >> 8) & 0xFF));
    frame.push_back(static_cast<char>(message.size() & 0xFF));
  }
  frame += message;
  this->client_->write(frame.data(), frame.size());
}

void OcppServer::send_ocpp_error_(const std::string &unique_id, const char *code, const char *description) {
  std::string response = "[4,\"" + json_escape(unique_id) + "\",\"" + code + "\",\"" + description + "\",{}]";
  this->send_ws_text_(response);
}

}  // namespace esphome::ocpp

#endif  // USE_OCPP
