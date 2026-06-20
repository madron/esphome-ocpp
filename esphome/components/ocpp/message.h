#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace esphome::ocpp {

static constexpr float DEFAULT_PHASE_VOLTAGE = 230.0f;
static constexpr float MIN_PHASE_INFERENCE_VOLTAGE = 190.0f;
static constexpr float MIN_PHASE_INFERENCE_CURRENT = 6.0f;
static constexpr float MIN_PHASE_INFERENCE_POWER = MIN_PHASE_INFERENCE_VOLTAGE * MIN_PHASE_INFERENCE_CURRENT;
static constexpr float MAX_PHASE_INFERENCE_ERROR = 0.4f;

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
            if (!this->append_phase_summary_group_(summary, "Current", this->current_l1, this->current_l2,
                                                   this->current_l3, "A"))
                this->append_summary_group_(summary, "Current", "Current.Import");
            this->append_summary_group_(summary, "Power", "Power.Active.Import");
            this->append_summary_group_(summary, "Energy", "Energy.Active.Import.Register");
            if (!this->append_phase_summary_group_(summary, "Voltage", this->voltage_l1, this->voltage_l2,
                                                   this->voltage_l3, "V"))
                this->append_summary_group_(summary, "Voltage", "Voltage");
            return summary;
        }

        void calculate_phase_values(uint8_t connector_phases, float phase_voltage,
                                    float latched_active_phases = NAN) {
            connector_phases = clamp_phases_(connector_phases);
            this->current_l1 = NAN;
            this->current_l2 = NAN;
            this->current_l3 = NAN;
            this->voltage_l1 = NAN;
            this->voltage_l2 = NAN;
            this->voltage_l3 = NAN;
            this->active_phases = NAN;

            if (!this->calculate_explicit_current_phase_values_(connector_phases)) {
                this->calculate_unqualified_current_phase_values_(connector_phases, phase_voltage, latched_active_phases);
            }
            this->calculate_voltage_phase_values_(connector_phases);
        }

        uint32_t connector_id;
        std::vector<SampledValue> sampled_values;
        float current;
        float power;
        float energy;
        float voltage;
        float current_l1;
        float current_l2;
        float current_l3;
        float voltage_l1;
        float voltage_l2;
        float voltage_l3;
        float active_phases;

    protected:
        static uint8_t clamp_phases_(uint8_t phases) {
            if (phases < 1)
                return 1;
            if (phases > 3)
                return 3;
            return phases;
        }

        static std::string effective_measurand_(const SampledValue &sampled_value) {
            return sampled_value.measurand.empty() ? "Energy.Active.Import.Register" : sampled_value.measurand;
        }

        static uint8_t phase_index_(const std::string &phase) {
            if (phase.rfind("L1", 0) == 0)
                return 1;
            if (phase.rfind("L2", 0) == 0)
                return 2;
            if (phase.rfind("L3", 0) == 0)
                return 3;
            return 0;
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

        static void append_phase_sample_(std::string &values, const char *label, float value, const char *unit) {
            if (std::isnan(value))
                return;
            if (!values.empty())
                values += ", ";
            values += label;
            values += "=";
            values += format_value_(value);
            values += " ";
            values += unit;
        }

        bool append_phase_summary_group_(std::string &summary, const char *label, float l1, float l2, float l3,
                                         const char *unit) const {
            std::string values;
            append_phase_sample_(values, "L1", l1, unit);
            append_phase_sample_(values, "L2", l2, unit);
            append_phase_sample_(values, "L3", l3, unit);
            if (values.empty())
                return false;
            if (!summary.empty())
                summary += " - ";
            summary += label;
            summary += ": ";
            summary += values;
            return true;
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
            this->current_l1 = NAN;
            this->current_l2 = NAN;
            this->current_l3 = NAN;
            this->voltage_l1 = NAN;
            this->voltage_l2 = NAN;
            this->voltage_l3 = NAN;
            this->active_phases = NAN;
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

        bool calculate_explicit_current_phase_values_(uint8_t connector_phases) {
            bool has_explicit_current = false;
            this->current_l1 = connector_phases >= 1 ? 0.0f : NAN;
            this->current_l2 = connector_phases >= 2 ? 0.0f : NAN;
            this->current_l3 = connector_phases >= 3 ? 0.0f : NAN;
            for (const auto &sampled_value : this->sampled_values) {
                if (std::isnan(sampled_value.value) || effective_measurand_(sampled_value) != "Current.Import")
                    continue;
                uint8_t phase_index = phase_index_(sampled_value.phase);
                if (phase_index == 0 || phase_index > connector_phases)
                    continue;
                float value = normalize_current_(sampled_value.value, sampled_value.unit);
                has_explicit_current = true;
                if (phase_index == 1)
                    this->current_l1 = value;
                else if (phase_index == 2)
                    this->current_l2 = value;
                else if (phase_index == 3)
                    this->current_l3 = value;
            }
            if (!has_explicit_current) {
                this->current_l1 = NAN;
                this->current_l2 = NAN;
                this->current_l3 = NAN;
                return false;
            }
            uint8_t active_count = 0;
            if (!std::isnan(this->current_l1) && this->current_l1 >= MIN_PHASE_INFERENCE_CURRENT)
                active_count++;
            if (!std::isnan(this->current_l2) && this->current_l2 >= MIN_PHASE_INFERENCE_CURRENT)
                active_count++;
            if (!std::isnan(this->current_l3) && this->current_l3 >= MIN_PHASE_INFERENCE_CURRENT)
                active_count++;
            this->active_phases = active_count == 0 ? NAN : static_cast<float>(active_count);
            return true;
        }

        float infer_active_phases_(uint8_t connector_phases, float phase_voltage) const {
            if (connector_phases == 1)
                return 1.0f;
            if (std::isnan(this->current) || std::isnan(this->power))
                return NAN;
            float voltage_for_inference = this->voltage;
            if (std::isnan(voltage_for_inference) || voltage_for_inference < MIN_PHASE_INFERENCE_VOLTAGE)
                voltage_for_inference = phase_voltage;
            if (std::isnan(voltage_for_inference) || voltage_for_inference < MIN_PHASE_INFERENCE_VOLTAGE ||
                this->current < MIN_PHASE_INFERENCE_CURRENT || this->power < MIN_PHASE_INFERENCE_POWER)
                return NAN;
            float estimated_phases = this->power / (voltage_for_inference * this->current);
            uint8_t nearest_phases = static_cast<uint8_t>(std::round(estimated_phases));
            if (nearest_phases < 1 || nearest_phases > connector_phases)
                return NAN;
            if (std::fabs(estimated_phases - static_cast<float>(nearest_phases)) > MAX_PHASE_INFERENCE_ERROR)
                return NAN;
            return static_cast<float>(nearest_phases);
        }

        void calculate_unqualified_current_phase_values_(uint8_t connector_phases, float phase_voltage,
                                                        float latched_active_phases) {
            if (std::isnan(this->current))
                return;
            float inferred_active_phases = latched_active_phases;
            if (std::isnan(inferred_active_phases))
                inferred_active_phases = this->infer_active_phases_(connector_phases, phase_voltage);
            this->active_phases = inferred_active_phases;
            uint8_t phases_to_populate = connector_phases;
            if (!std::isnan(inferred_active_phases))
                phases_to_populate = clamp_phases_(static_cast<uint8_t>(std::round(inferred_active_phases)));
            if (phases_to_populate > connector_phases)
                phases_to_populate = connector_phases;

            this->current_l1 = phases_to_populate >= 1 ? this->current : 0.0f;
            this->current_l2 = connector_phases >= 2 ? (phases_to_populate >= 2 ? this->current : 0.0f) : 0.0f;
            this->current_l3 = connector_phases >= 3 ? (phases_to_populate >= 3 ? this->current : 0.0f) : 0.0f;
        }

        void calculate_voltage_phase_values_(uint8_t connector_phases) {
            bool has_voltage_sample = !std::isnan(this->voltage);
            bool has_explicit_voltage = false;
            if (has_voltage_sample) {
                this->voltage_l1 = connector_phases >= 1 ? 0.0f : NAN;
                this->voltage_l2 = connector_phases >= 2 ? 0.0f : 0.0f;
                this->voltage_l3 = connector_phases >= 3 ? 0.0f : 0.0f;
            }
            for (const auto &sampled_value : this->sampled_values) {
                if (std::isnan(sampled_value.value) || effective_measurand_(sampled_value) != "Voltage")
                    continue;
                uint8_t phase_index = phase_index_(sampled_value.phase);
                if (phase_index == 0 || phase_index > connector_phases)
                    continue;
                float value = normalize_voltage_(sampled_value.value, sampled_value.unit);
                has_explicit_voltage = true;
                if (phase_index == 1)
                    this->voltage_l1 = value;
                else if (phase_index == 2)
                    this->voltage_l2 = value;
                else if (phase_index == 3)
                    this->voltage_l3 = value;
            }
            if (has_voltage_sample && !has_explicit_voltage) {
                this->voltage_l1 = connector_phases >= 1 ? this->voltage : NAN;
                this->voltage_l2 = connector_phases >= 2 ? this->voltage : 0.0f;
                this->voltage_l3 = connector_phases >= 3 ? this->voltage : 0.0f;
            }
        }
};

}  // namespace esphome::ocpp
