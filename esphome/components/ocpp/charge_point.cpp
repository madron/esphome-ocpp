#include "charge_point.h"
#include "esphome/core/log.h"

#include <memory>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp.charge_point";
static const char *const GET_CONFIGURATION_UNIQUE_ID = "get-configuration";
static const char *const TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID = "trigger-boot-notification";
static const char *const TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID = "trigger-status-notification";
static constexpr size_t DEBUG_OCPP_MESSAGE_CHUNK_SIZE = 360;

void log_ocpp_debug_message(const std::string &connection_id, const char *direction, const std::string &message) {
    if (message.size() <= DEBUG_OCPP_MESSAGE_CHUNK_SIZE) {
        ESP_LOGD(TAG, "%s %s %s", connection_id.c_str(), direction, message.c_str());
        return;
    }

    size_t chunks = (message.size() + DEBUG_OCPP_MESSAGE_CHUNK_SIZE - 1) / DEBUG_OCPP_MESSAGE_CHUNK_SIZE;
    ESP_LOGD(TAG, "%s %s payload_len=%u chunks=%u", connection_id.c_str(), direction,
            static_cast<unsigned>(message.size()), static_cast<unsigned>(chunks));
    for (size_t chunk = 0; chunk < chunks; chunk++) {
        size_t offset = chunk * DEBUG_OCPP_MESSAGE_CHUNK_SIZE;
        std::string part = message.substr(offset, DEBUG_OCPP_MESSAGE_CHUNK_SIZE);
        ESP_LOGD(TAG, "%s %s[%u/%u] %s", connection_id.c_str(), direction, static_cast<unsigned>(chunk + 1),
                static_cast<unsigned>(chunks), part.c_str());
    }
}

}  // namespace

void ChargePoint::set_charge_point_id(std::string charge_point_id) {
    this->charge_point_id_ = std::move(charge_point_id);
    this->connection_id_ = this->charge_point_id_;
}
const std::string &ChargePoint::get_charge_point_id() const { return this->charge_point_id_; }

void ChargePoint::set_connection_id(std::string connection_id) { this->connection_id_ = std::move(connection_id); }
const std::string &ChargePoint::get_connection_id() const { return this->connection_id_; }

void ChargePoint::set_force_protocol(std::string force_protocol) { this->forced_protocol_ = std::move(force_protocol); }
const std::string &ChargePoint::get_force_protocol() const { return this->forced_protocol_; }

void ChargePoint::set_debug_ocpp_messages(bool debug_ocpp_messages) { this->debug_ocpp_messages_ = debug_ocpp_messages; }
bool ChargePoint::get_debug_ocpp_messages() const { return this->debug_ocpp_messages_; }
void ChargePoint::set_startup_notifications_delay(uint32_t startup_notifications_delay_ms) {
    this->startup_notifications_delay_ms_ = startup_notifications_delay_ms;
}
uint32_t ChargePoint::get_startup_notifications_delay() const { return this->startup_notifications_delay_ms_; }
bool ChargePoint::is_online() const { return this->online_; }

void ChargePoint::on_connected(std::string connection_id, uint32_t now_millis) {
    this->on_connected(std::move(connection_id), "", now_millis);
}

void ChargePoint::on_connected(std::string connection_id, std::string protocol, uint32_t now_millis) {
    this->set_connection_id(std::move(connection_id));
    this->protocol_.set_websocket_protocol(protocol);
    this->connected_ = true;
    this->connected_at_millis_ = now_millis;
    if (this->protocol_text_sensor_ != nullptr)
        this->protocol_text_sensor_->publish_state(protocol);
    if (this->charger_info_text_sensor_ != nullptr)
        this->charger_info_text_sensor_->publish_state("");
}

void ChargePoint::on_disconnected() {
    this->connected_ = false;
    this->boot_notification_trigger_in_flight_ = false;
    this->status_notification_trigger_in_flight_ = false;
    this->in_flight_call_.reset();
    this->messages_.clear();
    if (this->protocol_text_sensor_ != nullptr)
        this->protocol_text_sensor_->publish_state("");
    if (this->charger_info_text_sensor_ != nullptr)
        this->charger_info_text_sensor_->publish_state("");
    this->set_online_(false);
}

void ChargePoint::handle_ocpp_text(const std::string &message) {
    if (this->debug_ocpp_messages_)
        log_ocpp_debug_message(this->connection_id_, "<<", message);
    std::unique_ptr<OcppMessage> ocpp_message = this->protocol_.parse_message(message);
    if (ocpp_message == nullptr)
        return;
    this->handle_ocpp_message_(*ocpp_message);
}

