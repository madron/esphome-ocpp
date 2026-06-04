#pragma once

#include "site_limits.h"

#include "esphome/components/button/button.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/socket/socket.h"
#include "esphome/core/component.h"

#ifdef USE_OCPP

#include <array>
#include <memory>
#include <string>

namespace esphome::ocpp {

class OcppServer;

class OcppConnectorButton : public button::Button {
 public:
  void set_parent(OcppServer *parent, uint8_t connector_id, bool start) {
    this->parent_ = parent;
    this->connector_id_ = connector_id;
    this->start_ = start;
  }

 protected:
  void press_action() override;

  OcppServer *parent_{nullptr};
  uint8_t connector_id_{0};
  bool start_{false};
};

class OcppCurrentLimitNumber : public number::Number {
 public:
  void set_parent(OcppServer *parent, uint8_t connector_id) {
    this->parent_ = parent;
    this->connector_id_ = connector_id;
  }

 protected:
  void control(float value) override;

  OcppServer *parent_{nullptr};
  uint8_t connector_id_{0};
};

struct ConfiguredConnector {
  uint8_t id;
  float max_current;
  std::string id_tag{"ESPHome"};
  sensor::Sensor *current_sensor{nullptr};
  sensor::Sensor *power_sensor{nullptr};
  OcppCurrentLimitNumber *current_limit_number{nullptr};
  OcppConnectorButton *start_button{nullptr};
  OcppConnectorButton *stop_button{nullptr};
  bool has_preferred_current_limit{false};
  float preferred_current_limit{0.0f};
  bool has_active_transaction{false};
  uint32_t active_transaction_id{0};
  std::string active_id_tag;
  bool is_charging{false};
  bool charging_profile_applied{false};
  bool has_latest_current_import{false};
  bool has_latest_power_active_import{false};
  float latest_current_import{0.0f};
  float latest_power_active_import{0.0f};
};

struct ConfiguredCharger {
  std::string charge_point_id;
  ConfiguredConnector connector;
  bool has_connector{false};
};

struct PendingOcppCall {
  bool active{false};
  char unique_id[40]{};
  const char *action{nullptr};
  uint8_t connector_id{0};
  uint32_t transaction_id{0};
  float current_limit{0.0f};
};

class OcppServer : public Component {
 public:
  void set_port(uint16_t port) { this->port_ = port; }
  void set_path(std::string path);
  void set_site(uint8_t phases, float voltage);
  void set_grid_max_power(float max_power);
  void set_grid_max_phase_imbalance(float max_phase_imbalance);
  void set_grid_max_current_per_phase(float max_current_per_phase);
  void set_grid_power_l1_sensor(sensor::Sensor *sensor) { this->grid_power_l1_sensor_ = sensor; }
  void set_grid_power_l2_sensor(sensor::Sensor *sensor) { this->grid_power_l2_sensor_ = sensor; }
  void set_grid_power_l3_sensor(sensor::Sensor *sensor) { this->grid_power_l3_sensor_ = sensor; }
  void set_grid_power_aggregate_sensor(sensor::Sensor *sensor) { this->grid_power_aggregate_sensor_ = sensor; }
  void add_charger(std::string charge_point_id);
  void add_connector(std::string charge_point_id, uint8_t connector_id, float max_current, std::string id_tag);
  void set_connector_current_sensor(std::string charge_point_id, uint8_t connector_id, sensor::Sensor *current_sensor);
  void set_connector_power_sensor(std::string charge_point_id, uint8_t connector_id, sensor::Sensor *power_sensor);
  void set_connector_current_limit_number(std::string charge_point_id, uint8_t connector_id,
                                          OcppCurrentLimitNumber *current_limit_number, float initial_limit);
  void set_connector_start_button(std::string charge_point_id, uint8_t connector_id, OcppConnectorButton *start_button);
  void set_connector_stop_button(std::string charge_point_id, uint8_t connector_id, OcppConnectorButton *stop_button);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void disconnect();
  void remote_start(uint8_t connector_id);
  void remote_start(uint8_t connector_id, std::string id_tag);
  void remote_start(uint8_t connector_id, std::string id_tag, float current_limit);
  void remote_stop();
  void remote_stop_connector(uint8_t connector_id);
  void remote_stop(uint32_t transaction_id);
  void set_current_limit(uint8_t connector_id, float current_limit);
  bool has_latest_current_import(uint8_t connector_id) const;
  bool has_latest_power_active_import(uint8_t connector_id) const;
  float get_latest_current_import(uint8_t connector_id) const;
  float get_latest_power_active_import(uint8_t connector_id) const;

