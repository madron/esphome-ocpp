import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


CPP_SOURCE = r'''
#include "esphome/components/ocpp/site_limits.h"

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using esphome::ocpp::SiteLimitConfig;
using esphome::ocpp::SitePowerMeasurements;
using esphome::ocpp::get_site_spare_current_per_phase;

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
  auto result = get_site_spare_current_per_phase(SiteLimitConfig{});
  assert_equal("no_configured_grid_limits", result, std::vector<float>{std::numeric_limits<float>::infinity()});

  SiteLimitConfig config;
  config.phases = 1;
  config.voltage = 100.0f;
  config.grid_max_power = 6000.0f;
  result = get_site_spare_current_per_phase(config);
  assert_equal("single_phase_max_power", result, std::vector<float>{60.0f});

  SitePowerMeasurements measurements;
  measurements.grid_power_l1 = 1400.0f;
  result = get_site_spare_current_per_phase(config, measurements);
  assert_equal("single_phase_dynamic_load", result, std::vector<float>{46.0f});

  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_power = 9000.0f;
  result = get_site_spare_current_per_phase(config);
  assert_equal("three_phase_total_power", result, std::vector<float>{30.0f, 30.0f, 30.0f});

  config.grid_max_current_per_phase = 32.0f;
  config.grid_max_phase_imbalance = 6000.0f;
  result = get_site_spare_current_per_phase(config);
  assert_equal("tightest_static_limit", result, std::vector<float>{30.0f, 30.0f, 30.0f});

  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_current_per_phase = 32.0f;
  measurements = SitePowerMeasurements{};
  measurements.grid_power_l1 = 2000.0f;
  measurements.grid_power_l2 = 1000.0f;
  measurements.grid_power_l3 = 0.0f;
  result = get_site_spare_current_per_phase(config, measurements);
  assert_equal("per_phase_current_headroom", result, std::vector<float>{12.0f, 22.0f, 32.0f});

  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_phase_imbalance = 6000.0f;
  measurements = SitePowerMeasurements{};
  measurements.grid_power_l1 = 5000.0f;
  measurements.grid_power_l2 = 1000.0f;
  measurements.grid_power_l3 = 1000.0f;
  result = get_site_spare_current_per_phase(config, measurements);
  assert_equal("remaining_phase_imbalance", result, std::vector<float>{20.0f, 60.0f, 60.0f});

  measurements.grid_power_l1 = 8000.0f;
  result = get_site_spare_current_per_phase(config, measurements);
  assert_equal("existing_imbalance_violation", result, std::vector<float>{0.0f, 0.0f, 0.0f});

  config = SiteLimitConfig{};
  config.phases = 3;
  config.voltage = 100.0f;
  config.grid_max_power = 9000.0f;
  measurements = SitePowerMeasurements{};
  measurements.grid_power_aggregate = 3000.0f;
  result = get_site_spare_current_per_phase(config, measurements);
  assert_equal("aggregate_measurement_fallback", result, std::vector<float>{20.0f, 20.0f, 20.0f});

  return 0;
}
'''


class SiteLimitTests(unittest.TestCase):
    def test_cpp_site_limit_cases(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            source = Path(tmpdir) / "site_limits_test.cpp"
            binary = Path(tmpdir) / "site_limits_test"
            source.write_text(textwrap.dedent(CPP_SOURCE))
            compile_result = subprocess.run(
                ["c++", "-std=c++17", "-I", str(REPO_ROOT), str(source), "-o", str(binary)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run([str(binary)], text=True, capture_output=True)
            if run_result.returncode != 0:
                self.fail(run_result.stderr)


if __name__ == "__main__":
    unittest.main()
