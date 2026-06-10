#include "ocpp.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cerrno>
#include <cctype>
#include <cstring>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static constexpr size_t MAX_RX_BUFFER = 4096;
static constexpr size_t MAX_WS_PAYLOAD = 2048;
static constexpr size_t MAX_WS_FRAMES_PER_LOOP = 1;

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

void OcppComponent::set_server_path(std::string path) {
  if (path.empty() || path[0] != '/')
    path.insert(path.begin(), '/');
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  this->server_path_ = std::move(path);
}

void OcppComponent::setup() {
  this->server_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (this->server_ == nullptr) {
    ESP_LOGE(TAG, "Could not create OCPP listen socket");
    this->mark_failed();
    return;
  }

  int enable = 1;
  this->server_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  sockaddr_storage addr{};
  socklen_t addr_len = socket::set_sockaddr_any(reinterpret_cast<sockaddr *>(&addr), sizeof(addr), this->server_port_);
  if (addr_len == 0 || this->server_->bind(reinterpret_cast<sockaddr *>(&addr), addr_len) != 0 ||
      this->server_->listen(1) != 0 || this->server_->setblocking(false) != 0) {
    ESP_LOGE(TAG, "Could not start OCPP listener on port %u", this->server_port_);
    this->mark_failed();
  }
}

void OcppComponent::loop() {
  if (this->server_ != nullptr && this->server_->ready())
    this->accept_client_();
  if (this->client_ != nullptr && this->client_->ready())
    this->read_client_();
}

void OcppComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "OCPP server:");
  ESP_LOGCONFIG(TAG, "  Listen: 0.0.0.0:%u%s", this->server_port_, this->server_path_.c_str());
}

float OcppComponent::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

void OcppComponent::accept_client_() {
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

void OcppComponent::close_client_() {
  if (this->client_ != nullptr)
    this->client_->close();
  this->client_.reset();
  this->rx_buffer_.clear();
  this->handshake_done_ = false;
  this->charge_point_id_.clear();
}

void OcppComponent::read_client_() {
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

bool OcppComponent::request_matches_path_(const std::string &uri) {
  if (uri == this->server_path_) {
    this->charge_point_id_ = "";
    return true;
  }
  std::string prefix = this->server_path_ + "/";
  if (uri.rfind(prefix, 0) != 0)
    return false;
  this->charge_point_id_ = uri.substr(prefix.size());
  return true;
}

void OcppComponent::handle_http_handshake_() {
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

  std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
  response += "Sec-WebSocket-Accept: " + this->websocket_accept_key_(key) + "\r\n";
  if (header_value(request, "Sec-WebSocket-Protocol").find("ocpp1.6") != std::string::npos)
    response += "Sec-WebSocket-Protocol: ocpp1.6\r\n";
  response += "\r\n";
  this->client_->write(response.data(), response.size());
  this->handshake_done_ = true;
  ESP_LOGI(TAG, "OCPP WebSocket accepted for charge point '%s'", this->charge_point_id_.c_str());
}

void OcppComponent::handle_ws_frames_() {
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
