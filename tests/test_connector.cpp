#include "esphome/components/ocpp/connector.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using esphome::ocpp::ConfiguredConnector;
using esphome::ocpp::ConnectorCurrentState;
using esphome::ocpp::connector_used_current_max;
using esphome::ocpp::connector_state_from_ocpp_status;
using esphome::ocpp::effective_allocated_current;
using esphome::ocpp::effective_connector_used_current;
using esphome::ocpp::equal_available_current;
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
  assert_equal("equal_available_current_is_zero_without_active_connectors",
               equal_available_current(18.0f, 12.0f, 0), 0.0f);
  assert_equal("equal_available_current_allows_prospective_connector",
               equal_available_current(9.0f, 0.0f, 1), 9.0f);
  assert_equal("equal_available_current_adds_equal_site_share",
               equal_available_current(12.0f, 8.0f, 2), 14.0f);
  assert_equal("equal_available_current_allows_negative_site_available_current",
               equal_available_current(-3.0f, 10.0f, 1), 7.0f);
  assert_equal("equal_available_current_rejects_negative_current",
               equal_available_current(12.0f, -1.0f, 2), 6.0f);
  assert_equal("connector_state_maps_available", std::string(connector_state_from_ocpp_status("Available")),
               std::string("unplugged"));
  assert_equal("connector_state_maps_preparing", std::string(connector_state_from_ocpp_status("Preparing")),
               std::string("plugged"));
  assert_equal("connector_state_maps_charging", std::string(connector_state_from_ocpp_status("Charging")),
               std::string("charging"));
  assert_equal("connector_state_maps_suspended_ev", std::string(connector_state_from_ocpp_status("SuspendedEV")),
               std::string("paused"));
  assert_equal("connector_state_maps_unknown", std::string(connector_state_from_ocpp_status("Other")),
               std::string("unknown"));

  ConnectorCurrentState current_state;
  current_state.is_charging = false;
  current_state.has_measured_used_current = true;
  current_state.measured_used_current = 12.0f;
  current_state.allocated_current = 16.0f;
  assert_equal("effective_used_current_inactive_is_zero", effective_connector_used_current(current_state), 0.0f);

  current_state = ConnectorCurrentState{};
  current_state.is_charging = true;
  current_state.allocated_current = 16.0f;
  assert_equal("effective_used_current_falls_back_to_allocated_current",
               effective_connector_used_current(current_state), 16.0f);

  current_state.has_measured_used_current = true;
  current_state.measured_used_current = 9.5f;
  assert_equal("effective_used_current_uses_session_measurement", effective_connector_used_current(current_state), 9.5f);

  current_state.measured_used_current = -1.0f;
  assert_equal("effective_used_current_rejects_negative_measurement", effective_connector_used_current(current_state),
               0.0f);
  current_state.measured_used_current = std::numeric_limits<float>::infinity();
  assert_equal("effective_used_current_rejects_infinite_measurement", effective_connector_used_current(current_state),
               0.0f);

  ConfiguredConnector connector{1, 32.0f};
  connector.latest_used_current = {8.0f, 12.0f, 4.0f};
  assert_equal("used_current_max_uses_highest_phase", connector_used_current_max(connector), 12.0f);

  connector.is_charging = true;
  connector.has_session_current_import = true;
  connector.allocated_current = 16.0f;
  assert_equal("configured_connector_effective_used_current_uses_measurement",
               effective_connector_used_current(connector), 12.0f);

  reset_connector_session_current(&connector);
  assert_equal("reset_session_clears_session_current", connector.has_session_current_import, false);
  assert_equal("reset_session_clears_latest_current", connector.latest_current_import, 0.0f);
  assert_equal("reset_session_clears_used_current", connector_used_current_max(connector), 0.0f);

  connector.has_preferred_current_limit = true;
  connector.preferred_current_limit = 10.0f;
  update_connector_allocation(&connector, 14.0f, 6.0f);
  assert_equal("update_allocation_uses_calculated_available_current", connector.available_current, 14.0f);
  assert_equal("update_allocation_sets_allocated_current", connector.allocated_current, 10.0f);

  connector.enabled = false;
  update_connector_allocation(&connector, 14.0f, 6.0f);
  assert_equal("update_allocation_respects_disabled_state", connector.allocated_current, 0.0f);

  connector.enabled = true;
  connector.has_preferred_current_limit = false;
  update_connector_allocation(&connector, std::numeric_limits<float>::infinity(), 6.0f);
  assert_equal("update_allocation_uses_connector_maximum_when_site_is_unbounded", connector.available_current, 32.0f);
  assert_equal("update_allocation_allocates_connector_maximum_when_site_is_unbounded", connector.allocated_current, 32.0f);

  return 0;
}
