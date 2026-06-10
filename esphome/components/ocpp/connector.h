#pragma once

#include <array>
#include <cstdint>
#include <string>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#ifdef USE_OCPP
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/preferences.h"
#endif

namespace esphome::sensor {
class Sensor;
}  // namespace esphome::sensor

namespace esphome::text_sensor {
class TextSensor;
}  // namespace esphome::text_sensor

namespace esphome::ocpp {

class OcppServer;
class OcppCurrentLimitNumber;
class OcppConnectorEnabledSwitch;
class OcppConnectorButton;

#ifdef USE_OCPP
class OcppConnectorEnabledSwitch : public switch_::Switch {
 public:
  void set_parent(OcppServer *parent, uint8_t connector_id) {
    this->parent_ = parent;
    this->connector_id_ = connector_id;
  }

 protected:
  void write_state(bool state) override;

  OcppServer *parent_{nullptr};
  uint8_t connector_id_{0};
};

class OcppConnectorButton : public button::Button {
 public:
  void set_parent(OcppServer *parent, uint8_t connector_id) {
    this->parent_ = parent;
    this->connector_id_ = connector_id;
  }

 protected:
  void press_action() override;

  OcppServer *parent_{nullptr};
  uint8_t connector_id_{0};
};

class OcppCurrentLimitNumber : public number::Number, public Component {
 public:
  void set_parent(OcppServer *parent, uint8_t connector_id) {
    this->parent_ = parent;
    this->connector_id_ = connector_id;
  }
  void set_initial_value(float initial_value) { this->initial_value_ = initial_value; }
  void set_restore_value(bool restore_value) { this->restore_value_ = restore_value; }

  void setup() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  void control(float value) override;

  OcppServer *parent_{nullptr};
  uint8_t connector_id_{0};
  float initial_value_{0.0f};
  bool restore_value_{false};
  ESPPreferenceObject pref_;
};
#endif

struct ConfiguredConnector {
  uint8_t id;
  float max_current;
  sensor::Sensor *current_sensor{nullptr};
  text_sensor::TextSensor *state_sensor{nullptr};
  OcppCurrentLimitNumber *current_limit_number{nullptr};
  OcppConnectorEnabledSwitch *enabled_switch{nullptr};
  OcppConnectorButton *restart_button{nullptr};
  bool enabled{true};
  bool has_preferred_current_limit{false};
  float preferred_current_limit{0.0f};
  bool has_active_transaction{false};
  uint32_t active_transaction_id{0};
  std::string active_id_tag;
  bool is_charging{false};
  bool charging_profile_applied{false};
  bool has_latest_current_import{false};
  bool has_latest_power_active_import{false};
  bool has_session_current_import{false};
  std::string state{"unplugged"};
  float available_current{0.0f};
  float allocated_current{0.0f};
  float latest_current_import{0.0f};
  float latest_power_active_import{0.0f};
};

float effective_allocated_current(float available_current, float max_current, float requested_current, float min_current,
                                  bool enabled);
float effective_allocated_current(float available_current, float min_current);
float equal_available_current(float site_available_current, float connector_current, uint8_t active_connector_count);
const char *connector_state_from_ocpp_status(const char *status);
void reset_connector_session_current(ConfiguredConnector *connector);
void update_connector_allocation(ConfiguredConnector *connector, float available_current, float min_current);

}  // namespace esphome::ocpp
