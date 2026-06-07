#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace esphome::ocpp {

struct GridLimitConfig {
  uint8_t phases{1};
  float voltage{230.0f};
  std::optional<float> max_power{};
  std::optional<float> max_phase_imbalance{};
  std::optional<float> max_current{};
};

struct GridPowerMeasurements {
  std::optional<float> power_l1{};
  std::optional<float> power_l2{};
  std::optional<float> power_l3{};
  std::optional<float> power_aggregate{};
};

inline float grid_clamp_non_negative(float value) { return value > 0.0f ? value : 0.0f; }

inline float grid_divide_or_zero(float numerator, float denominator) {
  return denominator > 0.0f ? numerator / denominator : 0.0f;
}

inline uint8_t grid_active_phases(const GridLimitConfig &config) { return config.phases == 3 ? 3 : 1; }

inline std::array<float, 3> grid_phase_power(const GridLimitConfig &config, const GridPowerMeasurements &measurements) {
  const uint8_t active_phases = grid_active_phases(config);
  std::array<float, 3> power{};
  if (active_phases == 3 && measurements.power_l1.has_value() && measurements.power_l2.has_value() &&
      measurements.power_l3.has_value()) {
    power[0] = measurements.power_l1.value();
    power[1] = measurements.power_l2.value();
    power[2] = measurements.power_l3.value();
  } else if (active_phases == 1 && measurements.power_l1.has_value()) {
    power[0] = measurements.power_l1.value();
  } else if (measurements.power_aggregate.has_value()) {
    const float estimated_phase_power = grid_divide_or_zero(measurements.power_aggregate.value(), active_phases);
    for (uint8_t i = 0; i < active_phases; i++)
      power[i] = estimated_phase_power;
  }
  return power;
}

inline float grid_current_max(const std::array<float, 3> &current) {
  return std::max(current[0], std::max(current[1], current[2]));
}

inline float grid_headroom_current_for_load(const GridLimitConfig &config, const GridPowerMeasurements &measurements,
                                            const std::array<bool, 3> &used_phases) {
  const uint8_t active_phases = grid_active_phases(config);
  uint8_t used_phase_count = 0;
  for (uint8_t i = 0; i < active_phases; i++) {
    if (used_phases[i])
      used_phase_count++;
  }
  if (used_phase_count == 0)
    return 0.0f;
  if (config.voltage <= 0.0f)
    return 0.0f;

  const auto power = grid_phase_power(config, measurements);
  float headroom_current = std::numeric_limits<float>::infinity();

  if (config.max_power.has_value()) {
    float total_power = 0.0f;
    for (uint8_t i = 0; i < active_phases; i++)
      total_power += power[i];
    const float remaining_power = grid_clamp_non_negative(config.max_power.value() - total_power);
    headroom_current = std::min(headroom_current, remaining_power / (config.voltage * used_phase_count));
  }

  if (config.max_current.has_value()) {
    for (uint8_t i = 0; i < active_phases; i++) {
      if (!used_phases[i])
        continue;
      const float used_current = power[i] / config.voltage;
      headroom_current = std::min(headroom_current, grid_clamp_non_negative(config.max_current.value() - used_current));
    }
  }

  if (active_phases == 3 && config.max_phase_imbalance.has_value()) {
    const float max_phase_imbalance = config.max_phase_imbalance.value();
    const float min_power = std::min(power[0], std::min(power[1], power[2]));
    const float max_power = std::max(power[0], std::max(power[1], power[2]));
    if (max_power - min_power > max_phase_imbalance)
      return 0.0f;

    float headroom_power = std::numeric_limits<float>::infinity();
    for (uint8_t high_phase = 0; high_phase < active_phases; high_phase++) {
      for (uint8_t low_phase = 0; low_phase < active_phases; low_phase++) {
        const float power_difference = power[high_phase] - power[low_phase];
        if (used_phases[high_phase] == used_phases[low_phase]) {
          if (power_difference > max_phase_imbalance)
            return 0.0f;
          continue;
        }
        if (used_phases[high_phase]) {
          headroom_power = std::min(headroom_power,
                                    grid_clamp_non_negative(max_phase_imbalance - power_difference));
        }
      }
    }
    headroom_current = std::min(headroom_current, headroom_power / config.voltage);
  }

  return headroom_current;
}

// Grid headroom is pure grid/source headroom from grid limits and signed grid power.
// Site headroom currently mirrors this value; future site logic can combine grid, storage, and other sources.
inline std::array<float, 3> grid_headroom_current(const GridLimitConfig &config,
                                                 const GridPowerMeasurements &measurements = {}) {
  const uint8_t active_phases = grid_active_phases(config);
  std::array<float, 3> headroom_current{};
  for (uint8_t i = 0; i < active_phases; i++)
    headroom_current[i] = std::numeric_limits<float>::infinity();
  if (config.voltage <= 0.0f) {
    for (uint8_t i = 0; i < active_phases; i++)
      headroom_current[i] = 0.0f;
    return headroom_current;
  }

  const auto power = grid_phase_power(config, measurements);

  if (config.max_power.has_value()) {
    float total_power = 0.0f;
    for (uint8_t i = 0; i < active_phases; i++)
      total_power += power[i];
    const float remaining_power = grid_clamp_non_negative(config.max_power.value() - total_power);
    const float balanced_headroom_current = remaining_power / (config.voltage * active_phases);
    for (uint8_t i = 0; i < active_phases; i++)
      headroom_current[i] = std::min(headroom_current[i], balanced_headroom_current);
  }

  if (config.max_current.has_value()) {
    for (uint8_t i = 0; i < active_phases; i++) {
      const float used_current = power[i] / config.voltage;
      const float phase_headroom_current = grid_clamp_non_negative(config.max_current.value() - used_current);
      headroom_current[i] = std::min(headroom_current[i], phase_headroom_current);
    }
  }

  if (active_phases == 3 && config.max_phase_imbalance.has_value()) {
    const float min_power = std::min(power[0], std::min(power[1], power[2]));
    const float max_power = std::max(power[0], std::max(power[1], power[2]));
    if (max_power - min_power > config.max_phase_imbalance.value()) {
      for (uint8_t i = 0; i < active_phases; i++)
        headroom_current[i] = std::min(headroom_current[i], 0.0f);
      return headroom_current;
    }
    for (uint8_t i = 0; i < active_phases; i++) {
      const float headroom_power = grid_clamp_non_negative(config.max_phase_imbalance.value() - (power[i] - min_power));
      headroom_current[i] = std::min(headroom_current[i], headroom_power / config.voltage);
    }
  }

  return headroom_current;
}

}  // namespace esphome::ocpp
