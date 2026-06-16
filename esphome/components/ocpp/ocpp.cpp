#include "ocpp.h"

#include "esphome/core/log.h"

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";

}  // namespace

void OcppComponent::setup() {
  this->protocol_.set_transport(this);
  this->server_.set_listener(this);
  this->server_.set_subprotocol("ocpp1.6");
  if (!this->server_.setup()) {
    ESP_LOGE(TAG, "Could not start OCPP server");
    this->mark_failed();
  }
}

void OcppComponent::loop() { this->server_.loop(); }

void OcppComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "OCPP server:");
  this->server_.dump_config();
  ESP_LOGCONFIG(TAG, "  Configured charge points: %u", static_cast<unsigned>(this->charge_points_.size()));
  for (auto *charge_point : this->charge_points_) {
    if (charge_point != nullptr) {
      ESP_LOGCONFIG(TAG, "    - charge_point_id: %s", charge_point->get_charge_point_id().c_str());
      if (charge_point->get_debug_ocpp_messages())
        ESP_LOGCONFIG(TAG, "      debug_ocpp_messages: true");
    }
  }
}

float OcppComponent::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

void OcppComponent::on_websocket_connected(const std::string &connection_id) {
  const ChargePoint *charge_point = this->find_charge_point_by_id_(connection_id);
  if (charge_point == nullptr) {
    ESP_LOGW(TAG, "Connected charge point '%s' is not configured", connection_id.c_str());
    this->protocol_.set_debug_ocpp_messages(false);
  } else {
    this->protocol_.set_debug_ocpp_messages(charge_point->get_debug_ocpp_messages());
  }
  this->protocol_.on_connected(connection_id);
}

void OcppComponent::on_websocket_disconnected() { this->protocol_.on_disconnected(); }

void OcppComponent::on_websocket_text(const std::string &message) { this->protocol_.handle_text(message); }

void OcppComponent::send_ocpp_text(const std::string &message) { this->server_.send_text(message); }

const ChargePoint *OcppComponent::find_charge_point_by_id_(const std::string &charge_point_id) const {
  for (auto *charge_point : this->charge_points_) {
    if (charge_point != nullptr && charge_point->get_charge_point_id() == charge_point_id)
      return charge_point;
  }
  return nullptr;
}

}  // namespace esphome::ocpp
