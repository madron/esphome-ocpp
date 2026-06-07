#pragma once

#include "grid.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace esphome::ocpp {

enum class SiteEnergyPolicy : uint8_t { NORMAL, SOLAR };

struct SiteLimitConfig {
  uint8_t phases{1};
  float voltage{230.0f};
  std::optional<float> grid_max_power{};
  std::optional<float> grid_max_phase_imbalance{};
  std::optional<float> grid_max_current{};
  SiteEnergyPolicy energy_policy{SiteEnergyPolicy::NORMAL};
  float solar_export_margin_power{300.0f};
  std::optional<float> storage_capacity_kwh{};
};

struct SitePowerMeasurements {
  std::optional<float> grid_power_l1{};
  std::optional<float> grid_power_l2{};
  std::optional<float> grid_power_l3{};
  std::optional<float> grid_power_aggregate{};
  std::optional<float> storage_power_l1{};
  std::optional<float> storage_power_l2{};
  std::optional<float> storage_power_l3{};
  std::optional<float> storage_power_aggregate{};
  std::optional<float> storage_soc{};
  std::optional<float> storage_energy_kwh{};
};

inline uint8_t site_active_phases(const SiteLimitConfig &config) { return config.phases == 3 ? 3 : 1; }

inline GridLimitConfig site_grid_limit_config(const SiteLimitConfig &config) {
  return GridLimitConfig{config.phases, config.voltage, config.grid_max_power, config.grid_max_phase_imbalance,
                         config.grid_max_current};
}

inline GridPowerMeasurements site_grid_power_measurements(const SitePowerMeasurements &measurements) {
  return GridPowerMeasurements{measurements.grid_power_l1, measurements.grid_power_l2, measurements.grid_power_l3,
                               measurements.grid_power_aggregate};
}

inline bool site_has_grid_power_measurement(const SiteLimitConfig &config, const SitePowerMeasurements &measurements) {
  const uint8_t active_phases = site_active_phases(config);
  if (active_phases == 3)
    return (measurements.grid_power_l1.has_value() && measurements.grid_power_l2.has_value() &&
            measurements.grid_power_l3.has_value()) ||
           measurements.grid_power_aggregate.has_value();
  return measurements.grid_power_l1.has_value() || measurements.grid_power_aggregate.has_value();
}

inline bool site_has_storage_power_measurement(const SiteLimitConfig &config, const SitePowerMeasurements &measurements) {
  const uint8_t active_phases = site_active_phases(config);
  if (active_phases == 3)
    return (measurements.storage_power_l1.has_value() && measurements.storage_power_l2.has_value() &&
            measurements.storage_power_l3.has_value()) ||
           measurements.storage_power_aggregate.has_value();
  return measurements.storage_power_l1.has_value() || measurements.storage_power_aggregate.has_value();
}

inline std::array<float, 3> site_storage_phase_power(const SiteLimitConfig &config,
                                                     const SitePowerMeasurements &measurements) {
  const uint8_t active_phases = site_active_phases(config);
  std::array<float, 3> power{};
  if (active_phases == 3 && measurements.storage_power_l1.has_value() && measurements.storage_power_l2.has_value() &&
      measurements.storage_power_l3.has_value()) {
    power[0] = measurements.storage_power_l1.value();
    power[1] = measurements.storage_power_l2.value();
    power[2] = measurements.storage_power_l3.value();
  } else if (active_phases == 1 && measurements.storage_power_l1.has_value()) {
    power[0] = measurements.storage_power_l1.value();
  } else if (measurements.storage_power_aggregate.has_value()) {
    const float estimated_phase_power = grid_divide_or_zero(measurements.storage_power_aggregate.value(), active_phases);
    for (uint8_t i = 0; i < active_phases; i++)
      power[i] = estimated_phase_power;
  }
  return power;
}

inline void normalize_site_storage_state(const SiteLimitConfig &config, SitePowerMeasurements *measurements) {
  if (measurements == nullptr || !config.storage_capacity_kwh.has_value() || config.storage_capacity_kwh.value() <= 0.0f)
    return;
  const float capacity = config.storage_capacity_kwh.value();
  if (measurements->storage_energy_kwh.has_value() && !measurements->storage_soc.has_value())
    measurements->storage_soc = measurements->storage_energy_kwh.value() * 100.0f / capacity;
  if (measurements->storage_soc.has_value() && !measurements->storage_energy_kwh.has_value())
    measurements->storage_energy_kwh = capacity * measurements->storage_soc.value() / 100.0f;
}

