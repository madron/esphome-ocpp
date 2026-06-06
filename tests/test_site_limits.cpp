#include "esphome/components/ocpp/site_limits.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using esphome::ocpp::SiteLimitConfig;
using esphome::ocpp::SitePowerMeasurements;
using esphome::ocpp::example_site_spare_current_per_phase;

template<typename T> std::ostream &operator<<(std::ostream &out, const std::vector<T> &values) {
  out << "{";
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0)
      out << ", ";
    if (std::isinf(values[i]))
      out << "inf";
    else
      out << values[i];
  }
  out << "}";
  return out;
}

template<typename T> void assert_equal(const char *description, const T &actual, const T &expected) {
  if (actual == expected)
    return;
  std::cerr << description << "\n";
  std::cerr << "Expected: " << expected << "\n";
  std::cerr << "Actual:   " << actual << "\n";
  std::abort();
}

int main() {

  // no_configured_grid_limits
  auto result = example_site_spare_current_per_phase(SiteLimitConfig{});
  assert_equal("no_configured_grid_limits", result, std::vector<float>{std::numeric_limits<float>::infinity()});

  // single_phase_max_power
  SiteLimitConfig config;
  config.phases = 1;
  config.voltage = 100.0f;
  config.grid_max_power = 6000.0f;
  result = example_site_spare_current_per_phase(config);
  assert_equal("single_phase_max_power", result, std::vector<float>{60.0f});

  // single_phase_dynamic_load
  SitePowerMeasurements measurements;
  measurements.grid_power_l1 = 1400.0f;
  result = example_site_spare_current_per_phase(config, measurements);
  assert_equal("single_phase_dynamic_load", result, std::vector<float>{46.0f});

  // three_phase_total_power
  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_power = 9000.0f;
  result = example_site_spare_current_per_phase(config);
  assert_equal("three_phase_total_power", result, std::vector<float>{30.0f, 30.0f, 30.0f});

  // tightest_static_limit
  config.grid_max_current = 32.0f;
  config.grid_max_phase_imbalance = 6000.0f;
  result = example_site_spare_current_per_phase(config);
  assert_equal("tightest_static_limit", result, std::vector<float>{30.0f, 30.0f, 30.0f});

  // per_phase_current_headroom
  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_current = 32.0f;
  measurements = SitePowerMeasurements{};
  measurements.grid_power_l1 = 2000.0f;
  measurements.grid_power_l2 = 1000.0f;
  measurements.grid_power_l3 = 0.0f;
  result = example_site_spare_current_per_phase(config, measurements);
  assert_equal("per_phase_current_headroom", result, std::vector<float>{12.0f, 22.0f, 32.0f});

  // remaining_phase_imbalance
  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_phase_imbalance = 6000.0f;
  measurements = SitePowerMeasurements{};
  measurements.grid_power_l1 = 5000.0f;
  measurements.grid_power_l2 = 1000.0f;
  measurements.grid_power_l3 = 1000.0f;
  result = example_site_spare_current_per_phase(config, measurements);
  assert_equal("remaining_phase_imbalance", result, std::vector<float>{20.0f, 60.0f, 60.0f});

  // existing_imbalance_violation
  measurements.grid_power_l1 = 8000.0f;
  result = example_site_spare_current_per_phase(config, measurements);
  assert_equal("existing_imbalance_violation", result, std::vector<float>{0.0f, 0.0f, 0.0f});

  // aggregate_measurement_fallback
  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_power = 9000.0f;
  measurements = SitePowerMeasurements{};
  measurements.grid_power_aggregate = 3000.0f;
  result = example_site_spare_current_per_phase(config, measurements);
  assert_equal("aggregate_measurement_fallback", result, std::vector<float>{20.0f, 20.0f, 20.0f});

  return 0;
}
