#include "ocpp.h"

#ifdef USE_OCPP

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static constexpr const char *CURRENT_TIME = "1970-01-01T00:00:00Z";

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


void OcppServer::handle_ws_text_(const std::string &message) {
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