inline float site_solar_surplus_current_for_load(const SiteLimitConfig &config,
                                                 const SitePowerMeasurements &measurements,
                                                 const std::array<bool, 3> &used_phases) {
  const uint8_t active_phases = site_active_phases(config);
  uint8_t used_phase_count = 0;
  for (uint8_t i = 0; i < active_phases; i++) {
    if (used_phases[i])
      used_phase_count++;
  }
  if (used_phase_count == 0 || config.voltage <= 0.0f)
    return 0.0f;
  if (!site_has_grid_power_measurement(config, measurements) && !site_has_storage_power_measurement(config, measurements))
    return 0.0f;

  const auto grid_power = grid_phase_power(site_grid_limit_config(config), site_grid_power_measurements(measurements));
  const auto storage_power = site_storage_phase_power(config, measurements);
  const float margin_per_used_phase = grid_divide_or_zero(config.solar_export_margin_power, used_phase_count);
  float headroom_power = std::numeric_limits<float>::infinity();

  for (uint8_t i = 0; i < active_phases; i++) {
    if (!used_phases[i])
      continue;
    const float storage_discharge_power = grid_clamp_non_negative(storage_power[i]);
    const float phase_headroom_power = -grid_power[i] - margin_per_used_phase - storage_discharge_power;
    headroom_power = std::min(headroom_power, phase_headroom_power);
  }
  return std::isfinite(headroom_power) ? headroom_power / config.voltage : 0.0f;
}

inline float site_available_current_for_load(const SiteLimitConfig &config, const SitePowerMeasurements &measurements,
                                             const std::array<bool, 3> &used_phases) {
  const float grid_headroom = grid_headroom_current_for_load(site_grid_limit_config(config),
                                                            site_grid_power_measurements(measurements), used_phases);
  if (config.energy_policy != SiteEnergyPolicy::SOLAR)
    return grid_headroom;

  const float solar_headroom = site_solar_surplus_current_for_load(config, measurements, used_phases);
  if (!std::isfinite(grid_headroom))
    return solar_headroom;
  if (!std::isfinite(solar_headroom))
    return grid_headroom;
  return std::min(grid_headroom, solar_headroom);
}

inline std::vector<float> site_phase_power(const SiteLimitConfig &config, const SitePowerMeasurements &measurements) {
  const uint8_t active_phases = site_active_phases(config);
  std::vector<float> power(active_phases, 0.0f);
  if (active_phases == 3 && measurements.grid_power_l1.has_value() && measurements.grid_power_l2.has_value() &&
      measurements.grid_power_l3.has_value()) {
    power[0] = measurements.grid_power_l1.value();
    power[1] = measurements.grid_power_l2.value();
    power[2] = measurements.grid_power_l3.value();
  } else if (active_phases == 1 && measurements.grid_power_l1.has_value()) {
    power[0] = measurements.grid_power_l1.value();
  } else if (measurements.grid_power_aggregate.has_value()) {
    const float estimated_phase_power = grid_divide_or_zero(measurements.grid_power_aggregate.value(), active_phases);
    for (auto &phase_power : power)
      phase_power = estimated_phase_power;
  }
  return power;
}

inline std::vector<float> site_headroom_current_per_phase(const SiteLimitConfig &config,
                                                          const SitePowerMeasurements &measurements = {}) {
  const uint8_t active_phases = site_active_phases(config);
  std::vector<float> current(active_phases, 0.0f);
  if (config.energy_policy != SiteEnergyPolicy::SOLAR) {
    const auto headroom_current = grid_headroom_current(site_grid_limit_config(config),
                                                        site_grid_power_measurements(measurements));
    for (uint8_t i = 0; i < active_phases; i++)
      current[i] = headroom_current[i];
    return current;
  }
  for (uint8_t i = 0; i < active_phases; i++) {
    std::array<bool, 3> used_phases{};
    used_phases[i] = true;
    current[i] = site_available_current_for_load(config, measurements, used_phases);
  }
  return current;
}

inline std::vector<float> example_site_spare_current_per_phase(const SiteLimitConfig &config,
                                                               const SitePowerMeasurements &measurements = {}) {
  return site_headroom_current_per_phase(config, measurements);
}

}  // namespace esphome::ocpp