void ChargePoint::loop(uint32_t now_millis) {
    if (!this->connected_)
        return;
    this->expire_in_flight_call_(now_millis);
    if (this->startup_notifications_delay_ms_ == 0)
        return;
    if (now_millis - this->connected_at_millis_ < this->startup_notifications_delay_ms_)
        return;
    this->send_startup_notification_triggers_();
}

bool ChargePoint::pop_queued_message(std::string *message, uint32_t now_millis) {
    if (message == nullptr || this->messages_.empty())
        return false;

    auto selected = this->messages_.end();
    for (auto it = this->messages_.begin(); it != this->messages_.end(); ++it) {
        if (it->message_type_id == OcppMessageType::CALL && this->in_flight_call_ != nullptr)
            continue;
        selected = it;
        break;
    }
    if (selected == this->messages_.end())
        return false;

    QueuedMessage queued_message = std::move(*selected);
    this->messages_.erase(selected);
    if (queued_message.message_type_id == OcppMessageType::CALL) {
        queued_message.sent_at_millis = now_millis;
        this->in_flight_call_.reset(new QueuedMessage(queued_message));
    }
    if (this->debug_ocpp_messages_)
        log_ocpp_debug_message(this->connection_id_, ">>", queued_message.payload);
    *message = std::move(queued_message.payload);
    return true;
}

bool ChargePoint::send_message_(QueuedMessage message) {
    if (this->messages_.size() >= this->max_queued_messages_) {
        ESP_LOGW(TAG, "Dropping outbound OCPP message for '%s'; queue is full", this->connection_id_.c_str());
        return false;
    }
    this->messages_.push_back(std::move(message));
    return true;
}

void ChargePoint::handle_ocpp_message_(const OcppMessage &message) {
    if (message.message_type_id == OcppMessageType::CALL_RESULT) {
        ESP_LOGD(TAG, "OCPP call result: charge_point='%s' uniqueId='%s'", this->connection_id_.c_str(),
                message.unique_id.c_str());
        auto *get_configuration_response = dynamic_cast<const GetConfigurationResponse *>(&message);
        if (get_configuration_response != nullptr)
            this->handle_get_configuration_response_(*get_configuration_response);
        this->handle_ocpp_call_reply_(message);
        this->handle_startup_notification_trigger_reply_(message);
        return;
    }

    if (message.message_type_id == OcppMessageType::CALL_ERROR) {
        ESP_LOGW(TAG, "OCPP call error: charge_point='%s' uniqueId='%s'", this->connection_id_.c_str(),
                message.unique_id.c_str());
        this->handle_ocpp_call_reply_(message);
        this->handle_startup_notification_trigger_reply_(message);
        return;
    }

    if (message.action.empty()) {
        ESP_LOGW(TAG, "Ignoring OCPP CALL message without action: charge_point='%s' uniqueId='%s'",
                this->connection_id_.c_str(), message.unique_id.c_str());
        return;
    }
    this->handle_ocpp_call_(message);
}

void ChargePoint::handle_ocpp_call_(const OcppMessage &call) {
    ESP_LOGD(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", this->connection_id_.c_str(),
            call.action.c_str(), call.unique_id.c_str());

    if (call.action == "BootNotification") {
        bool was_boot_trigger_in_flight = this->boot_notification_trigger_in_flight_;
        this->boot_notification_pending_ = false;
        this->boot_notification_trigger_in_flight_ = false;
        this->set_online_(true);
        const auto &boot_notification = static_cast<const BootNotification &>(call);
        this->publish_charger_info_(boot_notification);
        bool boot_response_queued = this->send_message_({this->protocol_.make_boot_notification_response(call.unique_id),
                                                         OcppMessageType::CALL_RESULT, call.unique_id});
        if (boot_response_queued)
            this->send_get_configuration_request_();
        if (was_boot_trigger_in_flight)
            this->send_startup_notification_triggers_();
    } else if (call.action == "Heartbeat") {
        this->set_online_(true);
        this->send_message_({this->protocol_.make_heartbeat_response(call.unique_id), OcppMessageType::CALL_RESULT,
                             call.unique_id});
    } else if (call.action == "StatusNotification") {
        this->status_notification_pending_ = false;
        this->status_notification_trigger_in_flight_ = false;
        this->set_online_(true);
        this->send_message_({this->protocol_.make_status_notification_response(call.unique_id), OcppMessageType::CALL_RESULT,
                             call.unique_id});
    } else {
        ESP_LOGW(TAG, "Unsupported OCPP action '%s' from charge point '%s'", call.action.c_str(),
                this->connection_id_.c_str());
        this->send_message_({this->protocol_.make_ocpp_error(call.unique_id, "NotImplemented",
                                                            "This OCPP action is not implemented"),
                             OcppMessageType::CALL_ERROR, call.unique_id});
    }
}

