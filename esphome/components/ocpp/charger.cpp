#include "charger.h"

#include <algorithm>
#include <utility>

namespace esphome::ocpp {

void configure_charger(ConfiguredCharger *charger, std::string charge_point_id, float max_current) {
  if (charger == nullptr)
    return;
  charger->charge_point_id = std::move(charge_point_id);
  charger->max_current = max_current;
  charger->drawn_current_sensor = nullptr;
  charger->drawn_current_source_sensor = nullptr;
  charger->drawn_current_source_sensors = {};
  charger->latest_drawn_current = {};
  charger->has_connector = false;
}

bool charger_has_charge_point_id(const ConfiguredCharger &charger, const std::string &charge_point_id) {
  return charger.charge_point_id == charge_point_id;
}

float effective_connector_max_current(float charger_max_current, float connector_max_current) {
  return std::min(charger_max_current, connector_max_current);
}

float charger_drawn_current_max(const ConfiguredCharger &charger) {
  return std::max(charger.latest_drawn_current[0],
                  std::max(charger.latest_drawn_current[1], charger.latest_drawn_current[2]));
}

std::array<float, 3> charger_drawn_current_from_source(float source_current) {
  return {source_current, source_current, source_current};
}

void update_charger_drawn_current_from_source(ConfiguredCharger *charger, float source_current) {
  if (charger == nullptr)
    return;
  charger->latest_drawn_current = charger_drawn_current_from_source(source_current);
}

std::array<float, 3> charger_drawn_current_from_connectors(const ConfiguredCharger &charger) {
  std::array<float, 3> drawn_current{};
  if (!charger.has_connector)
    return drawn_current;
  for (uint8_t i = 0; i < drawn_current.size(); i++)
    drawn_current[i] += charger.connector.latest_drawn_current[i];
  return drawn_current;
}

void update_charger_drawn_current_from_connectors(ConfiguredCharger *charger) {
  if (charger == nullptr)
    return;
  charger->latest_drawn_current = charger_drawn_current_from_connectors(*charger);
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
