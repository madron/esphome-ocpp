#include "ocpp.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";

}  // namespace

void OcppComponent::setup() {
}

void OcppComponent::loop() {
}

void OcppComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "OCPP:");
}

}  // namespace esphome::ocpp
