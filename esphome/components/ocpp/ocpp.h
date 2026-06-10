#pragma once

#include "charger.h"
#include "site.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#ifdef USE_OCPP

#include <array>
#include <memory>
#include <string>

namespace esphome::ocpp {

class OcppServer;

class OcppServer : public Component {
 public:
  void set_port(uint16_t port) { this->port_ = port; }
  void set_path(std::string path);
  void set_allocation_min_current(float min_current) { this->allocation_min_current_ = min_current; }
  void set_site(uint8_t phases, float voltage);
  void add_charger(std::string charge_point_id, float max_current, uint8_t phases = 3);
  void set_charger_phase_mapping(std::string charge_point_id, uint8_t charger_phase, uint8_t site_phase);
  void add_connector(std::string charge_point_id, uint8_t connector_id, float max_current);
  void set_connector_current_sensor(std::string charge_point_id, uint8_t connector_id, sensor::Sensor *current_sensor);
  void set_connector_state_sensor(std::string charge_point_id, uint8_t connector_id,
                                  text_sensor::TextSensor *state_sensor);
  void set_connector_current_limit_number(std::string charge_point_id, uint8_t connector_id,
                                          OcppCurrentLimitNumber *current_limit_number, float initial_limit);
  void apply_connector_current_limit_restore(uint8_t connector_id, float current_limit);
  void set_connector_enabled_switch(std::string charge_point_id, uint8_t connector_id,
                                    OcppConnectorEnabledSwitch *enabled_switch);
  void set_connector_restart_button(std::string charge_point_id, uint8_t connector_id,
                                    OcppConnectorButton *restart_button);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void disconnect();
  void remote_start(uint8_t connector_id);
  void remote_stop();
  void remote_stop_connector(uint8_t connector_id);
  void remote_stop(uint32_t transaction_id);
  void set_current_limit(uint8_t connector_id, float current_limit);
  void set_connector_enabled(uint8_t connector_id, bool enabled);
  bool is_connector_enabled(uint8_t connector_id) const;
  void restart_connector_session(uint8_t connector_id);
  bool has_latest_current_import(uint8_t connector_id) const;
  bool has_session_current_import(uint8_t connector_id) const;
  bool has_latest_power_active_import(uint8_t connector_id) const;
  float get_latest_current_import(uint8_t connector_id) const;
  float get_latest_power_active_import(uint8_t connector_id) const;

 protected:
  void accept_client_();
  void close_client_();
  void read_client_();
  void handle_http_handshake_();
  void handle_ws_frames_();
#include "protocol.h"

  bool request_matches_path_(const std::string &uri);
  ConfiguredCharger *find_charger_(const std::string &charge_point_id);
  const ConfiguredCharger *find_charger_(const std::string &charge_point_id) const;
  ConfiguredConnector *find_connector_(int connector_id);
  const ConfiguredConnector *find_connector_(int connector_id) const;
  bool update_connector_allocation_(ConfiguredConnector *connector, bool include_connector_as_active = false);
  bool should_defer_connector_allocation_(ConfiguredConnector *connector, bool include_connector_as_active);
  void reset_session_current_(ConfiguredConnector *connector);
  void publish_current_if_configured_(ConfiguredConnector *connector);
  void publish_connector_state_if_configured_(ConfiguredConnector *connector);
  std::string websocket_accept_key_(const std::string &client_key);

  uint16_t port_{9000};
  float allocation_min_current_{6.0f};
  std::string path_{"/ocpp"};
  ConfiguredSite site_;
  ConfiguredCharger charger_;
  bool has_charger_{false};
  std::unique_ptr<socket::ListenSocket> server_;
};

}  // namespace esphome::ocpp

#endif  // USE_OCPP
