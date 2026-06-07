#pragma once

#include "charger.h"
#include "site_limits.h"

#ifdef USE_OCPP
#include "esphome/components/sensor/sensor.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>

namespace esphome::ocpp {

struct ConfiguredSite {
  SiteLimitConfig limits;
  sensor::Sensor *grid_power_l1_sensor{nullptr};
  sensor::Sensor *grid_power_l2_sensor{nullptr};
  sensor::Sensor *grid_power_l3_sensor{nullptr};
  sensor::Sensor *grid_power_aggregate_sensor{nullptr};
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

inline bool update_grid_headroom_current(ConfiguredSite *site, const SitePowerMeasurements &measurements = {}) {
  if (site == nullptr)
    return false;

  const auto headroom_current = grid_headroom_current(site_grid_limit_config(site->limits),
                                                     site_grid_power_measurements(measurements));
  const bool changed = site_current_changed(site->latest_grid_headroom_current, headroom_current);
  site->latest_grid_headroom_current = headroom_current;
  return changed;
}

inline bool update_site_headroom_current(ConfiguredSite *site) {
  if (site == nullptr)
    return false;

  // Site headroom currently mirrors grid headroom. Future site logic can combine grid, storage, and other sources here.
  const auto headroom_current = site->latest_grid_headroom_current;
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
    site->grid_headroom_current_sensor->publish_state(grid_current_max(site->latest_grid_headroom_current));
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
    site->headroom_current_sensor->publish_state(grid_current_max(site->latest_headroom_current));
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
