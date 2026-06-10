#include "server.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp.server";
static constexpr size_t MAX_RX_BUFFER = 4096;
static constexpr size_t MAX_WS_PAYLOAD = 2048;
static constexpr size_t MAX_WS_FRAMES_PER_LOOP = 1;
static constexpr const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

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

}  // namespace

void OcppServer::set_path(std::string path) {
  if (path.empty() || path[0] != '/')
    path.insert(path.begin(), '/');
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  this->server_path_ = std::move(path);
}

bool OcppServer::setup() {
  this->server_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (this->server_ == nullptr) {
    ESP_LOGE(TAG, "Could not create listen socket");
    return false;
  }

  int enable = 1;
  this->server_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  sockaddr_storage addr{};
  socklen_t addr_len = socket::set_sockaddr_any(reinterpret_cast<sockaddr *>(&addr), sizeof(addr), this->server_port_);
  if (addr_len == 0 || this->server_->bind(reinterpret_cast<sockaddr *>(&addr), addr_len) != 0 ||
      this->server_->listen(1) != 0 || this->server_->setblocking(false) != 0) {
    ESP_LOGE(TAG, "Could not start listener on port %u", this->server_port_);
    return false;
  }
  return true;
}

void OcppServer::loop() {
  if (this->server_ != nullptr && this->server_->ready())
    this->accept_client_();
  if (this->client_ != nullptr && this->client_->ready())
    this->read_client_();
}

void OcppServer::dump_config() const {
  ESP_LOGCONFIG(TAG, "  Listen: 0.0.0.0:%u%s", this->server_port_, this->server_path_.c_str());
}

void OcppServer::accept_client_() {
  sockaddr_storage addr{};
  socklen_t addr_len = sizeof(addr);
  auto client = this->server_->accept_loop_monitored(reinterpret_cast<sockaddr *>(&addr), &addr_len);
  if (client == nullptr)
    return;
  if (this->client_ != nullptr) {
    ESP_LOGW(TAG, "Rejecting additional WebSocket connection; only one client is supported");
    client->close();
    return;
  }
  client->setblocking(false);
  this->client_ = std::move(client);
  this->rx_buffer_.clear();
  this->connection_id_.clear();
  this->handshake_done_ = false;
  ESP_LOGI(TAG, "WebSocket client connected");
}

void OcppServer::close_client_() {
  bool notify = this->handshake_done_;
  if (this->client_ != nullptr)
    this->client_->close();
  this->client_.reset();
  this->rx_buffer_.clear();
  this->handshake_done_ = false;
  this->connection_id_.clear();
  if (notify && this->listener_ != nullptr)
    this->listener_->on_websocket_disconnected();
}

void OcppServer::read_client_() {
  uint8_t buffer[512];
  while (true) {
    ssize_t read = this->client_->read(buffer, sizeof(buffer));
    if (read > 0) {
      this->rx_buffer_.append(reinterpret_cast<const char *>(buffer), read);
      if (this->rx_buffer_.size() > MAX_RX_BUFFER) {
        ESP_LOGW(TAG, "Closing WebSocket connection with oversized receive buffer");
        this->close_client_();
        return;
      }
      continue;
    }
    if (read == 0) {
      ESP_LOGI(TAG, "WebSocket client disconnected");
      this->close_client_();
      return;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGW(TAG, "WebSocket read failed: errno=%d", errno);
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
  if (uri == this->server_path_) {
    this->connection_id_ = "";
    return true;
  }
  std::string prefix = this->server_path_ + "/";
  if (uri.rfind(prefix, 0) != 0)
    return false;
  this->connection_id_ = uri.substr(prefix.size());
  return true;
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

  std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
  response += "Sec-WebSocket-Accept: " + this->websocket_accept_key_(key) + "\r\n";
  if (!this->subprotocol_.empty() && header_value(request, "Sec-WebSocket-Protocol").find(this->subprotocol_) != std::string::npos)
    response += "Sec-WebSocket-Protocol: " + this->subprotocol_ + "\r\n";
  response += "\r\n";
  this->client_->write(response.data(), response.size());
  this->handshake_done_ = true;
  ESP_LOGI(TAG, "WebSocket accepted for connection '%s'", this->connection_id_.c_str());
  if (this->listener_ != nullptr)
    this->listener_->on_websocket_connected(this->connection_id_);
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
      ESP_LOGW(TAG, "Closing WebSocket connection with unsupported large frame");
      this->close_client_();
      return;
    }
    if (!masked || payload_len > MAX_WS_PAYLOAD) {
      ESP_LOGW(TAG, "Closing WebSocket connection with invalid frame");
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
      this->write_frame_(0xA, payload);
      continue;
    }
    if (opcode == 0x1 && this->listener_ != nullptr)
      this->listener_->on_websocket_text(payload);

    if (++frames_handled >= MAX_WS_FRAMES_PER_LOOP)
      return;
  }
}

std::string OcppServer::websocket_accept_key_(const std::string &client_key) {
  auto digest = sha1(client_key + WS_GUID);
  return base64_encode(digest.data(), digest.size());
}

void OcppServer::send_text(const std::string &message) { this->write_frame_(0x1, message); }

void OcppServer::write_frame_(uint8_t opcode, const std::string &payload) {
  if (this->client_ == nullptr)
    return;
  std::string frame;
  frame.push_back(static_cast<char>(0x80 | opcode));
  if (payload.size() < 126) {
    frame.push_back(static_cast<char>(payload.size()));
  } else {
    frame.push_back(126);
    frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
    frame.push_back(static_cast<char>(payload.size() & 0xFF));
  }
  frame += payload;
  this->client_->write(frame.data(), frame.size());
}

}  // namespace esphome::ocpp