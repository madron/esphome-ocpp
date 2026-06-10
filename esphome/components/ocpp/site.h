#pragma once

#include "charger.h"
#include "site_limits.h"

#ifdef USE_OCPP
#include "esphome/components/sensor/sensor.h"
#endif

#include <cstdint>

namespace esphome::ocpp {

class OcppServer;

struct ConfiguredSite {
  SiteLimitConfig limits;
  sensor::Sensor *storage_power_l1_sensor{nullptr};
  sensor::Sensor *storage_power_l2_sensor{nullptr};
  sensor::Sensor *storage_power_l3_sensor{nullptr};
  sensor::Sensor *storage_power_aggregate_sensor{nullptr};
  sensor::Sensor *storage_soc_sensor{nullptr};
  sensor::Sensor *storage_energy_sensor{nullptr};
};

inline void configure_site(ConfiguredSite *site, uint8_t phases, float voltage) {
  if (site == nullptr)
    return;
  site->limits.phases = phases == 3 ? 3 : 1;
  site->limits.voltage = voltage;
}

}  // namespace esphome::ocpp
