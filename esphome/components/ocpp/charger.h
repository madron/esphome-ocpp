#pragma once

#include "connector.h"

#include <array>
#include <cstdint>
#include <string>

namespace esphome::ocpp {

struct ConfiguredCharger {
  std::string charge_point_id;
  float max_current{0.0f};
  uint8_t phases{3};
  std::array<uint8_t, 3> phase_mapping{0, 1, 2};
  sensor::Sensor *used_current_sensor{nullptr};
  std::array<sensor::Sensor *, 3> used_current_sensors{};
  std::array<float, 3> latest_used_current{};
  bool latest_used_current_phase_specific{false};
  ConfiguredConnector connector;
  bool has_connector{false};
};

void configure_charger(ConfiguredCharger *charger, std::string charge_point_id, float max_current, uint8_t phases = 3);
bool charger_has_charge_point_id(const ConfiguredCharger &charger, const std::string &charge_point_id);
float effective_connector_max_current(float charger_max_current, float connector_max_current);
float charger_used_current_max(const ConfiguredCharger &charger);
std::array<bool, 3> charger_configured_load_phases(const ConfiguredCharger &charger);
std::array<bool, 3> charger_effective_load_phases(const ConfiguredCharger &charger,
                                                  const ConfiguredConnector *connector = nullptr);
std::array<float, 3> charger_used_current_from_connectors(const ConfiguredCharger &charger);
void update_charger_used_current_from_connectors(ConfiguredCharger *charger);
ConfiguredConnector *find_configured_connector(ConfiguredCharger *charger, int connector_id);
const ConfiguredConnector *find_configured_connector(const ConfiguredCharger *charger, int connector_id);
ConfiguredConnector *find_active_transaction_connector(ConfiguredCharger *charger);
ConfiguredConnector *find_transaction_connector(ConfiguredCharger *charger, uint32_t transaction_id);

}  // namespace esphome::ocpp
