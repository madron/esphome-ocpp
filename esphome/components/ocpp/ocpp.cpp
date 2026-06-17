#include "ocpp.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";

}  // namespace

void OcppComponent::setup() {
  this->server_.set_listener(this);
  this->server_.set_max_clients(this->charge_points_.size());
  this->server_.set_subprotocol("ocpp1.6");
  if (!this->server_.setup()) {
    ESP_LOGE(TAG, "Could not start OCPP server");
    this->mark_failed();
  }
}

void OcppComponent::loop() {
  this->server_.loop();
  uint32_t now = millis();
  for (auto *charge_point : this->charge_points_) {
    if (charge_point != nullptr)
      charge_point->loop(now);
  }
}

void OcppComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "OCPP:");
  std::string charger_url = this->server_.get_charger_url();
  ESP_LOGCONFIG(TAG, "  %s", charger_url.c_str());
  ESP_LOGCONFIG(TAG, "  Charge points: %u", static_cast<unsigned>(this->charge_points_.size()));
  for (auto *charge_point : this->charge_points_) {
    if (charge_point != nullptr) {
      ESP_LOGCONFIG(TAG, "    - charge_point_id: %s", charge_point->get_charge_point_id().c_str());
      if (charge_point->get_debug_ocpp_messages())
        ESP_LOGCONFIG(TAG, "      debug_ocpp_messages: true");
      if (charge_point->get_force_boot_notification())
        ESP_LOGCONFIG(TAG, "      force_boot_notification: true");
    }
  }
}

float OcppComponent::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

void OcppComponent::on_websocket_connected(const std::string &connection_id) {
  ChargePoint *charge_point = this->assign_charge_point_for_connection_(connection_id);
  if (charge_point == nullptr) {
    ESP_LOGW(TAG, "No charge point available for '%s'", connection_id.c_str());
  } else {
    charge_point->on_connected(connection_id, millis());
  }
}

void OcppComponent::on_websocket_disconnected(const std::string &connection_id) {
  ChargePoint *charge_point = this->find_charge_point_by_connection_id_(connection_id);
  if (charge_point != nullptr) {
    bool dynamic_slot = charge_point->get_charge_point_id().empty();
    charge_point->on_disconnected();
    if (dynamic_slot)
      charge_point->set_connection_id("");
  }
}

void OcppComponent::on_websocket_text(const std::string &connection_id, const std::string &message) {
  ChargePoint *charge_point = this->find_charge_point_by_connection_id_(connection_id);
  if (charge_point != nullptr)
    charge_point->handle_ocpp_text(message);
}

void OcppComponent::send_ocpp_text(const std::string &connection_id, const std::string &message) {
  this->server_.send_text(connection_id, message);
}

ChargePoint *OcppComponent::find_charge_point_by_connection_id_(const std::string &connection_id) const {
  for (auto *charge_point : this->charge_points_) {
    if (charge_point != nullptr && charge_point->get_connection_id() == connection_id)
      return charge_point;
  }
  return nullptr;
}

ChargePoint *OcppComponent::assign_charge_point_for_connection_(const std::string &connection_id) const {
  ChargePoint *charge_point = this->find_charge_point_by_connection_id_(connection_id);
  if (charge_point != nullptr)
    return charge_point;
  for (auto *charge_point : this->charge_points_) {
    if (charge_point != nullptr && charge_point->get_connection_id().empty()) {
      charge_point->set_connection_id(connection_id);
      return charge_point;
    }
  }
  return nullptr;
}

}  // namespace esphome::ocpp
