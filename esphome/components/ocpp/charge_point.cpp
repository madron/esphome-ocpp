#include "charge_point.h"
#include "esphome/core/log.h"

#include <memory>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp.charge_point";
static constexpr uint32_t FORCE_BOOT_NOTIFICATION_DELAY_MS = 5000;

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
void ChargePoint::set_force_boot_notification(bool force_boot_notification) {
    this->force_boot_notification_ = force_boot_notification;
}
bool ChargePoint::get_force_boot_notification() const { return this->force_boot_notification_; }
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
    this->force_boot_notification_scheduled_ =
        this->force_boot_notification_ && this->force_boot_notification_pending_;
}

void ChargePoint::on_disconnected() {
    this->connected_ = false;
    this->force_boot_notification_scheduled_ = false;
    this->messages_.clear();
    if (this->protocol_text_sensor_ != nullptr)
        this->protocol_text_sensor_->publish_state("");
    this->set_online_(false);
}

void ChargePoint::handle_ocpp_text(const std::string &message) {
    if (this->debug_ocpp_messages_)
        ESP_LOGD(TAG, "%s << %s", this->connection_id_.c_str(), message.c_str());
    std::unique_ptr<OcppMessage> ocpp_message = this->protocol_.parse_message(message);
    if (ocpp_message == nullptr)
        return;
    this->handle_ocpp_message_(*ocpp_message);
}

void ChargePoint::loop(uint32_t now_millis) {
    if (!this->connected_ || !this->force_boot_notification_scheduled_)
        return;
    if (now_millis - this->connected_at_millis_ < FORCE_BOOT_NOTIFICATION_DELAY_MS)
        return;
    this->send_forced_boot_notification_trigger_();
}

void ChargePoint::send_message_(const std::string &message) {
    if (this->debug_ocpp_messages_)
        ESP_LOGD(TAG, "%s >> %s", this->connection_id_.c_str(), message.c_str());
    if (this->messages_.size() >= this->max_queued_messages_) {
        ESP_LOGW(TAG, "Dropping outbound OCPP message for '%s'; queue is full", this->connection_id_.c_str());
        return;
    }
    this->messages_.push_back(message);
}

bool ChargePoint::pop_queued_message(std::string *message) {
    if (message == nullptr || this->messages_.empty())
        return false;
    *message = std::move(this->messages_.front());
    this->messages_.erase(this->messages_.begin());
    return true;
}

void ChargePoint::handle_ocpp_message_(const OcppMessage &message) {
    if (message.message_type_id == OcppMessageType::CALL_RESULT) {
        ESP_LOGD(TAG, "OCPP call result: charge_point='%s' uniqueId='%s'", this->connection_id_.c_str(),
                message.unique_id.c_str());
        return;
    }

    if (message.message_type_id == OcppMessageType::CALL_ERROR) {
        ESP_LOGW(TAG, "OCPP call error: charge_point='%s' uniqueId='%s'", this->connection_id_.c_str(),
                message.unique_id.c_str());
        return;
    }

    const OcppCall *call = message.as_call();
    if (call == nullptr) {
        ESP_LOGW(TAG, "Ignoring OCPP CALL message without action: charge_point='%s' uniqueId='%s'",
                this->connection_id_.c_str(), message.unique_id.c_str());
        return;
    }
    this->handle_ocpp_call_(*call);
}

void ChargePoint::handle_ocpp_call_(const OcppCall &call) {
    ESP_LOGD(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", this->connection_id_.c_str(),
            call.action.c_str(), call.unique_id.c_str());

    if (call.action == "BootNotification") {
        this->force_boot_notification_pending_ = false;
        this->force_boot_notification_scheduled_ = false;
        this->set_online_(true);
        this->send_message_(this->protocol_.make_boot_notification_response(call.unique_id));
    } else if (call.action == "Heartbeat") {
        this->set_online_(true);
        this->send_message_(this->protocol_.make_heartbeat_response(call.unique_id));
    } else if (call.action == "StatusNotification") {
        this->set_online_(true);
        this->send_message_(this->protocol_.make_status_notification_response(call.unique_id));
    } else {
        ESP_LOGW(TAG, "Unsupported OCPP action '%s' from charge point '%s'", call.action.c_str(),
                this->connection_id_.c_str());
        this->send_message_(
            this->protocol_.make_ocpp_error(call.unique_id, "NotImplemented", "This OCPP action is not implemented"));
    }
}

void ChargePoint::send_forced_boot_notification_trigger_() {
    this->force_boot_notification_pending_ = false;
    this->force_boot_notification_scheduled_ = false;
    this->send_message_(this->protocol_.make_trigger_boot_notification("trigger-boot-notification"));
}

void ChargePoint::set_online_(bool online) {
    if (this->online_ == online)
        return;
    this->online_ = online;
    if (this->online_binary_sensor_ != nullptr)
        this->online_binary_sensor_->publish_state(online);
}

}  // namespace esphome::ocpp
