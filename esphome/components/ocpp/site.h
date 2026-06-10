#pragma once

#include "charger.h"
#include "site_limits.h"

#include <cstdint>

namespace esphome::ocpp {

class OcppComponent;

struct ConfiguredSite {
  SiteLimitConfig limits;
};

inline void configure_site(ConfiguredSite *site, uint8_t phases, float voltage) {
  if (site == nullptr)
    return;
  site->limits.phases = phases == 3 ? 3 : 1;
  site->limits.voltage = voltage;
}

}  // namespace esphome::ocpp