 protected:
  void accept_client_();
  void close_client_();
  void read_client_();
  void handle_http_handshake_();
  void handle_ws_frames_();
  void handle_ws_text_(const std::string &message);
  void handle_boot_notification_(const std::string &unique_id, JsonObject payload);
  void handle_heartbeat_(const std::string &unique_id);
  void handle_authorize_(const std::string &unique_id, JsonObject payload);
  void handle_status_notification_(const std::string &unique_id, JsonObject payload);
  void handle_start_transaction_(const std::string &unique_id, JsonObject payload);
  void handle_stop_transaction_(const std::string &unique_id, JsonObject payload);
  void handle_meter_values_(const std::string &unique_id, JsonObject payload);
  void handle_call_result_(const std::string &unique_id, JsonObject payload);
  void handle_call_error_(const std::string &unique_id, const std::string &error_code, const std::string &description);
  void remote_start_(uint8_t connector_id, std::string id_tag, bool use_current_limit, float current_limit);
  void send_set_charging_profile_(uint8_t connector_id, uint32_t transaction_id, float current_limit);
  std::string send_ocpp_call_(const char *unique_prefix, const char *action, const std::string &payload_json,
                              uint8_t connector_id = 0, uint32_t transaction_id = 0, float current_limit = 0.0f);
  void send_ws_text_(const std::string &message);
  void send_ocpp_error_(const std::string &unique_id, const char *code, const char *description);

  bool request_matches_path_(const std::string &uri);
  ConfiguredCharger *find_charger_(const std::string &charge_point_id);
  const ConfiguredCharger *find_charger_(const std::string &charge_point_id) const;
  ConfiguredConnector *find_connector_(int connector_id);
  const ConfiguredConnector *find_connector_(int connector_id) const;
  ConfiguredConnector *find_active_transaction_connector_();
  ConfiguredConnector *find_transaction_connector_(uint32_t transaction_id);
  void note_transaction_id_(uint32_t transaction_id);
  void mark_transaction_started_(uint8_t connector_id, uint32_t transaction_id, const char *id_tag);
  void recover_transaction_from_meter_values_(uint8_t connector_id, uint32_t transaction_id);
  void clear_transaction_(uint32_t transaction_id);
  void send_preferred_current_limit_if_needed_(uint8_t connector_id);
  std::string next_unique_id_(const char *prefix);
  void track_pending_call_(const std::string &unique_id, const char *action, uint8_t connector_id,
                           uint32_t transaction_id, float current_limit);
  PendingOcppCall *find_pending_call_(const std::string &unique_id);
  void clear_pending_call_(const std::string &unique_id);
  void clear_pending_calls_();
  std::string websocket_accept_key_(const std::string &client_key);

  uint16_t port_{9000};
  std::string path_{"/ocpp"};
  SiteLimitConfig site_limits_;
  sensor::Sensor *grid_power_l1_sensor_{nullptr};
  sensor::Sensor *grid_power_l2_sensor_{nullptr};
  sensor::Sensor *grid_power_l3_sensor_{nullptr};
  sensor::Sensor *grid_power_aggregate_sensor_{nullptr};
  ConfiguredCharger charger_;
  bool has_charger_{false};
  std::unique_ptr<socket::ListenSocket> server_;
  std::unique_ptr<socket::Socket> client_;
  std::string rx_buffer_;
  std::string charge_point_id_;
  bool handshake_done_{false};
  uint8_t pending_profile_connector_id_{0};
  float pending_profile_current_limit_{0.0f};
  std::array<PendingOcppCall, 4> pending_calls_{};
  uint32_t next_message_id_{1};
  uint32_t next_transaction_id_{1};
};

}  // namespace esphome::ocpp

#endif  // USE_OCPP
