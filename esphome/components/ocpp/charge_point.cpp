#include "charge_point.h"

#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#else
#define ESP_LOGD(tag, ...)
#endif

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

void ChargePoint::set_debug_ocpp_messages(bool debug_ocpp_messages) { this->debug_ocpp_messages_ = debug_ocpp_messages; }
bool ChargePoint::get_debug_ocpp_messages() const { return this->debug_ocpp_messages_; }
void ChargePoint::set_force_boot_notification(bool force_boot_notification) {
  this->force_boot_notification_ = force_boot_notification;
}
bool ChargePoint::get_force_boot_notification() const { return this->force_boot_notification_; }
bool ChargePoint::is_online() const { return this->online_; }

void ChargePoint::on_connected(std::string connection_id, uint32_t now_millis) {
  this->set_connection_id(std::move(connection_id));
  this->connected_ = true;
  this->connected_at_millis_ = now_millis;
  this->force_boot_notification_scheduled_ =
      this->force_boot_notification_ && this->force_boot_notification_pending_;
}

void ChargePoint::on_disconnected() {
  this->connected_ = false;
  this->force_boot_notification_scheduled_ = false;
  this->set_online_(false);
}

void ChargePoint::handle_ocpp_text(const std::string &message) {
  if (this->debug_ocpp_messages_)
    ESP_LOGD(TAG, "%s << %s", this->connection_id_.c_str(), message.c_str());
  this->apply_protocol_result_(this->protocol_.handle_text(this->connection_id_, message));
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
  if (this->message_sink_ != nullptr)
    this->message_sink_->send_ocpp_text(this->connection_id_, message);
}

void ChargePoint::apply_protocol_result_(const OcppProtocolResult &result) {
  for (const auto &event : result.events) {
    if (event.type == OcppProtocolEventType::BOOT_NOTIFICATION_ACCEPTED) {
      this->force_boot_notification_pending_ = false;
      this->force_boot_notification_scheduled_ = false;
      this->set_online_(true);
    } else if (event.type == OcppProtocolEventType::HEARTBEAT_RECEIVED ||
               event.type == OcppProtocolEventType::STATUS_NOTIFICATION_RECEIVED) {
      this->set_online_(true);
    }
  }
  for (const auto &message : result.outbound_messages) {
    this->send_message_(message);
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
