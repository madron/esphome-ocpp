#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

class Authorize : public OcppCall {
    public:
        Authorize(
            std::string unique_id = "",
            std::string id_tag = ""
        )
            : OcppCall(
                  "Authorize",
                  std::move(unique_id)
              ),
              id_tag(std::move(id_tag)) {}

        std::string id_tag;
};

class StartTransaction : public OcppCall {
    public:
        StartTransaction(
            std::string unique_id = "",
            uint32_t connector_id = 1,
            std::string id_tag = ""
        )
            : OcppCall(
                  "StartTransaction",
                  std::move(unique_id)
              ),
              connector_id(connector_id), id_tag(std::move(id_tag)) {}

        uint32_t connector_id;
        std::string id_tag;
};

class StopTransaction : public OcppCall {
    public:
        StopTransaction(
            std::string unique_id = "",
            uint32_t transaction_id = 0
        )
            : OcppCall(
                  "StopTransaction",
                  std::move(unique_id)
              ),
              transaction_id(transaction_id) {}

        uint32_t transaction_id;
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

struct SampledValue {
    SampledValue(
        float value = NAN,
        std::string measurand = "",
        std::string unit = "",
        std::string phase = ""
    )
        : value(value), measurand(std::move(measurand)), unit(std::move(unit)), phase(std::move(phase)) {}

    float value;
    std::string measurand;
    std::string unit;
    std::string phase;
};

class MeterValues : public OcppCall {
    public:
        MeterValues(
            std::string unique_id = "",
            uint32_t connector_id = 1,
            std::vector<SampledValue> sampled_values = {}
        )
            : OcppCall("MeterValues", std::move(unique_id)), connector_id(connector_id),
              sampled_values(std::move(sampled_values)) {
            this->calculate_values_();
        }

        std::string sampled_values_summary() const {
            std::string summary;
            this->append_summary_group_(summary, "Current", "Current.Import");
            this->append_summary_group_(summary, "Power", "Power.Active.Import");
            this->append_summary_group_(summary, "Energy", "Energy.Active.Import.Register");
            this->append_summary_group_(summary, "Voltage", "Voltage");
            return summary;
        }

        uint32_t connector_id;
        std::vector<SampledValue> sampled_values;
        float current;
        float power;
        float energy;
        float voltage;

    protected:
        static std::string effective_measurand_(const SampledValue &sampled_value) {
            return sampled_value.measurand.empty() ? "Energy.Active.Import.Register" : sampled_value.measurand;
        }

        static float normalize_current_(float value, const std::string &unit) {
            if (unit == "mA")
                return value / 1000.0f;
            return value;
        }

        static float normalize_power_(float value, const std::string &unit) {
            if (unit == "kW")
                return value * 1000.0f;
            return value;
        }

        static float normalize_energy_(float value, const std::string &unit) {
            if (unit == "kWh")
                return value;
            if (unit == "MWh")
                return value * 1000.0f;
            return value / 1000.0f;
        }

        static float normalize_voltage_(float value, const std::string &unit) {
            if (unit == "mV")
                return value / 1000.0f;
            if (unit == "kV")
                return value * 1000.0f;
            return value;
        }

        static std::string format_value_(float value) {
            char buffer[24];
            if (std::fabs(value - std::round(value)) < 0.001f)
                std::snprintf(buffer, sizeof(buffer), "%.0f", value);
            else
                std::snprintf(buffer, sizeof(buffer), "%.3f", value);
            std::string formatted(buffer);
            if (formatted.find('.') != std::string::npos) {
                while (formatted.size() > 1 && formatted.back() == '0')
                    formatted.pop_back();
                if (!formatted.empty() && formatted.back() == '.')
                    formatted.pop_back();
            }
            return formatted;
        }

        static std::string format_sample_(const SampledValue &sampled_value) {
            std::string formatted;
            if (!sampled_value.phase.empty())
                formatted += sampled_value.phase + "=";
            formatted += format_value_(sampled_value.value);
            if (!sampled_value.unit.empty())
                formatted += " " + sampled_value.unit;
            return formatted;
        }

        void append_summary_group_(std::string &summary, const char *label, const char *measurand) const {
            std::string values;
            for (const auto &sampled_value : this->sampled_values) {
                if (std::isnan(sampled_value.value))
                    continue;
                if (effective_measurand_(sampled_value) != measurand)
                    continue;
                if (!values.empty())
                    values += ", ";
                values += format_sample_(sampled_value);
            }
            if (values.empty())
                return;
            if (!summary.empty())
                summary += " - ";
            summary += label;
            summary += ": ";
            summary += values;
        }

        void calculate_values_() {
            this->current = NAN;
            this->power = NAN;
            this->energy = NAN;
            this->voltage = NAN;
            for (const auto &sampled_value : this->sampled_values) {
                if (std::isnan(sampled_value.value))
                    continue;
                std::string measurand = effective_measurand_(sampled_value);
                if (measurand == "Current.Import")
                    this->current = normalize_current_(sampled_value.value, sampled_value.unit);
                else if (measurand == "Power.Active.Import")
                    this->power = normalize_power_(sampled_value.value, sampled_value.unit);
                else if (measurand == "Energy.Active.Import.Register")
                    this->energy = normalize_energy_(sampled_value.value, sampled_value.unit);
                else if (measurand == "Voltage")
                    this->voltage = normalize_voltage_(sampled_value.value, sampled_value.unit);
            }
        }
};

}  // namespace esphome::ocpp
