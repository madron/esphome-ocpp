#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <utility>

namespace esphome::ocpp {

enum class OcppMessageType : uint8_t {
    CALL = 2,
    CALL_RESULT = 3,
    CALL_ERROR = 4,
};

// These classes are an abstraction of real OCPP messages and are intended to be
// common ground across supported OCPP protocol versions.
//
// In practice, they should mostly model the intersection of fields available
// across protocol versions, and likely only the interesting/needed fields.
//
// Avoid relying on fields that exist only in a subset of protocol versions,
// such as BootNotification.reason in OCPP 2.0.1.
//
// Their main use is to help keep charge_point code independent from the OCPP
// protocol version in use.
class OcppMessage {
    public:
        OcppMessage(
            OcppMessageType message_type_id,
            std::string unique_id = "",
            std::string action = ""
        )
            : message_type_id(message_type_id), unique_id(std::move(unique_id)), action(std::move(action)) {}
        virtual ~OcppMessage() = default;

        const OcppMessageType message_type_id;
        std::string unique_id;
        const std::string action;
};

class OcppCall : public OcppMessage {
    public:
        OcppCall(
            std::string action,
            std::string unique_id = ""
        )
            : OcppMessage(
                  OcppMessageType::CALL,
                  std::move(unique_id),
                  std::move(action)
              ) {}
};

class BootNotification : public OcppCall {
    public:
        BootNotification(
            std::string unique_id = "",
            std::string charge_point_model = "",
            std::string charge_point_vendor = "",
            std::string firmware_version = ""
        )
            : OcppCall(
                  "BootNotification",
                  std::move(unique_id)
              ),
              charge_point_model(std::move(charge_point_model)),
              charge_point_vendor(std::move(charge_point_vendor)), firmware_version(std::move(firmware_version)) {}

        std::string charge_point_model;
        std::string charge_point_vendor;
        std::string firmware_version;
};

class GetConfigurationResponse : public OcppMessage {
    public:
        GetConfigurationResponse(
            std::string unique_id = "",
            std::string meter_value_sample_interval = "",
            std::string meter_values_sampled_data = "",
            std::string connector_switch_3_to_1_phase_supported = ""
        )
            : OcppMessage(OcppMessageType::CALL_RESULT, std::move(unique_id), "GetConfiguration"),
              meter_value_sample_interval(std::move(meter_value_sample_interval)),
              meter_values_sampled_data(std::move(meter_values_sampled_data)),
              connector_switch_3_to_1_phase_supported(std::move(connector_switch_3_to_1_phase_supported)) {}

        std::string meter_value_sample_interval;
        std::string meter_values_sampled_data;
        std::string connector_switch_3_to_1_phase_supported;
};

class ChangeConfigurationResponse : public OcppMessage {
    public:
        ChangeConfigurationResponse(
            std::string unique_id = "",
            std::string status = ""
        )
            : OcppMessage(OcppMessageType::CALL_RESULT, std::move(unique_id), "ChangeConfiguration"),
              status(std::move(status)) {}

        std::string status;
};

class StatusNotification : public OcppCall {
    public:
        StatusNotification(
            std::string unique_id = "",
            uint32_t connector_id = 1,
            std::string error_code = "",
            std::string status = ""
        )
            : OcppCall("StatusNotification", std::move(unique_id)), connector_id(connector_id),
              error_code(std::move(error_code)), status(std::move(status)) {}

        uint32_t connector_id;
        std::string error_code;
        std::string status;
};

class MeterValues : public OcppCall {
    public:
        MeterValues(
            std::string unique_id = "",
            uint32_t connector_id = 1,
            float current = NAN,
            float power = NAN,
            float energy = NAN,
            float voltage = NAN
        )
            : OcppCall("MeterValues", std::move(unique_id)), connector_id(connector_id), current(current), power(power),
              energy(energy), voltage(voltage) {}

        uint32_t connector_id;
        float current;
        float power;
        float energy;
        float voltage;
};

}  // namespace esphome::ocpp
