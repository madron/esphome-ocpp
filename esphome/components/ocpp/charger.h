#pragma once

#include "connector.h"

#include <cstdint>
#include <string>

namespace esphome::ocpp {

struct ConfiguredCharger {
  std::string charge_point_id;
  float max_current{0.0f};
  ConfiguredConnector connector;
  bool has_connector{false};
};

void configure_charger(ConfiguredCharger *charger, std::string charge_point_id, float max_current);
bool charger_has_charge_point_id(const ConfiguredCharger &charger, const std::string &charge_point_id);
float effective_connector_max_current(float charger_max_current, float connector_max_current);
ConfiguredConnector *find_configured_connector(ConfiguredCharger *charger, int connector_id);
const ConfiguredConnector *find_configured_connector(const ConfiguredCharger *charger, int connector_id);
ConfiguredConnector *find_active_transaction_connector(ConfiguredCharger *charger);
ConfiguredConnector *find_transaction_connector(ConfiguredCharger *charger, uint32_t transaction_id);

}  // namespace esphome::ocpp
