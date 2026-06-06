#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

struct ConnectorCurrentState {
  bool is_charging{false};
  bool has_measured_drawn_current{false};
  float measured_drawn_current{0.0f};
  float allocated_current{0.0f};
};

inline float clamp_non_negative(float value) { return value > 0.0f ? value : 0.0f; }

inline float clamp_finite_non_negative(float value) {
  return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

inline float divide_or_zero(float numerator, float denominator) {
  return denominator > 0.0f ? numerator / denominator : 0.0f;
}

inline uint8_t site_active_phases(const SiteLimitConfig &config) { return config.phases == 3 ? 3 : 1; }

inline float effective_allocated_current(float available_current, float max_current, float requested_current,
                                        float min_current, bool enabled) {
  if (!enabled)
    return 0.0f;
  const float effective_current = std::min({clamp_finite_non_negative(available_current),
                                           clamp_finite_non_negative(max_current),
                                           clamp_finite_non_negative(requested_current)});
  if (min_current > 0.0f && effective_current < min_current)
    return 0.0f;
  return effective_current;
}

inline float effective_allocated_current(float available_current, float min_current) {
  return effective_allocated_current(available_current, available_current, available_current, min_current, true);
}

inline float effective_connector_drawn_current(const ConnectorCurrentState &state) {
  if (!state.is_charging)
    return 0.0f;
  if (state.has_measured_drawn_current)
    return clamp_finite_non_negative(state.measured_drawn_current);
  return clamp_finite_non_negative(state.allocated_current);
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
    const float estimated_phase_power = divide_or_zero(measurements.grid_power_aggregate.value(), active_phases);
    for (auto &phase_power : power)
      phase_power = estimated_phase_power;
  }
  return power;
}

// NOTE: This helper is only a small, testable example for automatic tests. Do not treat this specific spare-current
// calculation as the project direction. The part worth preserving is the separation between static config
// (`SiteLimitConfig`) and dynamic state (`SitePowerMeasurements`), plus the C++ test infrastructure around it.
inline std::vector<float> example_site_spare_current_per_phase(const SiteLimitConfig &config,
                                                               const SitePowerMeasurements &measurements = {}) {
  const uint8_t active_phases = site_active_phases(config);
  std::vector<float> spare_current(active_phases, std::numeric_limits<float>::infinity());
  if (config.voltage <= 0.0f)
    return std::vector<float>(active_phases, 0.0f);

  const auto power = site_phase_power(config, measurements);

  if (config.grid_max_power.has_value()) {
    float total_power = 0.0f;
    for (uint8_t i = 0; i < active_phases; i++)
      total_power += power[i];
    const float remaining_power = clamp_non_negative(config.grid_max_power.value() - total_power);
    const float balanced_spare_current = remaining_power / (config.voltage * active_phases);
    for (auto &phase_spare_current : spare_current)
      phase_spare_current = std::min(phase_spare_current, balanced_spare_current);
  }

  if (config.grid_max_current.has_value()) {
    for (uint8_t i = 0; i < active_phases; i++) {
      const float used_current = power[i] / config.voltage;
      const float phase_spare_current = clamp_non_negative(config.grid_max_current.value() - used_current);
      spare_current[i] = std::min(spare_current[i], phase_spare_current);
    }
  }

  if (active_phases == 3 && config.grid_max_phase_imbalance.has_value()) {
    const auto minmax = std::minmax_element(power.begin(), power.end());
    const float min_power = *minmax.first;
    const float max_power = *minmax.second;
    if (max_power - min_power > config.grid_max_phase_imbalance.value()) {
      for (auto &phase_spare_current : spare_current)
        phase_spare_current = std::min(phase_spare_current, 0.0f);
      return spare_current;
    }
    for (uint8_t i = 0; i < active_phases; i++) {
      const float spare_power = clamp_non_negative(config.grid_max_phase_imbalance.value() - (power[i] - min_power));
      spare_current[i] = std::min(spare_current[i], spare_power / config.voltage);
    }
  }

  return spare_current;
}

}  // namespace esphome::ocpp
