#pragma once

#include "charger.h"
#include "site_limits.h"

#ifdef USE_OCPP
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#endif

#include <algorithm>
#include <array>
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
  sensor::Sensor *used_current_sensor{nullptr};
  std::array<sensor::Sensor *, 3> used_current_sensors{};
  std::array<float, 3> latest_used_current{};
};

inline bool site_current_changed(const std::array<float, 3> &a, const std::array<float, 3> &b) {
  for (uint8_t i = 0; i < a.size(); i++) {
    if (a[i] != b[i])
      return true;
  }
  return false;
}

inline void configure_site(ConfiguredSite *site, uint8_t phases, float voltage) {
  if (site == nullptr)
    return;
  site->limits.phases = phases == 3 ? 3 : 1;
  site->limits.voltage = voltage;
}

inline float site_used_current_max(const std::array<float, 3> &used_current) {
  return std::max(used_current[0], std::max(used_current[1], used_current[2]));
}

inline std::array<float, 3> site_used_current_from_charger(const ConfiguredCharger &charger) {
  std::array<float, 3> site_used_current{};
  const uint8_t active_phases = charger.phases == 3 ? 3 : 1;
  for (uint8_t charger_phase = 0; charger_phase < active_phases; charger_phase++) {
    const uint8_t site_phase = charger.phase_mapping[charger_phase];
    if (site_phase < site_used_current.size())
      site_used_current[site_phase] += charger.latest_used_current[charger_phase];
  }
  return site_used_current;
}

inline bool update_site_used_current(ConfiguredSite *site, const ConfiguredCharger *charger) {
  if (site == nullptr)
    return false;

  std::array<float, 3> used_current{};
  if (charger != nullptr) {
    const auto charger_used_current = site_used_current_from_charger(*charger);
    for (uint8_t i = 0; i < used_current.size(); i++)
      used_current[i] += charger_used_current[i];
  }

  const bool changed = site_current_changed(site->latest_used_current, used_current);
  site->latest_used_current = used_current;
  return changed;
}

inline void publish_site_used_current_if_configured(ConfiguredSite *site) {
#ifdef USE_OCPP
  if (site == nullptr)
    return;
  if (site->used_current_sensor != nullptr)
    site->used_current_sensor->publish_state(site_used_current_max(site->latest_used_current));
  const uint8_t active_phases = site->limits.phases == 3 ? 3 : 1;
  for (uint8_t i = 0; i < active_phases; i++) {
    if (site->used_current_sensors[i] != nullptr)
      site->used_current_sensors[i]->publish_state(site->latest_used_current[i]);
  }
#else
  (void) site;
#endif
}

}  // namespace esphome::ocpp
