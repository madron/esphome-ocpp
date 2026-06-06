#include "esphome/components/ocpp/site.h"

#include <cstdlib>
#include <iostream>

using esphome::ocpp::ConfiguredCharger;
using esphome::ocpp::ConfiguredSite;
using esphome::ocpp::configure_site;
using esphome::ocpp::site_drawn_current_from_charger;
using esphome::ocpp::site_drawn_current_max;
using esphome::ocpp::update_site_drawn_current;

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
  ConfiguredSite site;
  configure_site(&site, 3, 230.0f);
  assert_equal("configure_site_sets_phases", site.limits.phases, static_cast<uint8_t>(3));
  assert_equal("configure_site_sets_voltage", site.limits.voltage, 230.0f);

  ConfiguredCharger charger;
  charger.phases = 3;
  charger.latest_drawn_current = {6.0f, 8.0f, 4.0f};
  charger.phase_mapping = {1, 2, 0};
  auto site_drawn_current = site_drawn_current_from_charger(charger);
  assert_equal("site_drawn_current_maps_rotated_l1", site_drawn_current[1], 6.0f);
  assert_equal("site_drawn_current_maps_rotated_l2", site_drawn_current[2], 8.0f);
  assert_equal("site_drawn_current_maps_rotated_l3", site_drawn_current[0], 4.0f);
  assert_equal("site_drawn_current_max_uses_highest_phase", site_drawn_current_max(site_drawn_current), 8.0f);

  assert_true("update_site_drawn_current_reports_change", update_site_drawn_current(&site, &charger));
  assert_equal("update_site_drawn_current_stores_l1", site.latest_drawn_current[0], 4.0f);
  assert_equal("update_site_drawn_current_stores_l2", site.latest_drawn_current[1], 6.0f);
  assert_equal("update_site_drawn_current_stores_l3", site.latest_drawn_current[2], 8.0f);
  assert_false("update_site_drawn_current_reports_unchanged", update_site_drawn_current(&site, &charger));

  charger.phases = 1;
  charger.latest_drawn_current = {7.0f, 8.0f, 9.0f};
  charger.phase_mapping = {1, 0, 2};
  site_drawn_current = site_drawn_current_from_charger(charger);
  assert_equal("single_phase_charger_maps_only_local_l1_to_site_phase", site_drawn_current[1], 7.0f);
  assert_equal("single_phase_charger_ignores_unused_local_l2", site_drawn_current[0], 0.0f);
  assert_equal("single_phase_charger_ignores_unused_local_l3", site_drawn_current[2], 0.0f);

  assert_true("update_site_drawn_current_accepts_null_site", !update_site_drawn_current(nullptr, &charger));
  return 0;
}