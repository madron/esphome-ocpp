#pragma once

#include "grid.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace esphome::ocpp {

struct SiteLimitConfig {
  uint8_t phases{1};
  float voltage{230.0f};
  std::optional<float> grid_max_power{};
  std::optional<float> grid_max_phase_imbalance{};
  std::optional<float> grid_max_current{};
};

struct SitePowerMeasurements {
  std::optional<float> grid_power_l1{};
  std::optional<float> grid_power_l2{};
  std::optional<float> grid_power_l3{};
  std::optional<float> grid_power_aggregate{};
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
  const auto headroom_current = grid_headroom_current(site_grid_limit_config(config),
                                                      site_grid_power_measurements(measurements));
  for (uint8_t i = 0; i < active_phases; i++)
    current[i] = headroom_current[i];
  return current;
}

inline std::vector<float> example_site_spare_current_per_phase(const SiteLimitConfig &config,
                                                               const SitePowerMeasurements &measurements = {}) {
  return site_headroom_current_per_phase(config, measurements);
}

}  // namespace esphome::ocpp