void ChargePoint::handle_ocpp_call_reply_(const OcppMessage &message) {
    if (this->in_flight_call_ == nullptr)
        return;
    if (message.unique_id != this->in_flight_call_->unique_id)
        return;
    this->in_flight_call_.reset();
}

void ChargePoint::handle_get_configuration_response_(const GetConfigurationResponse &message) {
    this->meter_value_sample_interval_ = message.meter_value_sample_interval;
    this->meter_values_sampled_data_ = message.meter_values_sampled_data;
    this->connector_switch_3_to_1_phase_supported_ = message.connector_switch_3_to_1_phase_supported;

    ESP_LOGI(TAG,
             "GetConfiguration response: charge_point_id='%s' MeterValueSampleInterval='%s' MeterValuesSampledData='%s' "
             "ConnectorSwitch3to1PhaseSupported='%s'",
             this->connection_id_.c_str(), this->meter_value_sample_interval_.c_str(),
             this->meter_values_sampled_data_.c_str(), this->connector_switch_3_to_1_phase_supported_.c_str());
}

void ChargePoint::handle_startup_notification_trigger_reply_(const OcppMessage &message) {
    if (message.unique_id == TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID) {
        this->boot_notification_trigger_in_flight_ = false;
        this->send_startup_notification_triggers_();
    } else if (message.unique_id == TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID) {
        this->status_notification_trigger_in_flight_ = false;
    }
}

void ChargePoint::send_get_configuration_request_() {
    if (this->get_configuration_requested_)
        return;

    std::string request = this->protocol_.make_get_configuration_request(GET_CONFIGURATION_UNIQUE_ID);
    if (request.empty())
        return;

    if (!this->send_message_({std::move(request), OcppMessageType::CALL, GET_CONFIGURATION_UNIQUE_ID, "GetConfiguration"}))
        return;

    this->get_configuration_requested_ = true;
}

void ChargePoint::send_startup_notification_triggers_() {
    if (!this->connected_ || this->startup_notifications_delay_ms_ == 0)
        return;

    if (this->in_flight_call_ != nullptr)
        return;

    if (this->boot_notification_trigger_in_flight_ || this->status_notification_trigger_in_flight_)
        return;

    if (this->boot_notification_pending_) {
        this->send_boot_notification_trigger_();
        return;
    }

    if (this->status_notification_pending_)
        this->send_status_notification_trigger_();
}

void ChargePoint::send_boot_notification_trigger_() {
    this->boot_notification_pending_ = false;
    this->boot_notification_trigger_in_flight_ = true;
    this->send_message_({this->protocol_.make_trigger_boot_notification(TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID),
                         OcppMessageType::CALL, TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID, "TriggerMessage"});
}

void ChargePoint::send_status_notification_trigger_() {
    this->status_notification_pending_ = false;
    this->status_notification_trigger_in_flight_ = true;
    this->send_message_({this->protocol_.make_trigger_status_notification(TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID),
                         OcppMessageType::CALL, TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID, "TriggerMessage"});
}

void ChargePoint::expire_in_flight_call_(uint32_t now_millis) {
    if (this->in_flight_call_ == nullptr)
        return;
    if (now_millis - this->in_flight_call_->sent_at_millis < DEFAULT_CALL_TIMEOUT_MS)
        return;

    ESP_LOGW(TAG, "OCPP call timed out: charge_point='%s' action='%s' uniqueId='%s'", this->connection_id_.c_str(),
            this->in_flight_call_->action.c_str(), this->in_flight_call_->unique_id.c_str());
    OcppMessage timed_out_message(OcppMessageType::CALL_ERROR, this->in_flight_call_->unique_id);
    this->in_flight_call_.reset();
    this->handle_startup_notification_trigger_reply_(timed_out_message);
}

void ChargePoint::set_online_(bool online) {
    if (this->online_ == online)
        return;
    this->online_ = online;
    if (this->online_binary_sensor_ != nullptr)
        this->online_binary_sensor_->publish_state(online);
}

void ChargePoint::publish_charger_info_(const BootNotification &boot_notification) {
    if (this->charger_info_text_sensor_ == nullptr)
        return;

    std::string info;
    if (!boot_notification.charge_point_vendor.empty())
        info = "vendor: " + boot_notification.charge_point_vendor;
    if (!boot_notification.charge_point_model.empty()) {
        if (!info.empty())
            info += ", ";
        info += "model: " + boot_notification.charge_point_model;
    }
    if (!boot_notification.firmware_version.empty()) {
        if (!info.empty())
            info += ", ";
        info += "firmware: " + boot_notification.firmware_version;
    }
    this->charger_info_text_sensor_->publish_state(info);
}

}  // namespace esphome::ocpp
