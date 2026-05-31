#pragma once

#include "esphome/components/json/json_util.h"
#include "esphome/components/socket/socket.h"
#include "esphome/core/component.h"

#ifdef USE_OCPP

#include <memory>
#include <string>
#include <vector>

namespace esphome::ocpp {

struct ConfiguredConnector {
  uint8_t id;
  float max_current;
};

struct ConfiguredCharger {
  std::string charge_point_id;
  std::vector<ConfiguredConnector> connectors;
};

class OcppServer : public Component {
 public:
  void set_port(uint16_t port) { this->port_ = port; }
  void set_path(std::string path);
  void add_charger(std::string charge_point_id);
  void add_connector(std::string charge_point_id, uint8_t connector_id, float max_current);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void disconnect();
  void remote_start(uint8_t connector_id, std::string id_tag, float current_limit);

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
  void handle_call_result_(const std::string &unique_id, JsonObject payload);
  void handle_call_error_(const std::string &unique_id, const std::string &error_code, const std::string &description);
  void send_ws_text_(const std::string &message);
  void send_ocpp_error_(const std::string &unique_id, const char *code, const char *description);

  bool request_matches_path_(const std::string &uri);
  ConfiguredCharger *find_charger_(const std::string &charge_point_id);
  const ConfiguredCharger *find_charger_(const std::string &charge_point_id) const;
  const ConfiguredConnector *find_connector_(int connector_id) const;
  std::string websocket_accept_key_(const std::string &client_key);

  uint16_t port_{9000};
  std::string path_{"/ocpp"};
  std::vector<ConfiguredCharger> chargers_;
  std::unique_ptr<socket::ListenSocket> server_;
  std::unique_ptr<socket::Socket> client_;
  std::string rx_buffer_;
  std::string charge_point_id_;
  bool handshake_done_{false};
  uint32_t next_message_id_{1};
  uint32_t next_transaction_id_{1};
};

}  // namespace esphome::ocpp

#endif  // USE_OCPP
