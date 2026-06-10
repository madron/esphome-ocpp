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
  void set_allocation_min_current(float min_current) { this->allocation_min_current_ = min_current; }
  void set_allocation_log_decisions(bool log_decisions) { this->allocation_log_decisions_ = log_decisions; }
  void set_site(uint8_t phases, float voltage);
  void set_storage_capacity(float capacity_kwh);
  void set_storage_power_l1_sensor(sensor::Sensor *sensor) { this->site_.storage_power_l1_sensor = sensor; }
  void set_storage_power_l2_sensor(sensor::Sensor *sensor) { this->site_.storage_power_l2_sensor = sensor; }
  void set_storage_power_l3_sensor(sensor::Sensor *sensor) { this->site_.storage_power_l3_sensor = sensor; }
  void set_storage_power_aggregate_sensor(sensor::Sensor *sensor) { this->site_.storage_power_aggregate_sensor = sensor; }
  void set_storage_soc_sensor(sensor::Sensor *sensor) { this->site_.storage_soc_sensor = sensor; }
  void set_storage_energy_sensor(sensor::Sensor *sensor) { this->site_.storage_energy_sensor = sensor; }
  void add_charger(std::string charge_point_id, float max_current, uint8_t phases = 3);
  void set_charger_phase_mapping(std::string charge_point_id, uint8_t charger_phase, uint8_t site_phase);
  void add_connector(std::string charge_point_id, uint8_t connector_id, float max_current);
  void set_connector_current_sensor(std::string charge_point_id, uint8_t connector_id, sensor::Sensor *current_sensor);
  void set_connector_power_sensor(std::string charge_point_id, uint8_t connector_id, sensor::Sensor *power_sensor);
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
  bool send_remote_start_now_(uint8_t connector_id, const std::string &id_tag, bool use_current_limit,
                              float current_limit);
  void send_pending_remote_start_if_ready_();
  void request_remote_stop_(uint32_t transaction_id);
  bool send_remote_stop_now_(uint32_t transaction_id);
  void send_pending_remote_stop_if_ready_();
  void request_set_charging_profile_(uint8_t connector_id, uint32_t transaction_id, float current_limit);
  bool send_set_charging_profile_now_(uint8_t connector_id, uint32_t transaction_id, float current_limit);
  void send_pending_set_charging_profile_if_ready_();
  std::string send_ocpp_call_(const char *unique_prefix, const char *action, const std::string &payload_json,
                              uint8_t connector_id = 0, uint32_t transaction_id = 0, float current_limit = 0.0f,
                              bool fixed_unique_id = false);
  bool queue_ws_text_(std::string message);
  bool flush_queued_ws_text_();
  void clear_queued_ws_text_();
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
  bool update_connector_allocation_(ConfiguredConnector *connector, bool include_connector_as_active = false);
  bool should_defer_connector_allocation_(ConfiguredConnector *connector, bool include_connector_as_active);
  void run_pending_allocation_evaluation_if_ready_();
  void note_set_charging_profile_accepted_(float current_limit);
  void reset_session_current_(ConfiguredConnector *connector);
  void publish_current_if_configured_(ConfiguredConnector *connector);
  void publish_connector_state_if_configured_(ConfiguredConnector *connector);
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
  float allocation_min_current_{6.0f};
  std::string path_{"/ocpp"};
  ConfiguredSite site_;
  ConfiguredCharger charger_;
  bool has_charger_{false};
  std::unique_ptr<socket::ListenSocket> server_;
  std::unique_ptr<socket::Socket> client_;
  std::string rx_buffer_;
  std::string charge_point_id_;
  bool handshake_done_{false};
  uint8_t pending_profile_connector_id_{0};
  uint8_t pending_session_restart_connector_id_{0};
  float pending_profile_current_limit_{0.0f};
  bool remote_start_in_flight_{false};
  uint8_t remote_start_in_flight_connector_id_{0};
  std::string remote_start_in_flight_id_tag_;
  bool remote_start_in_flight_use_current_limit_{false};
  float remote_start_in_flight_current_limit_{0.0f};
  bool pending_remote_start_{false};
  uint8_t pending_remote_start_connector_id_{0};
  std::string pending_remote_start_id_tag_;
  bool pending_remote_start_use_current_limit_{false};
  float pending_remote_start_current_limit_{0.0f};
  bool remote_stop_in_flight_{false};
  uint32_t remote_stop_in_flight_transaction_id_{0};
  bool pending_remote_stop_{false};
  uint32_t pending_remote_stop_transaction_id_{0};
  bool set_charging_profile_in_flight_{false};
  bool pending_set_charging_profile_{false};
  uint8_t pending_set_charging_profile_connector_id_{0};
  uint32_t pending_set_charging_profile_transaction_id_{0};
  float pending_set_charging_profile_current_limit_{0.0f};
  bool pending_allocation_evaluation_{false};
  uint8_t pending_allocation_connector_id_{0};
  bool pending_allocation_include_connector_as_active_{false};
  bool allocation_log_decisions_{false};
  std::array<PendingOcppCall, 4> pending_calls_{};
  std::array<std::string, 4> tx_queue_{};
  uint8_t tx_queue_head_{0};
  uint8_t tx_queue_count_{0};
  uint32_t next_message_id_{1};
  uint32_t next_transaction_id_{1};
};

}  // namespace esphome::ocpp

#endif  // USE_OCPP
