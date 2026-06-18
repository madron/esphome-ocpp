#include "protocol.h"
#include "esphome/core/log.h"

#include <memory>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static constexpr const char *CURRENT_TIME = "1970-01-01T00:00:00Z";

struct OcppEnvelope {
    OcppMessageType message_type_id;
    std::string unique_id;
    std::string action;
    size_t payload_pos{0};
};

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

bool read_quoted(const std::string &message, size_t *pos, std::string *out) {
    if (*pos >= message.size() || message[*pos] != '"')
        return false;
    (*pos)++;
    out->clear();
    while (*pos < message.size()) {
        char c = message[(*pos)++];
        if (c == '"')
            return true;
        if (c == '\\' && *pos < message.size())
            c = message[(*pos)++];
        out->push_back(c);
    }
    return false;
}

void skip_ws(const std::string &message, size_t *pos) {
    while (*pos < message.size() &&
            (message[*pos] == ' ' || message[*pos] == '\t' || message[*pos] == '\r' || message[*pos] == '\n'))
        (*pos)++;
}

bool expect_char(const std::string &message, size_t *pos, char expected) {
    skip_ws(message, pos);
    if (*pos >= message.size() || message[*pos] != expected)
        return false;
    (*pos)++;
    return true;
}

bool parse_ocpp_envelope(const std::string &message, OcppEnvelope *envelope) {
    size_t pos = 0;
    if (!expect_char(message, &pos, '['))
        return false;

    skip_ws(message, &pos);
    if (pos >= message.size())
        return false;
    char message_type = message[pos++];
    if (message_type == '2')
        envelope->message_type_id = OcppMessageType::CALL;
    else if (message_type == '3')
        envelope->message_type_id = OcppMessageType::CALL_RESULT;
    else if (message_type == '4')
        envelope->message_type_id = OcppMessageType::CALL_ERROR;
    else
        return false;

    if (!expect_char(message, &pos, ',') || !read_quoted(message, &pos, &envelope->unique_id))
        return false;

    if (envelope->message_type_id != OcppMessageType::CALL)
        return true;

    if (!expect_char(message, &pos, ',') || !read_quoted(message, &pos, &envelope->action) || !expect_char(message, &pos, ','))
        return false;
    envelope->payload_pos = pos;
    return true;
}

bool find_string_field_after(const std::string &message, size_t start_pos, const char *field_name, std::string *out) {
    std::string key = std::string("\"") + field_name + "\"";
    size_t pos = message.find(key, start_pos);
    if (pos == std::string::npos)
        return false;
    pos += key.size();
    if (!expect_char(message, &pos, ':'))
        return false;
    return read_quoted(message, &pos, out);
}

std::unique_ptr<OcppMessage> parse_boot_notification_1_6(const OcppEnvelope &envelope, const std::string &message) {
    std::string charge_point_model;
    std::string charge_point_vendor;
    std::string firmware_version;
    find_string_field_after(message, envelope.payload_pos, "chargePointModel", &charge_point_model);
    find_string_field_after(message, envelope.payload_pos, "chargePointVendor", &charge_point_vendor);
    find_string_field_after(message, envelope.payload_pos, "firmwareVersion", &firmware_version);
    return std::unique_ptr<OcppMessage>(new BootNotification(
        envelope.unique_id,
        charge_point_model,
        charge_point_vendor,
        firmware_version
    ));
}

std::unique_ptr<OcppMessage> parse_boot_notification_2_0_1(const OcppEnvelope &envelope, const std::string &message) {
    std::string charge_point_model;
    std::string charge_point_vendor;
    std::string firmware_version;
    size_t charging_station_pos = message.find("\"chargingStation\"", envelope.payload_pos);
    if (charging_station_pos != std::string::npos) {
        find_string_field_after(message, charging_station_pos, "model", &charge_point_model);
        find_string_field_after(message, charging_station_pos, "vendorName", &charge_point_vendor);
        find_string_field_after(message, charging_station_pos, "firmwareVersion", &firmware_version);
    }
    return std::unique_ptr<OcppMessage>(new BootNotification(
        envelope.unique_id,
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
    OcppEnvelope envelope{OcppMessageType::CALL};
    if (!parse_ocpp_envelope(message, &envelope)) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
        return nullptr;
    }

    if (envelope.message_type_id != OcppMessageType::CALL)
        return std::unique_ptr<OcppMessage>(new OcppMessage(envelope.message_type_id, envelope.unique_id));

    if (envelope.action == "BootNotification") {
        if (this->version_ == OcppProtocolVersion::OCPP_2_0_1)
            return parse_boot_notification_2_0_1(envelope, message);
        return parse_boot_notification_1_6(envelope, message);
    }

    return std::unique_ptr<OcppMessage>(new OcppCall(envelope.action, envelope.unique_id));
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

std::string OcppProtocol::make_ocpp_error(const std::string &unique_id, const char *code, const char *description) const {
    return "[4,\"" + json_escape(unique_id) + "\",\"" + code + "\",\"" + description + "\",{}]";
}

}  // namespace esphome::ocpp
