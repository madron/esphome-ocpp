#pragma once

#include "esphome/components/json/json_util.h"
#include "esphome/components/socket/socket.h"
#include "esphome/core/component.h"

#include <memory>
#include <string>

namespace esphome::ocpp {

class OcppComponent;

class OcppComponent : public Component {
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override;

    // server
    void set_server_port(uint16_t port) { this->server_port_ = port; }
    void set_server_path(std::string path);

  protected:
    void accept_client_();
    void close_client_();
    void read_client_();
    void handle_http_handshake_();
    void handle_ws_frames_();
    #include "protocol.h"

    bool request_matches_path_(const std::string &uri);
    std::string websocket_accept_key_(const std::string &client_key);

    // server
    std::unique_ptr<socket::ListenSocket> server_;
    uint16_t server_port_{9000};
    std::string server_path_{"/"};

};

}  // namespace esphome::ocpp
