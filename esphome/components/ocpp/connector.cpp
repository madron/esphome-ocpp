#include "connector.h"

#ifdef USE_OCPP
#include "ocpp.h"
#endif

#include <algorithm>
#include <cmath>

#ifndef OCPP_SPLIT_IMPLEMENTATION_INLINE
#define OCPP_SPLIT_IMPLEMENTATION_INLINE
#endif

namespace esphome::ocpp {
namespace {

float clamp_finite_non_negative(float value) { return std::isfinite(value) && value > 0.0f ? value : 0.0f; }

}  // namespace

OCPP_SPLIT_IMPLEMENTATION_INLINE float effective_allocated_current(float available_current, float max_current,
                                                                   float requested_current, float min_current,
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

OCPP_SPLIT_IMPLEMENTATION_INLINE float effective_allocated_current(float available_current, float min_current) {
  return effective_allocated_current(available_current, available_current, available_current, min_current, true);
}

OCPP_SPLIT_IMPLEMENTATION_INLINE float effective_connector_drawn_current(const ConnectorCurrentState &state) {
  if (!state.is_charging)
    return 0.0f;
  if (state.has_measured_drawn_current)
    return clamp_finite_non_negative(state.measured_drawn_current);
  return clamp_finite_non_negative(state.allocated_current);
}

OCPP_SPLIT_IMPLEMENTATION_INLINE float effective_connector_drawn_current(const ConfiguredConnector &connector) {
  return effective_connector_drawn_current(ConnectorCurrentState{connector.is_charging,
                                                                connector.has_session_current_import,
                                                                connector_drawn_current_max(connector),
                                                                connector.allocated_current});
}

OCPP_SPLIT_IMPLEMENTATION_INLINE float connector_drawn_current_max(const ConfiguredConnector &connector) {
  return std::max(connector.latest_drawn_current[0],
                  std::max(connector.latest_drawn_current[1], connector.latest_drawn_current[2]));
}

OCPP_SPLIT_IMPLEMENTATION_INLINE void reset_connector_session_current(ConfiguredConnector *connector) {
  if (connector == nullptr)
    return;
  connector->has_session_current_import = false;
  connector->has_latest_current_import = false;
  connector->latest_current_import = 0.0f;
  connector->latest_drawn_current = {};
}

OCPP_SPLIT_IMPLEMENTATION_INLINE void update_connector_allocation(ConfiguredConnector *connector, float min_current) {
  if (connector == nullptr)
    return;
  // TODO: available_current currently uses the connector maximum as a placeholder. It should become dependent on
  // site availability, other active sessions, and the configured allocation policy.
  connector->available_current = connector->max_current;
  const float requested_current = connector->has_preferred_current_limit ? connector->preferred_current_limit
                                                                         : connector->max_current;
  connector->allocated_current = effective_allocated_current(connector->available_current, connector->max_current,
                                                            requested_current, min_current, connector->enabled);
}

#ifdef USE_OCPP
OCPP_SPLIT_IMPLEMENTATION_INLINE void OcppCurrentLimitNumber::control(float value) {
  if (this->parent_ == nullptr)
    return;
  this->parent_->set_current_limit(this->connector_id_, value);
}

OCPP_SPLIT_IMPLEMENTATION_INLINE void OcppConnectorEnabledSwitch::write_state(bool state) {
  if (this->parent_ == nullptr)
    return;
  this->parent_->set_connector_enabled(this->connector_id_, state);
}

OCPP_SPLIT_IMPLEMENTATION_INLINE void OcppConnectorButton::press_action() {
  if (this->parent_ == nullptr)
    return;
  this->parent_->restart_connector_session(this->connector_id_);
}
#endif

}  // namespace esphome::ocpp
