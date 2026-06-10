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
}

float OcppComponent::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

void OcppComponent::on_websocket_connected(const std::string &connection_id) { this->protocol_.on_connected(connection_id); }

void OcppComponent::on_websocket_disconnected() { this->protocol_.on_disconnected(); }

void OcppComponent::on_websocket_text(const std::string &message) { this->protocol_.handle_text(message); }

void OcppComponent::send_ocpp_text(const std::string &message) { this->server_.send_text(message); }

}  // namespace esphome::ocpp
