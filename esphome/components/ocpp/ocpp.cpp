#include "ocpp.h"

#ifdef USE_OCPP

#include "esphome/core/alloc_helpers.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static constexpr size_t MAX_RX_BUFFER = 4096;
static constexpr size_t MAX_WS_PAYLOAD = 2048;
static constexpr size_t MAX_WS_FRAMES_PER_LOOP = 1;
static constexpr const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::array<bool, 3> charger_site_load_phases(const ConfiguredCharger &charger, const ConfiguredConnector *connector) {
  const auto charger_load_phases = charger_effective_load_phases(charger, connector);
  std::array<bool, 3> site_load_phases{};
  for (uint8_t i = 0; i < charger_load_phases.size(); i++) {
    if (!charger_load_phases[i])
      continue;
    const uint8_t site_phase = charger.phase_mapping[i] < site_load_phases.size() ? charger.phase_mapping[i] : 0;
    site_load_phases[site_phase] = true;
  }
  return site_load_phases;
}

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


}  // namespace

void OcppServer::set_path(std::string path) {
  if (path.empty() || path[0] != '/')
    path.insert(path.begin(), '/');
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  this->path_ = std::move(path);
}

void OcppServer::set_site(uint8_t phases, float voltage) {
  configure_site(&this->site_, phases, voltage);
}

void OcppServer::add_charger(std::string charge_point_id, float max_current, uint8_t phases) {
  if (this->has_charger_ && charger_has_charge_point_id(this->charger_, charge_point_id)) {
    this->charger_.max_current = max_current;
    this->charger_.phases = phases == 3 ? 3 : 1;
    return;
  }
  configure_charger(&this->charger_, std::move(charge_point_id), max_current, phases);
  this->has_charger_ = true;
}

void OcppServer::set_charger_phase_mapping(std::string charge_point_id, uint8_t charger_phase, uint8_t site_phase) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || charger_phase >= 3 || site_phase >= 3)
    return;
  charger->phase_mapping[charger_phase] = site_phase;
}

void OcppServer::add_connector(std::string charge_point_id, uint8_t connector_id, float max_current) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr) {
    this->add_charger(charge_point_id, max_current);
    charger = this->find_charger_(charge_point_id);
  }
  if (charger == nullptr)
    return;
  charger->connector = ConfiguredConnector{connector_id,
                                           effective_connector_max_current(charger->max_current, max_current)};
  this->update_connector_allocation_(&charger->connector);
  charger->has_connector = true;
}

void OcppServer::set_connector_current_sensor(std::string charge_point_id, uint8_t connector_id,
                                              sensor::Sensor *current_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.current_sensor = current_sensor;
}

void OcppServer::set_connector_state_sensor(std::string charge_point_id, uint8_t connector_id,
                                            text_sensor::TextSensor *state_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.state_sensor = state_sensor;
}

void OcppServer::set_connector_current_limit_number(std::string charge_point_id, uint8_t connector_id,
                                                    OcppCurrentLimitNumber *current_limit_number,
                                                    float initial_limit) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.current_limit_number = current_limit_number;
  charger->connector.preferred_current_limit = initial_limit;
  charger->connector.has_preferred_current_limit = true;
  this->update_connector_allocation_(&charger->connector);
  current_limit_number->set_parent(this, connector_id);
}

void OcppServer::apply_connector_current_limit_restore(uint8_t connector_id, float current_limit) {
  auto *connector = this->find_connector_(connector_id);
  if (connector == nullptr)
    return;
  const float limit = std::min(current_limit, connector->max_current);
  if (limit <= 0.0f)
    return;
  connector->preferred_current_limit = limit;
  connector->has_preferred_current_limit = true;
  connector->charging_profile_applied = false;
  this->update_connector_allocation_(connector);
}

void OcppServer::set_connector_enabled_switch(std::string charge_point_id, uint8_t connector_id,
                                              OcppConnectorEnabledSwitch *enabled_switch) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.enabled_switch = enabled_switch;
  enabled_switch->set_parent(this, connector_id);
}

