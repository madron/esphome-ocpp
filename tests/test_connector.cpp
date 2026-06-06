#include "esphome/components/ocpp/connector.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using esphome::ocpp::ConfiguredConnector;
using esphome::ocpp::ConnectorCurrentState;
using esphome::ocpp::connector_drawn_current_max;
using esphome::ocpp::effective_allocated_current;
using esphome::ocpp::effective_connector_drawn_current;
using esphome::ocpp::reset_connector_session_current;
using esphome::ocpp::update_connector_allocation;

template<typename T> void assert_equal(const char *description, const T &actual, const T &expected) {
  if (actual == expected)
    return;
  std::cerr << description << "\n";
  std::cerr << "Expected: " << expected << "\n";
  std::cerr << "Actual:   " << actual << "\n";
  std::abort();
}

int main() {
  assert_equal("allocated_current_matches_available_above_minimum", effective_allocated_current(16.0f, 6.0f), 16.0f);
  assert_equal("allocated_current_is_zero_below_minimum", effective_allocated_current(4.0f, 6.0f), 0.0f);
  assert_equal("allocated_current_rejects_negative_values", effective_allocated_current(-1.0f, 6.0f), 0.0f);
  assert_equal("allocated_current_rejects_infinite_values",
               effective_allocated_current(std::numeric_limits<float>::infinity(), 6.0f), 0.0f);
  assert_equal("allocated_current_respects_connector_maximum",
               effective_allocated_current(32.0f, 16.0f, 32.0f, 6.0f, true), 16.0f);
  assert_equal("allocated_current_respects_requested_current_limit",
               effective_allocated_current(16.0f, 16.0f, 10.0f, 6.0f, true), 10.0f);
  assert_equal("allocated_current_is_zero_when_requested_limit_below_minimum",
               effective_allocated_current(16.0f, 16.0f, 4.0f, 6.0f, true), 0.0f);
  assert_equal("allocated_current_respects_enabled_state",
               effective_allocated_current(16.0f, 16.0f, 16.0f, 6.0f, false), 0.0f);

  ConnectorCurrentState current_state;
  current_state.is_charging = false;
  current_state.has_measured_drawn_current = true;
  current_state.measured_drawn_current = 12.0f;
  current_state.allocated_current = 16.0f;
  assert_equal("effective_drawn_current_inactive_is_zero", effective_connector_drawn_current(current_state), 0.0f);

  current_state = ConnectorCurrentState{};
  current_state.is_charging = true;
  current_state.allocated_current = 16.0f;
  assert_equal("effective_drawn_current_falls_back_to_allocated_current",
               effective_connector_drawn_current(current_state), 16.0f);

  current_state.has_measured_drawn_current = true;
  current_state.measured_drawn_current = 9.5f;
  assert_equal("effective_drawn_current_uses_session_measurement", effective_connector_drawn_current(current_state), 9.5f);

  current_state.measured_drawn_current = -1.0f;
  assert_equal("effective_drawn_current_rejects_negative_measurement", effective_connector_drawn_current(current_state),
               0.0f);
  current_state.measured_drawn_current = std::numeric_limits<float>::infinity();
  assert_equal("effective_drawn_current_rejects_infinite_measurement", effective_connector_drawn_current(current_state),
               0.0f);

  ConfiguredConnector connector{1, 32.0f};
  connector.latest_drawn_current = {8.0f, 12.0f, 4.0f};
  assert_equal("drawn_current_max_uses_highest_phase", connector_drawn_current_max(connector), 12.0f);

  connector.is_charging = true;
  connector.has_session_current_import = true;
  connector.allocated_current = 16.0f;
  assert_equal("configured_connector_effective_drawn_current_uses_measurement",
               effective_connector_drawn_current(connector), 12.0f);

  reset_connector_session_current(&connector);
  assert_equal("reset_session_clears_session_current", connector.has_session_current_import, false);
  assert_equal("reset_session_clears_latest_current", connector.latest_current_import, 0.0f);
  assert_equal("reset_session_clears_drawn_current", connector_drawn_current_max(connector), 0.0f);

  connector.has_preferred_current_limit = true;
  connector.preferred_current_limit = 10.0f;
  update_connector_allocation(&connector, 6.0f);
  assert_equal("update_allocation_uses_preferred_limit", connector.available_current, 32.0f);
  assert_equal("update_allocation_sets_allocated_current", connector.allocated_current, 10.0f);

  connector.enabled = false;
  update_connector_allocation(&connector, 6.0f);
  assert_equal("update_allocation_respects_disabled_state", connector.allocated_current, 0.0f);

  return 0;
}
