#pragma once

#include "esphome/components/socket/socket.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace esphome::ocpp {

class OcppServerListener {
  public:
    virtual ~OcppServerListener() = default;

    virtual void on_websocket_connected(const std::string &connection_id) = 0;
    virtual void on_websocket_disconnected() = 0;
    virtual void on_websocket_text(const std::string &message) = 0;
};

class OcppServer {
  public:
    void set_listener(OcppServerListener *listener) { this->listener_ = listener; }
    void set_port(uint16_t port) { this->server_port_ = port; }
    void set_path(std::string path);
    void set_subprotocol(std::string subprotocol) { this->subprotocol_ = std::move(subprotocol); }

    bool setup();
    void loop();
    void dump_config() const;
    void send_text(const std::string &message);

  protected:
    void accept_client_();
    void close_client_();
    void read_client_();
    void handle_http_handshake_();
    void handle_ws_frames_();
    void write_frame_(uint8_t opcode, const std::string &payload);

    bool request_matches_path_(const std::string &uri);
    std::string websocket_accept_key_(const std::string &client_key);

    OcppServerListener *listener_{nullptr};
    std::unique_ptr<socket::ListenSocket> server_;
    std::unique_ptr<socket::Socket> client_;
    uint16_t server_port_{9000};
    std::string server_path_{"/"};
    std::string subprotocol_;
    std::string rx_buffer_;
    std::string connection_id_;
    bool handshake_done_{false};
};

}  // namespace esphome::ocpp
