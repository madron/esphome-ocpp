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
  charger->has_connector = false;
}

bool charger_has_charge_point_id(const ConfiguredCharger &charger, const std::string &charge_point_id) {
  return charger.charge_point_id == charge_point_id;
}

float effective_connector_max_current(float charger_max_current, float connector_max_current) {
  return std::min(charger_max_current, connector_max_current);
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
  (void) connector;
  return charger_configured_load_phases(charger);
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
