#include "esphome/components/ocpp/grid.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using esphome::ocpp::GridLimitConfig;
using esphome::ocpp::GridPowerMeasurements;
using esphome::ocpp::grid_current_max;
using esphome::ocpp::grid_headroom_current;

template<typename T> void assert_equal(const char *description, const T &actual, const T &expected) {
  if (actual == expected)
    return;
  std::cerr << description << "\n";
  std::cerr << "Expected: " << expected << "\n";
  std::cerr << "Actual:   " << actual << "\n";
  std::abort();
}

int main() {
  GridLimitConfig config;
  config.phases = 3;
  config.voltage = 100.0f;
  config.max_power = 9000.0f;
  GridPowerMeasurements measurements;
  measurements.power_l1 = -1000.0f;
  measurements.power_l2 = -1000.0f;
  measurements.power_l3 = -1000.0f;
  auto headroom_current = grid_headroom_current(config, measurements);
  assert_equal("negative_grid_power_increases_headroom_l1", headroom_current[0], 40.0f);
  assert_equal("negative_grid_power_increases_headroom_l2", headroom_current[1], 40.0f);
  assert_equal("negative_grid_power_increases_headroom_l3", headroom_current[2], 40.0f);
  assert_equal("grid_headroom_current_max_uses_highest_phase", grid_current_max(headroom_current), 40.0f);

  config.max_current = 32.0f;
  measurements.power_l1 = 2000.0f;
  measurements.power_l2 = 1000.0f;
  measurements.power_l3 = 0.0f;
  headroom_current = grid_headroom_current(config, measurements);
  assert_equal("grid_headroom_applies_l1_current_limit", headroom_current[0], 12.0f);
  assert_equal("grid_headroom_applies_total_power_limit_l2", headroom_current[1], 20.0f);
  assert_equal("grid_headroom_applies_total_power_limit_l3", headroom_current[2], 20.0f);

  return 0;
}