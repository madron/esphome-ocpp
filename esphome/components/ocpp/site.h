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
  sensor::Sensor *grid_headroom_current_sensor{nullptr};
  std::array<sensor::Sensor *, 3> grid_headroom_current_sensors{};
  std::array<float, 3> latest_grid_headroom_current{};
  sensor::Sensor *headroom_current_sensor{nullptr};
  std::array<sensor::Sensor *, 3> headroom_current_sensors{};
  std::array<float, 3> latest_headroom_current{};
  sensor::Sensor *drawn_current_sensor{nullptr};
  std::array<sensor::Sensor *, 3> drawn_current_sensors{};
  std::array<float, 3> latest_drawn_current{};
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

inline float site_drawn_current_max(const std::array<float, 3> &drawn_current) {
  return std::max(drawn_current[0], std::max(drawn_current[1], drawn_current[2]));
}

inline float site_current_max(const std::array<float, 3> &current, uint8_t phases) {
  const uint8_t active_phases = phases == 3 ? 3 : 1;
  float value = current[0];
  for (uint8_t i = 1; i < active_phases; i++)
    value = std::max(value, current[i]);
  return value;
}

inline bool update_grid_headroom_current(ConfiguredSite *site, const SitePowerMeasurements &measurements = {}) {
  if (site == nullptr)
    return false;

  const auto headroom_current = grid_headroom_current(site_grid_limit_config(site->limits),
                                                     site_grid_power_measurements(measurements));
  const bool changed = site_current_changed(site->latest_grid_headroom_current, headroom_current);
  site->latest_grid_headroom_current = headroom_current;
  return changed;
}

inline bool update_site_headroom_current(ConfiguredSite *site, const SitePowerMeasurements &measurements = {}) {
  if (site == nullptr)
    return false;

  std::array<float, 3> headroom_current{};
  if (site->limits.energy_policy != SiteEnergyPolicy::SOLAR) {
    headroom_current = site->latest_grid_headroom_current;
    const bool changed = site_current_changed(site->latest_headroom_current, headroom_current);
    site->latest_headroom_current = headroom_current;
    return changed;
  }
  const uint8_t active_phases = site->limits.phases == 3 ? 3 : 1;
  for (uint8_t i = 0; i < active_phases; i++) {
    std::array<bool, 3> used_phases{};
    used_phases[i] = true;
    headroom_current[i] = site_available_current_for_load(site->limits, measurements, used_phases);
  }
  const bool changed = site_current_changed(site->latest_headroom_current, headroom_current);
  site->latest_headroom_current = headroom_current;
  return changed;
}

inline std::array<float, 3> site_drawn_current_from_charger(const ConfiguredCharger &charger) {
  std::array<float, 3> site_drawn_current{};
  const uint8_t active_phases = charger.phases == 3 ? 3 : 1;
  for (uint8_t charger_phase = 0; charger_phase < active_phases; charger_phase++) {
    const uint8_t site_phase = charger.phase_mapping[charger_phase];
    if (site_phase < site_drawn_current.size())
      site_drawn_current[site_phase] += charger.latest_drawn_current[charger_phase];
  }
  return site_drawn_current;
}

inline bool update_site_drawn_current(ConfiguredSite *site, const ConfiguredCharger *charger) {
  if (site == nullptr)
    return false;

  std::array<float, 3> drawn_current{};
  if (charger != nullptr) {
    const auto charger_drawn_current = site_drawn_current_from_charger(*charger);
    for (uint8_t i = 0; i < drawn_current.size(); i++)
      drawn_current[i] += charger_drawn_current[i];
  }

  const bool changed = site_current_changed(site->latest_drawn_current, drawn_current);
  site->latest_drawn_current = drawn_current;
  return changed;
}

inline void publish_grid_headroom_current_if_configured(ConfiguredSite *site) {
#ifdef USE_OCPP
  if (site == nullptr)
    return;
  if (site->grid_headroom_current_sensor != nullptr)
    site->grid_headroom_current_sensor->publish_state(site_current_max(site->latest_grid_headroom_current,
                                                                       site->limits.phases));
  const uint8_t active_phases = site->limits.phases == 3 ? 3 : 1;
  for (uint8_t i = 0; i < active_phases; i++) {
    if (site->grid_headroom_current_sensors[i] != nullptr)
      site->grid_headroom_current_sensors[i]->publish_state(site->latest_grid_headroom_current[i]);
  }
#else
  (void) site;
#endif
}

inline void publish_site_headroom_current_if_configured(ConfiguredSite *site) {
#ifdef USE_OCPP
  if (site == nullptr)
    return;
  if (site->headroom_current_sensor != nullptr)
    site->headroom_current_sensor->publish_state(site_current_max(site->latest_headroom_current, site->limits.phases));
  const uint8_t active_phases = site->limits.phases == 3 ? 3 : 1;
  for (uint8_t i = 0; i < active_phases; i++) {
    if (site->headroom_current_sensors[i] != nullptr)
      site->headroom_current_sensors[i]->publish_state(site->latest_headroom_current[i]);
  }
#else
  (void) site;
#endif
}

inline void publish_site_drawn_current_if_configured(ConfiguredSite *site) {
#ifdef USE_OCPP
  if (site == nullptr)
    return;
  if (site->drawn_current_sensor != nullptr)
    site->drawn_current_sensor->publish_state(site_drawn_current_max(site->latest_drawn_current));
  const uint8_t active_phases = site->limits.phases == 3 ? 3 : 1;
  for (uint8_t i = 0; i < active_phases; i++) {
    if (site->drawn_current_sensors[i] != nullptr)
      site->drawn_current_sensors[i]->publish_state(site->latest_drawn_current[i]);
  }
#else
  (void) site;
#endif
}

}  // namespace esphome::ocpp
