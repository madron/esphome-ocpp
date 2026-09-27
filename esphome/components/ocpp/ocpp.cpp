#include "ocpp.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

// MicroOcpp is wired up as a build dependency (see llm-wiki/decisions.md) but
// not used yet: this include only proves the library resolves and compiles
// against this project's ESP-IDF build. No MicroOcpp API is called below.
#include <MicroOcpp.h>

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
