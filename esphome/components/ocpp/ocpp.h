#pragma once

#include "protocol.h"
#include "server.h"
#include "esphome/core/component.h"

#include <string>
#include <utility>

namespace esphome::ocpp {

class OcppComponent;

class OcppComponent : public Component, public OcppServerListener, public OcppProtocolTransport {
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override;

    // server
    void set_server_port(uint16_t port) { this->server_.set_port(port); }
    void set_server_path(std::string path) { this->server_.set_path(std::move(path)); }

  protected:
    void on_websocket_connected(const std::string &connection_id) override;
    void on_websocket_disconnected() override;
    void on_websocket_text(const std::string &message) override;
    void send_ocpp_text(const std::string &message) override;

    OcppServer server_;
    OcppProtocol protocol_;

};

}  // namespace esphome::ocpp
