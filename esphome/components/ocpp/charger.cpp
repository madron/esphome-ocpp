#include "charger.h"

#include <algorithm>
#include <utility>

namespace esphome::ocpp {

void configure_charger(ConfiguredCharger *charger, std::string charge_point_id, float max_current, uint8_t phases) {
  if (charger == nullptr)
    return;
  charger->charge_point_id = std::move(charge_point_id);
  charger->max_current = max_current;
  charger->phases = phases == 3 ? 3 : 1;
  charger->phase_mapping = {0, 1, 2};
  charger->used_current_sensor = nullptr;
  charger->used_current_sensors = {};
  charger->latest_used_current = {};
  charger->latest_used_current_phase_specific = false;
  charger->has_connector = false;
}

bool charger_has_charge_point_id(const ConfiguredCharger &charger, const std::string &charge_point_id) {
  return charger.charge_point_id == charge_point_id;
}

float effective_connector_max_current(float charger_max_current, float connector_max_current) {
  return std::min(charger_max_current, connector_max_current);
}

float charger_used_current_max(const ConfiguredCharger &charger) {
  return std::max(charger.latest_used_current[0],
                  std::max(charger.latest_used_current[1], charger.latest_used_current[2]));
}

std::array<bool, 3> charger_configured_load_phases(const ConfiguredCharger &charger) {
  std::array<bool, 3> load_phases{};
  const uint8_t active_phases = charger.phases == 3 ? 3 : 1;
  for (uint8_t i = 0; i < active_phases; i++)
    load_phases[i] = true;
  return load_phases;
}

std::array<bool, 3> charger_effective_load_phases(const ConfiguredCharger &charger,
                                                  const ConfiguredConnector *connector) {
  auto load_phases = charger_configured_load_phases(charger);
  if (charger.phases != 3)
    return load_phases;

  const std::array<float, 3> *phase_current = nullptr;
  if (connector != nullptr && connector->has_phase_specific_current_import) {
    phase_current = &connector->latest_used_current;
  } else if (charger.latest_used_current_phase_specific) {
    phase_current = &charger.latest_used_current;
  }
  if (phase_current == nullptr)
    return load_phases;

  std::array<bool, 3> detected_phases{};
  uint8_t detected_phase_count = 0;
  for (uint8_t i = 0; i < charger.phases; i++) {
    if ((*phase_current)[i] > 0.1f) {
      detected_phases[i] = true;
      detected_phase_count++;
    }
  }
  return detected_phase_count == 1 ? detected_phases : load_phases;
}

std::array<float, 3> charger_used_current_from_connectors(const ConfiguredCharger &charger) {
  std::array<float, 3> used_current{};
  if (!charger.has_connector)
    return used_current;
  for (uint8_t i = 0; i < used_current.size(); i++)
    used_current[i] += charger.connector.latest_used_current[i];
  return used_current;
}

void update_charger_used_current_from_connectors(ConfiguredCharger *charger) {
  if (charger == nullptr)
    return;
  charger->latest_used_current = charger_used_current_from_connectors(*charger);
  charger->latest_used_current_phase_specific = charger->has_connector &&
                                                charger->connector.has_phase_specific_current_import;
}

ConfiguredConnector *find_configured_connector(ConfiguredCharger *charger, int connector_id) {
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return nullptr;
  return &charger->connector;
}

const ConfiguredConnector *find_configured_connector(const ConfiguredCharger *charger, int connector_id) {
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return nullptr;
  return &charger->connector;
}

ConfiguredConnector *find_active_transaction_connector(ConfiguredCharger *charger) {
  if (charger == nullptr || !charger->has_connector || !charger->connector.has_active_transaction)
    return nullptr;
  return &charger->connector;
}

ConfiguredConnector *find_transaction_connector(ConfiguredCharger *charger, uint32_t transaction_id) {
  if (charger == nullptr || !charger->has_connector || !charger->connector.has_active_transaction ||
      charger->connector.active_transaction_id != transaction_id)
    return nullptr;
  return &charger->connector;
}

}  // namespace esphome::ocpp