void OcppServer::set_connector_restart_button(std::string charge_point_id, uint8_t connector_id,
                                              OcppConnectorButton *restart_button) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.restart_button = restart_button;
  restart_button->set_parent(this, connector_id);
}

bool OcppServer::has_latest_current_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr && connector->has_latest_current_import;
}

bool OcppServer::has_session_current_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr && connector->has_session_current_import;
}

bool OcppServer::has_latest_power_active_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr && connector->has_latest_power_active_import;
}

float OcppServer::get_latest_current_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr ? connector->latest_current_import : 0.0f;
}

float OcppServer::get_latest_power_active_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr ? connector->latest_power_active_import : 0.0f;
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

  if (this->has_charger_ && this->charger_.has_connector && this->charger_.connector.current_limit_number != nullptr &&
      this->charger_.connector.has_preferred_current_limit) {
    this->charger_.connector.current_limit_number->publish_state(this->charger_.connector.preferred_current_limit);
  }
  if (this->has_charger_ && this->charger_.has_connector) {
    auto *connector = &this->charger_.connector;
    this->update_connector_allocation_(connector);
    this->publish_connector_state_if_configured_(connector);
    if (connector->enabled_switch != nullptr)
      connector->enabled_switch->publish_state(connector->enabled);
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
  ESP_LOGCONFIG(TAG, "  Allocation: strategy=equal min_current=%.1f A", this->allocation_min_current_);
  ESP_LOGCONFIG(TAG, "  Site: phases=%u voltage=%.1f V", this->site_.limits.phases, this->site_.limits.voltage);
  ESP_LOGCONFIG(TAG, "  Configured charger: %s", this->has_charger_ ? this->charger_.charge_point_id.c_str() : "none");
  if (this->has_charger_)
    ESP_LOGCONFIG(TAG, "    Charger max_current=%.1f A per phase", this->charger_.max_current);
  if (this->has_charger_ && this->charger_.has_connector) {
    ESP_LOGCONFIG(TAG, "    Connector %u max_current=%.1f A", this->charger_.connector.id,
                  this->charger_.connector.max_current);
  }
  ESP_LOGCONFIG(TAG, "  Implemented messages: BootNotification");
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

void OcppServer::remote_start(uint8_t connector_id) {
  ESP_LOGW(TAG, "Remote start is not supported; only BootNotification is implemented (connectorId=%u)", connector_id);
}

void OcppServer::remote_stop() {
  ESP_LOGW(TAG, "Remote stop is not supported; only BootNotification is implemented");
}

void OcppServer::remote_stop_connector(uint8_t connector_id) {
  ESP_LOGW(TAG, "Remote stop is not supported; only BootNotification is implemented (connectorId=%u)",
           connector_id);
}

void OcppServer::remote_stop(uint32_t transaction_id) {
  ESP_LOGW(TAG, "Remote stop is not supported; only BootNotification is implemented (transactionId=%u)",
           transaction_id);
}

void OcppServer::set_current_limit(uint8_t connector_id, float current_limit) {
  if (current_limit <= 0) {
    ESP_LOGW(TAG, "Ignoring non-positive current limit %.1f A", current_limit);
    return;
  }

  float limit = current_limit;
  auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot store current limit; connector %u is not configured for charge point '%s'", connector_id,
             this->charge_point_id_.c_str());
    return;
  }
  if (connector != nullptr && limit > connector->max_current) {
    ESP_LOGW(TAG, "Clamping stored current limit %.1f A to configured connector maximum %.1f A", limit,
             connector->max_current);
    limit = connector->max_current;
  }

  if (connector != nullptr) {
    connector->preferred_current_limit = limit;
    connector->has_preferred_current_limit = true;
    connector->charging_profile_applied = false;
    const bool allocation_updated = this->update_connector_allocation_(connector);
    if (connector->current_limit_number != nullptr)
      connector->current_limit_number->publish_state(limit);
    if (!allocation_updated)
      return;
  }
  ESP_LOGI(TAG, "Stored current limit %.1f A for connector %u; current-limit OCPP messages are not supported", limit,
           connector_id);
}

