#include "charger.h"

#include <algorithm>
#include <utility>

#ifndef OCPP_SPLIT_IMPLEMENTATION_INLINE
#define OCPP_SPLIT_IMPLEMENTATION_INLINE
#endif

namespace esphome::ocpp {

OCPP_SPLIT_IMPLEMENTATION_INLINE void configure_charger(ConfiguredCharger *charger, std::string charge_point_id,
                                                        float max_current) {
  if (charger == nullptr)
    return;
  charger->charge_point_id = std::move(charge_point_id);
  charger->max_current = max_current;
  charger->has_connector = false;
}

OCPP_SPLIT_IMPLEMENTATION_INLINE bool charger_has_charge_point_id(const ConfiguredCharger &charger,
                                                                  const std::string &charge_point_id) {
  return charger.charge_point_id == charge_point_id;
}

OCPP_SPLIT_IMPLEMENTATION_INLINE float effective_connector_max_current(float charger_max_current,
                                                                       float connector_max_current) {
  return std::min(charger_max_current, connector_max_current);
}

OCPP_SPLIT_IMPLEMENTATION_INLINE ConfiguredConnector *find_configured_connector(ConfiguredCharger *charger,
                                                                                int connector_id) {
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return nullptr;
  return &charger->connector;
}

OCPP_SPLIT_IMPLEMENTATION_INLINE const ConfiguredConnector *find_configured_connector(const ConfiguredCharger *charger,
                                                                                      int connector_id) {
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return nullptr;
  return &charger->connector;
}

OCPP_SPLIT_IMPLEMENTATION_INLINE ConfiguredConnector *find_active_transaction_connector(ConfiguredCharger *charger) {
  if (charger == nullptr || !charger->has_connector || !charger->connector.has_active_transaction)
    return nullptr;
  return &charger->connector;
}

OCPP_SPLIT_IMPLEMENTATION_INLINE ConfiguredConnector *find_transaction_connector(ConfiguredCharger *charger,
                                                                                 uint32_t transaction_id) {
  if (charger == nullptr || !charger->has_connector || !charger->connector.has_active_transaction ||
      charger->connector.active_transaction_id != transaction_id)
    return nullptr;
  return &charger->connector;
}

}  // namespace esphome::ocpp
