#pragma once

#include "esphome/components/json/json_util.h"
#include "esphome/components/socket/socket.h"
#include "esphome/core/component.h"

#ifdef USE_OCPP

#include <memory>
#include <string>

namespace esphome::ocpp {

class OcppComponent;

class OcppComponent : public Component {
 public:
  void set_port(uint16_t port) { this->port_ = port; }
  void set_path(std::string path);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  void accept_client_();
  void close_client_();
  void read_client_();
  void handle_http_handshake_();
  void handle_ws_frames_();
#include "protocol.h"

  bool request_matches_path_(const std::string &uri);
  std::string websocket_accept_key_(const std::string &client_key);

  uint16_t port_{9000};
  std::string path_{"/ocpp"};
  std::unique_ptr<socket::ListenSocket> server_;
};

}  // namespace esphome::ocpp

#endif  // USE_OCPP
