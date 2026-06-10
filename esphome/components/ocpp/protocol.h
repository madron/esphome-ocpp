#pragma once

// OcppServer protocol member declarations. This file is included from
// OcppServer's protected section in ocpp.h so these remain class members.

struct PendingOcppCall {
  bool active{false};
  char unique_id[40]{};
  const char *action{nullptr};
  uint8_t connector_id{0};
  uint32_t transaction_id{0};
  float current_limit{0.0f};
};

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
std::string next_unique_id_(const char *prefix);
void track_pending_call_(const std::string &unique_id, const char *action, uint8_t connector_id,
                         uint32_t transaction_id, float current_limit);
PendingOcppCall *find_pending_call_(const std::string &unique_id);
void clear_pending_call_(const std::string &unique_id);
void clear_pending_calls_();

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
std::array<PendingOcppCall, 4> pending_calls_{};
std::array<std::string, 4> tx_queue_{};
uint8_t tx_queue_head_{0};
uint8_t tx_queue_count_{0};
uint32_t next_message_id_{1};
uint32_t next_transaction_id_{1};