void OcppServer::set_connector_enabled(uint8_t connector_id, bool enabled) {
  auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot %s connector; connector %u is not configured for charge point '%s'",
             enabled ? "enable" : "disable", connector_id, this->charge_point_id_.c_str());
    return;
  }
  if (connector == nullptr)
    return;

  connector->enabled = enabled;
  connector->charging_profile_applied = false;
  const bool allocation_updated = this->update_connector_allocation_(connector);
  if (connector->enabled_switch != nullptr)
    connector->enabled_switch->publish_state(enabled);

  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGI(TAG, "Stored connector %u as %s; no OCPP wallbox is connected", connector_id,
             enabled ? "enabled" : "disabled");
    return;
  }
  ESP_LOGI(TAG, "Stored connector %u as %s; connector control OCPP messages are not supported", connector_id,
           enabled ? "enabled" : "disabled");
  (void) allocation_updated;
}

bool OcppServer::is_connector_enabled(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector == nullptr || connector->enabled;
}

void OcppServer::restart_connector_session(uint8_t connector_id) {
  ESP_LOGW(TAG, "Connector session restart is not supported; only BootNotification is implemented (connectorId=%u)",
           connector_id);
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
  if (this->has_charger_ && charger_has_charge_point_id(this->charger_, charge_point_id))
    return &this->charger_;
  return nullptr;
}

const ConfiguredCharger *OcppServer::find_charger_(const std::string &charge_point_id) const {
  if (this->has_charger_ && charger_has_charge_point_id(this->charger_, charge_point_id))
    return &this->charger_;
  return nullptr;
}

ConfiguredConnector *OcppServer::find_connector_(int connector_id) {
  auto *charger = this->find_charger_(this->charge_point_id_);
  if (charger == nullptr && this->has_charger_ && this->charge_point_id_.empty())
    charger = &this->charger_;
  return find_configured_connector(charger, connector_id);
}

const ConfiguredConnector *OcppServer::find_connector_(int connector_id) const {
  const auto *charger = this->find_charger_(this->charge_point_id_);
  if (charger == nullptr && this->has_charger_ && this->charge_point_id_.empty())
    charger = &this->charger_;
  return find_configured_connector(charger, connector_id);
}

bool OcppServer::update_connector_allocation_(ConfiguredConnector *connector, bool include_connector_as_active) {
  if (connector == nullptr)
    return false;
  if (this->should_defer_connector_allocation_(connector, include_connector_as_active))
    return false;

  const float available_current = connector->max_current;
  update_connector_allocation(connector, available_current, this->allocation_min_current_);
  return true;
}

bool OcppServer::should_defer_connector_allocation_(ConfiguredConnector *connector, bool include_connector_as_active) {
  (void) connector;
  (void) include_connector_as_active;
  return false;
}

void OcppServer::reset_session_current_(ConfiguredConnector *connector) {
  reset_connector_session_current(connector);
}

void OcppServer::publish_current_if_configured_(ConfiguredConnector *connector) {
  if (connector != nullptr && connector->current_sensor != nullptr)
    connector->current_sensor->publish_state(connector->latest_current_import);
}

void OcppServer::publish_connector_state_if_configured_(ConfiguredConnector *connector) {
  if (connector != nullptr && connector->state_sensor != nullptr)
    connector->state_sensor->publish_state(connector->state);
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
  if (this->has_charger_ && configured_charger == nullptr) {
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
    ESP_LOGI(TAG, "Using configured charger '%s'", configured_charger->charge_point_id.c_str());
  }
}

std::string OcppServer::websocket_accept_key_(const std::string &client_key) {
  auto digest = sha1(client_key + WS_GUID);
  return base64_encode(digest.data(), digest.size());
}

void OcppServer::handle_ws_frames_() {
  size_t frames_handled = 0;
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

    if (++frames_handled >= MAX_WS_FRAMES_PER_LOOP)
      return;
  }
}

}  // namespace esphome::ocpp

#endif  // USE_OCPP
