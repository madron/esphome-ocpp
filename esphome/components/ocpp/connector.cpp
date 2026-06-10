#include "connector.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace esphome::ocpp {
namespace {

float clamp_finite_non_negative(float value) { return std::isfinite(value) && value > 0.0f ? value : 0.0f; }

}  // namespace

float effective_allocated_current(float available_current, float max_current, float requested_current, float min_current,
                                  bool enabled) {
  if (!enabled)
    return 0.0f;
  const float effective_current = std::min({clamp_finite_non_negative(available_current),
                                           clamp_finite_non_negative(max_current),
                                           clamp_finite_non_negative(requested_current)});
  if (min_current > 0.0f && effective_current < min_current)
    return 0.0f;
  return effective_current;
}

float effective_allocated_current(float available_current, float min_current) {
  return effective_allocated_current(available_current, available_current, available_current, min_current, true);
}

float equal_available_current(float site_available_current, float connector_current, uint8_t active_connector_count) {
  if (active_connector_count == 0)
    return 0.0f;
  const float safe_connector_current = clamp_finite_non_negative(connector_current);
  if (!std::isfinite(site_available_current))
    return safe_connector_current + site_available_current;
  return clamp_finite_non_negative(safe_connector_current + site_available_current / active_connector_count);
}

const char *connector_state_from_ocpp_status(const char *status) {
  if (status == nullptr)
    return "unknown";
  if (std::strcmp(status, "Available") == 0)
    return "unplugged";
  if (std::strcmp(status, "Preparing") == 0)
    return "plugged";
  if (std::strcmp(status, "Charging") == 0)
    return "charging";
  if (std::strcmp(status, "SuspendedEV") == 0 || std::strcmp(status, "SuspendedEVSE") == 0)
    return "paused";
  if (std::strcmp(status, "Finishing") == 0)
    return "finishing";
  if (std::strcmp(status, "Reserved") == 0)
    return "reserved";
  if (std::strcmp(status, "Unavailable") == 0)
    return "unavailable";
  if (std::strcmp(status, "Faulted") == 0)
    return "faulted";
  return "unknown";
}

void reset_connector_session_current(ConfiguredConnector *connector) {
  if (connector == nullptr)
    return;
  connector->has_session_current_import = false;
  connector->has_latest_current_import = false;
  connector->latest_current_import = 0.0f;
}

void update_connector_allocation(ConfiguredConnector *connector, float available_current, float min_current) {
  if (connector == nullptr)
    return;
  connector->available_current = std::isfinite(available_current) ? clamp_finite_non_negative(available_current)
                                                                  : connector->max_current;
  const float requested_current = connector->has_preferred_current_limit ? connector->preferred_current_limit
                                                                         : connector->max_current;
  connector->allocated_current = effective_allocated_current(connector->available_current, connector->max_current,
                                                            requested_current, min_current, connector->enabled);
}

}  // namespace esphome::ocpp
