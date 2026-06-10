#pragma once

#include "charger.h"
#include "site_limits.h"

#ifdef USE_OCPP
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#endif

#include <cstdint>

namespace esphome::ocpp {

class OcppServer;
class OcppSolarExportMarginNumber;

#ifdef USE_OCPP
class OcppSolarExportMarginNumber : public number::Number {
 public:
  void set_parent(OcppServer *parent) { this->parent_ = parent; }

 protected:
  void control(float value) override;

  OcppServer *parent_{nullptr};
};
#endif

struct ConfiguredSite {
  SiteLimitConfig limits;
  sensor::Sensor *grid_power_l1_sensor{nullptr};
  sensor::Sensor *grid_power_l2_sensor{nullptr};
  sensor::Sensor *grid_power_l3_sensor{nullptr};
  sensor::Sensor *grid_power_aggregate_sensor{nullptr};
  sensor::Sensor *storage_power_l1_sensor{nullptr};
  sensor::Sensor *storage_power_l2_sensor{nullptr};
  sensor::Sensor *storage_power_l3_sensor{nullptr};
  sensor::Sensor *storage_power_aggregate_sensor{nullptr};
  sensor::Sensor *storage_soc_sensor{nullptr};
  sensor::Sensor *storage_energy_sensor{nullptr};
  OcppSolarExportMarginNumber *solar_export_margin_power_number{nullptr};
};

inline void configure_site(ConfiguredSite *site, uint8_t phases, float voltage) {
  if (site == nullptr)
    return;
  site->limits.phases = phases == 3 ? 3 : 1;
  site->limits.voltage = voltage;
}

}  // namespace esphome::ocpp
