#pragma once

#include "esphome/components/socket/socket.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace esphome::ocpp {

const std::vector<std::string> &supported_ocpp_protocols();
std::string select_supported_protocol(const std::string &client_protocols, const std::string &forced_protocol = "",
                                      std::string *reject_reason = nullptr);

class OcppServerListener {
    public:
        virtual ~OcppServerListener() = default;

        virtual std::string select_websocket_protocol(const std::string &connection_id,
                                                      const std::string &client_protocols,
                                                      std::string *reject_reason) = 0;
        virtual void on_websocket_connected(const std::string &connection_id, const std::string &protocol) = 0;
        virtual void on_websocket_disconnected(const std::string &connection_id) = 0;
        virtual void on_websocket_text(const std::string &connection_id, const std::string &message) = 0;
};

class OcppServer {
    public:
        void set_listener(OcppServerListener *listener) { this->listener_ = listener; }
        void set_port(uint16_t port) { this->server_port_ = port; }
        void set_max_clients(size_t max_clients) { this->max_clients_ = max_clients; }
        void set_path(std::string path);

        bool setup();
        void loop();
        void send_text(const std::string &connection_id, const std::string &message);
        std::string get_charger_url() const;

    protected:
        struct ClientSession {
            std::unique_ptr<socket::Socket> socket;
            std::string rx_buffer;
            std::string connection_id;
            bool handshake_done{false};
        };
        using ClientSessions = std::list<ClientSession>;

        void accept_client_();
        void close_client_(ClientSessions::iterator client);
        void read_client_(ClientSessions::iterator client);
        bool handle_http_handshake_(ClientSessions::iterator client);
        bool handle_ws_frames_(ClientSessions::iterator client);
        void write_frame_(ClientSession *client, uint8_t opcode, const std::string &payload);

        bool request_matches_path_(const std::string &uri, std::string *connection_id) const;
        std::string websocket_accept_key_(const std::string &client_key);
        bool has_connection_id_(const std::string &connection_id, const ClientSession *except) const;

        OcppServerListener *listener_{nullptr};
        std::unique_ptr<socket::ListenSocket> server_;
        ClientSessions clients_;
        size_t max_clients_{1};
        uint16_t server_port_{9000};
        std::string server_path_{"/"};
};

}  // namespace esphome::ocpp
