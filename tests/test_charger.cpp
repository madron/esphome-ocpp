#include "esphome/components/ocpp/charger.h"

#include <cstdlib>
#include <iostream>

using esphome::ocpp::ConfiguredCharger;
using esphome::ocpp::ConfiguredConnector;
using esphome::ocpp::charger_drawn_current_from_connectors;
using esphome::ocpp::charger_drawn_current_from_source;
using esphome::ocpp::charger_drawn_current_max;
using esphome::ocpp::charger_has_charge_point_id;
using esphome::ocpp::configure_charger;
using esphome::ocpp::effective_connector_max_current;
using esphome::ocpp::find_active_transaction_connector;
using esphome::ocpp::find_configured_connector;
using esphome::ocpp::find_transaction_connector;
using esphome::ocpp::update_charger_drawn_current_from_connectors;
using esphome::ocpp::update_charger_drawn_current_from_source;

template<typename T> void assert_equal(const char *description, const T &actual, const T &expected) {
  if (actual == expected)
    return;
  std::cerr << description << "\n";
  std::cerr << "Expected: " << expected << "\n";
  std::cerr << "Actual:   " << actual << "\n";
  std::abort();
}

void assert_true(const char *description, bool actual) { assert_equal(description, actual, true); }
void assert_false(const char *description, bool actual) { assert_equal(description, actual, false); }

int main() {
  ConfiguredCharger charger;
  charger.connector = ConfiguredConnector{1, 16.0f};
  charger.has_connector = true;

  configure_charger(&charger, "wallbox", 32.0f);
  assert_true("configure_charger_sets_charge_point_id", charger_has_charge_point_id(charger, "wallbox"));
  assert_false("configure_charger_resets_connector", charger.has_connector);
  assert_equal("configure_charger_sets_max_current", charger.max_current, 32.0f);
  assert_equal("configure_charger_resets_drawn_current", charger_drawn_current_max(charger), 0.0f);

  assert_equal("effective_connector_max_current_uses_charger_limit", effective_connector_max_current(16.0f, 32.0f),
               16.0f);
  assert_equal("effective_connector_max_current_uses_connector_limit", effective_connector_max_current(32.0f, 16.0f),
               16.0f);

  auto scalar_source_drawn_current = charger_drawn_current_from_source(13.0f);
  assert_equal("charger_drawn_current_source_replicates_l1", scalar_source_drawn_current[0], 13.0f);
  assert_equal("charger_drawn_current_source_replicates_l2", scalar_source_drawn_current[1], 13.0f);
  assert_equal("charger_drawn_current_source_replicates_l3", scalar_source_drawn_current[2], 13.0f);
  update_charger_drawn_current_from_source(&charger, 13.0f);
  assert_equal("charger_drawn_current_source_max_matches_source", charger_drawn_current_max(charger), 13.0f);

  charger.connector = ConfiguredConnector{2, 16.0f};
  charger.has_connector = true;
  charger.connector.latest_drawn_current = {6.0f, 8.0f, 4.0f};
  auto connector_drawn_current = charger_drawn_current_from_connectors(charger);
  assert_equal("charger_drawn_current_sums_l1", connector_drawn_current[0], 6.0f);
  assert_equal("charger_drawn_current_sums_l2", connector_drawn_current[1], 8.0f);
  assert_equal("charger_drawn_current_sums_l3", connector_drawn_current[2], 4.0f);
  update_charger_drawn_current_from_connectors(&charger);
  assert_equal("charger_drawn_current_max_uses_highest_phase", charger_drawn_current_max(charger), 8.0f);

  assert_true("find_configured_connector_matches_id", find_configured_connector(&charger, 2) == &charger.connector);
  assert_true("find_configured_connector_rejects_other_id", find_configured_connector(&charger, 3) == nullptr);

  charger.connector.has_active_transaction = false;
  assert_true("find_active_transaction_connector_requires_active_transaction",
              find_active_transaction_connector(&charger) == nullptr);
  charger.connector.has_active_transaction = true;
  charger.connector.active_transaction_id = 42;
  assert_true("find_active_transaction_connector_finds_active_connector",
              find_active_transaction_connector(&charger) == &charger.connector);
  assert_true("find_transaction_connector_matches_transaction", find_transaction_connector(&charger, 42) == &charger.connector);
  assert_true("find_transaction_connector_rejects_other_transaction", find_transaction_connector(&charger, 43) == nullptr);

  return 0;
}
