#include "protocol.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

#include <memory>

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

bool parse_message_type_id(int raw_message_type_id, OcppMessageType *message_type_id) {
    if (raw_message_type_id == 2) {
        *message_type_id = OcppMessageType::CALL;
        return true;
    }
    if (raw_message_type_id == 3) {
        *message_type_id = OcppMessageType::CALL_RESULT;
        return true;
    }
    if (raw_message_type_id == 4) {
        *message_type_id = OcppMessageType::CALL_ERROR;
        return true;
    }
    return false;
}

std::string json_string_or_empty(const JsonVariant &value) {
    if (!value.is<const char *>())
        return "";
    return value.as<std::string>();
}

std::unique_ptr<OcppMessage> parse_boot_notification_1_6(const std::string &unique_id, const JsonObject &payload) {
    return std::unique_ptr<OcppMessage>(new BootNotification(
        unique_id,
        json_string_or_empty(payload["chargePointModel"]),
        json_string_or_empty(payload["chargePointVendor"]),
        json_string_or_empty(payload["firmwareVersion"])
    ));
}

std::unique_ptr<OcppMessage> parse_boot_notification_2_0_1(const std::string &unique_id, const JsonObject &payload) {
    std::string charge_point_model;
    std::string charge_point_vendor;
    std::string firmware_version;
    JsonVariant charging_station = payload["chargingStation"];
    if (charging_station.is<JsonObject>()) {
        JsonObject charging_station_object = charging_station.as<JsonObject>();
        charge_point_model = json_string_or_empty(charging_station_object["model"]);
        charge_point_vendor = json_string_or_empty(charging_station_object["vendorName"]);
        firmware_version = json_string_or_empty(charging_station_object["firmwareVersion"]);
    }
    return std::unique_ptr<OcppMessage>(new BootNotification(
        unique_id,
        charge_point_model,
        charge_point_vendor,
        firmware_version
    ));
}

}  // namespace

bool OcppProtocol::set_websocket_protocol(const std::string &protocol) {
    if (protocol.empty() || protocol == "ocpp1.6") {
        this->version_ = OcppProtocolVersion::OCPP_1_6;
        return true;
    }
    if (protocol == "ocpp2.0.1") {
        this->version_ = OcppProtocolVersion::OCPP_2_0_1;
        return true;
    }
    return false;
}

OcppProtocolVersion OcppProtocol::get_version() const { return this->version_; }

std::unique_ptr<OcppMessage> OcppProtocol::parse_message(const std::string &message) const {
    JsonDocument document = json::parse_json(message);
    if (document.overflowed() || document.isNull() || !document.is<JsonArray>()) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
        return nullptr;
    }

    JsonArray frame = document.as<JsonArray>();
    if (frame.size() < 2 || !frame[static_cast<size_t>(0)].is<int>() || !frame[static_cast<size_t>(1)].is<const char *>()) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP frame: %s", message.c_str());
        return nullptr;
    }

    OcppMessageType message_type_id{OcppMessageType::CALL};
    if (!parse_message_type_id(frame[static_cast<size_t>(0)].as<int>(), &message_type_id)) {
        ESP_LOGW(TAG, "Ignoring OCPP frame with unsupported MessageTypeId: %s", message.c_str());
        return nullptr;
    }

    std::string unique_id = frame[static_cast<size_t>(1)].as<std::string>();
    if (message_type_id != OcppMessageType::CALL)
        return std::unique_ptr<OcppMessage>(new OcppMessage(message_type_id, unique_id));

    if (frame.size() < 4 || !frame[static_cast<size_t>(2)].is<const char *>() || !frame[static_cast<size_t>(3)].is<JsonObject>()) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP CALL frame: %s", message.c_str());
        return nullptr;
    }

    std::string action = frame[static_cast<size_t>(2)].as<std::string>();
    JsonObject payload = frame[static_cast<size_t>(3)].as<JsonObject>();

    if (action == "BootNotification") {
        if (this->version_ == OcppProtocolVersion::OCPP_2_0_1)
            return parse_boot_notification_2_0_1(unique_id, payload);
        return parse_boot_notification_1_6(unique_id, payload);
    }

    return std::unique_ptr<OcppMessage>(new OcppCall(action, unique_id));
}

std::string OcppProtocol::make_boot_notification_response(const std::string &unique_id) const {
    return "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME +
           "\",\"interval\":300,\"status\":\"Accepted\"}]";
}

std::string OcppProtocol::make_heartbeat_response(const std::string &unique_id) const {
    return "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME + "\"}]";
}

std::string OcppProtocol::make_status_notification_response(const std::string &unique_id) const {
    return "[3,\"" + json_escape(unique_id) + "\",{}]";
}

std::string OcppProtocol::make_trigger_boot_notification(const std::string &unique_id) const {
    return "[2,\"" + json_escape(unique_id) + "\",\"TriggerMessage\",{\"requestedMessage\":\"BootNotification\"}]";
}

std::string OcppProtocol::make_trigger_status_notification(const std::string &unique_id) const {
    return "[2,\"" + json_escape(unique_id) + "\",\"TriggerMessage\",{\"requestedMessage\":\"StatusNotification\"}]";
}

std::string OcppProtocol::make_ocpp_error(const std::string &unique_id, const char *code, const char *description) const {
    return "[4,\"" + json_escape(unique_id) + "\",\"" + code + "\",\"" + description + "\",{}]";
}

}  // namespace esphome::ocpp
