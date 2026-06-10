#include "ocpp.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <array>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
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


void OcppComponent::handle_ws_text_(const std::string &message) {
  auto doc = json::parse_json(message);
  if (doc.isNull() || doc.overflowed() || !doc.is<JsonArray>()) {
    ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
    return;
  }
  JsonArray root = doc.as<JsonArray>();
  int message_type = root[0] | 0;
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
  } else {
    ESP_LOGW(TAG, "Unsupported OCPP action '%s' from charge point '%s'", action.c_str(), this->charge_point_id_.c_str());
    this->send_ocpp_error_(unique_id, "NotImplemented", "This OCPP action is not implemented");
  }
}

void OcppComponent::handle_boot_notification_(const std::string &unique_id, JsonObject payload) {
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

std::string OcppComponent::websocket_accept_key_(const std::string &client_key) {
  auto digest = sha1(client_key + WS_GUID);
  return base64_encode(digest.data(), digest.size());
}

void OcppComponent::send_ws_text_(const std::string &message) {
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

void OcppComponent::send_ocpp_error_(const std::string &unique_id, const char *code, const char *description) {
  std::string response = "[4,\"" + json_escape(unique_id) + "\",\"" + code + "\",\"" + description + "\",{}]";
  this->send_ws_text_(response);
}

}  // namespace esphome::ocpp
