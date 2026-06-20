#pragma once

#include "message.h"

#include <memory>
#include <string>

namespace esphome::ocpp {

enum class OcppProtocolVersion {
    OCPP_1_6,
    OCPP_2_0_1,
};


class OcppProtocol {
    public:
        bool set_websocket_protocol(const std::string &protocol);
        OcppProtocolVersion get_version() const;
        std::unique_ptr<OcppMessage> parse_message(const std::string &message) const;
        std::string make_change_configuration_request(
            const std::string &unique_id,
            const std::string &key,
            const std::string &value
        ) const;
        std::string make_get_configuration_request(const std::string &unique_id) const;
        std::string make_authorize_response(const std::string &unique_id) const;
        std::string make_boot_notification_response(const std::string &unique_id) const;
        std::string make_heartbeat_response(const std::string &unique_id) const;
        std::string make_meter_values_response(const std::string &unique_id) const;
        std::string make_start_transaction_response(const std::string &unique_id, uint32_t transaction_id) const;
        std::string make_status_notification_response(const std::string &unique_id) const;
        std::string make_stop_transaction_response(const std::string &unique_id) const;
        std::string make_trigger_boot_notification(const std::string &unique_id) const;
        std::string make_trigger_status_notification(const std::string &unique_id) const;
        std::string make_ocpp_error(const std::string &unique_id, const char *code, const char *description) const;

    protected:
        OcppProtocolVersion version_{OcppProtocolVersion::OCPP_1_6};
};

}  // namespace esphome::ocpp
