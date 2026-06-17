#pragma once

#include "charge_point.h"
#include "protocol.h"
#include "server.h"
#include "esphome/core/component.h"

#include <string>
#include <utility>
#include <vector>

namespace esphome::ocpp {

class OcppComponent : public Component, public OcppServerListener {
    public:
        void setup() override;
        void loop() override;
        void dump_config() override;
        float get_setup_priority() const override;

        // server
        void set_server_port(uint16_t port) { this->server_.set_port(port); }
        void set_server_path(std::string path) { this->server_.set_path(std::move(path)); }

        void add_charge_point(ChargePoint *charge_point) {
            this->charge_points_.push_back(charge_point);
        }

    protected:
        void on_websocket_connected(const std::string &connection_id) override;
        void on_websocket_disconnected(const std::string &connection_id) override;
        void on_websocket_text(const std::string &connection_id, const std::string &message) override;
        bool send_queued_message_();
        ChargePoint *find_charge_point_by_connection_id_(const std::string &connection_id) const;
        ChargePoint *assign_charge_point_for_connection_(const std::string &connection_id) const;

        OcppServer server_;
        std::vector<ChargePoint *> charge_points_;
        size_t next_outbound_charge_point_{0};

};

}  // namespace esphome::ocpp
