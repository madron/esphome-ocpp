#include "protocol.h"
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

bool expect_call_message_type(const std::string &message, size_t *pos) {
    skip_ws(message, pos);
    if (*pos >= message.size() || message[*pos] != '2')
        return false;
    (*pos)++;
    return true;
}

bool expect_message_type(const std::string &message, size_t *pos, char expected) {
    skip_ws(message, pos);
    if (*pos >= message.size() || message[*pos] != expected)
        return false;
    (*pos)++;
    return true;
}

bool parse_ocpp_message_type(const std::string &message, char *message_type) {
    size_t pos = 0;
    if (!expect_char(message, &pos, '['))
        return false;
    skip_ws(message, &pos);
    if (pos >= message.size())
        return false;
    char type = message[pos++];
    if (type != '2' && type != '3' && type != '4')
        return false;
    *message_type = type;
    return true;
}

bool parse_ocpp_response(const std::string &message, char expected_type, std::string *unique_id) {
    size_t pos = 0;
    return expect_char(message, &pos, '[') && expect_message_type(message, &pos, expected_type) &&
            expect_char(message, &pos, ',') && read_quoted(message, &pos, unique_id);
}

bool parse_ocpp_call(const std::string &message, std::string *unique_id, std::string *action) {
    size_t pos = 0;
    return expect_char(message, &pos, '[') && expect_call_message_type(message, &pos) && expect_char(message, &pos, ',') &&
            read_quoted(message, &pos, unique_id) && expect_char(message, &pos, ',') && read_quoted(message, &pos, action) &&
            expect_char(message, &pos, ',');
}

}  // namespace

OcppProtocolResult OcppProtocol::handle_text(const std::string &charge_point_id, const std::string &message) {
    OcppProtocolResult result;
    std::string unique_id;
    std::string action;
    char message_type = '\0';
    if (!parse_ocpp_message_type(message, &message_type)) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
        return result;
    }

    if (message_type == '3') {
        if (parse_ocpp_response(message, message_type, &unique_id))
            ESP_LOGD(TAG, "OCPP call result: charge_point='%s' uniqueId='%s'", charge_point_id.c_str(), unique_id.c_str());
        else
            ESP_LOGW(TAG, "Ignoring invalid OCPP CallResult message: %s", message.c_str());
        return result;
    }

    if (message_type == '4') {
        if (parse_ocpp_response(message, message_type, &unique_id))
            ESP_LOGW(TAG, "OCPP call error: charge_point='%s' uniqueId='%s'", charge_point_id.c_str(), unique_id.c_str());
        else
            ESP_LOGW(TAG, "Ignoring invalid OCPP CallError message: %s", message.c_str());
        return result;
    }

    if (!parse_ocpp_call(message, &unique_id, &action)) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
        return result;
    }

    ESP_LOGD(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", charge_point_id.c_str(), action.c_str(),
            unique_id.c_str());
    if (action == "BootNotification") {
        this->handle_boot_notification_(unique_id, &result);
    } else if (action == "Heartbeat") {
        this->handle_heartbeat_(unique_id, &result);
    } else if (action == "StatusNotification") {
        this->handle_status_notification_(unique_id, &result);
    } else {
        ESP_LOGW(TAG, "Unsupported OCPP action '%s' from charge point '%s'", action.c_str(), charge_point_id.c_str());
        result.outbound_messages.push_back(
            this->make_ocpp_error_(unique_id, "NotImplemented", "This OCPP action is not implemented"));
    }
    return result;
}

std::string OcppProtocol::make_trigger_boot_notification(const std::string &unique_id) const {
    return "[2,\"" + json_escape(unique_id) + "\",\"TriggerMessage\",{\"requestedMessage\":\"BootNotification\"}]";
}

void OcppProtocol::handle_boot_notification_(const std::string &unique_id, OcppProtocolResult *result) {
    std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME +
                            "\",\"interval\":300,\"status\":\"Accepted\"}]";
    result->events.push_back({OcppProtocolEventType::BOOT_NOTIFICATION_ACCEPTED});
    result->outbound_messages.push_back(response);
}

void OcppProtocol::handle_heartbeat_(const std::string &unique_id, OcppProtocolResult *result) {
    std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME + "\"}]";
    result->events.push_back({OcppProtocolEventType::HEARTBEAT_RECEIVED});
    result->outbound_messages.push_back(response);
}

void OcppProtocol::handle_status_notification_(const std::string &unique_id, OcppProtocolResult *result) {
    std::string response = "[3,\"" + json_escape(unique_id) + "\",{}]";
    result->events.push_back({OcppProtocolEventType::STATUS_NOTIFICATION_RECEIVED});
    result->outbound_messages.push_back(response);
}

std::string OcppProtocol::make_ocpp_error_(const std::string &unique_id, const char *code, const char *description) const {
    return "[4,\"" + json_escape(unique_id) + "\",\"" + code + "\",\"" + description + "\",{}]";
}

}  // namespace esphome::ocpp
