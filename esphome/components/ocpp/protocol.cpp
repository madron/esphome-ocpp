#include "protocol.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdlib>
#include <memory>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static constexpr const char *CURRENT_TIME = "1970-01-01T00:00:00Z";
static const char *const CHANGE_CONFIGURATION_UNIQUE_ID_PREFIX = "change-config-";
static const char *const GET_CONFIGURATION_UNIQUE_ID = "get-configuration";
static const char *const GET_CONFIGURATION_KEY_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED =
    "ConnectorSwitch3to1PhaseSupported";
static const char *const GET_CONFIGURATION_KEY_METER_VALUE_SAMPLE_INTERVAL = "MeterValueSampleInterval";
static const char *const GET_CONFIGURATION_KEY_METER_VALUES_SAMPLED_DATA = "MeterValuesSampledData";

std::string json_escape(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

bool parse_message_type_id(int raw_message_type_id, OcppMessageType *message_type_id) {
    if (raw_message_type_id == 2) {
        *message_type_id = OcppMessageType::CALL;
        return true;
    }
    if (raw_message_type_id == 3) {
        *message_type_id = OcppMessageType::CALL_RESULT;
        return true;
    }
    if (raw_message_type_id == 4) {
        *message_type_id = OcppMessageType::CALL_ERROR;
        return true;
    }
    return false;
}

std::string json_string_or_empty(const JsonVariant &value) {
    if (!value.is<const char *>())
        return "";
    return value.as<std::string>();
}

float json_float_or_nan(const JsonVariant &value) {
    if (value.is<float>())
        return value.as<float>();
    if (value.is<double>())
        return static_cast<float>(value.as<double>());
    if (value.is<int>())
        return static_cast<float>(value.as<int>());
    if (!value.is<const char *>())
        return NAN;

    const char *raw = value.as<const char *>();
    char *end = nullptr;
    float parsed = std::strtof(raw, &end);
    if (end == raw || *end != '\0')
        return NAN;
    return parsed;
}

uint32_t json_uint_or_default(const JsonVariant &value, uint32_t default_value) {
    if (!value.is<int>())
        return default_value;
    int raw = value.as<int>();
    if (raw < 0)
        return default_value;
    return static_cast<uint32_t>(raw);
}

float apply_metric_multiplier(float value, const JsonObject &sampled_value) {
    JsonVariant unit_of_measure = sampled_value["unitOfMeasure"];
    if (!unit_of_measure.is<JsonObject>())
        return value;
    JsonVariant multiplier = unit_of_measure.as<JsonObject>()["multiplier"];
    if (!multiplier.is<int>())
        return value;
    return value * std::pow(10.0f, static_cast<float>(multiplier.as<int>()));
}

std::string sampled_value_unit(const JsonObject &sampled_value) {
    std::string unit = json_string_or_empty(sampled_value["unit"]);
    if (!unit.empty())
        return unit;

    JsonVariant unit_of_measure = sampled_value["unitOfMeasure"];
    if (!unit_of_measure.is<JsonObject>())
        return "";
    return json_string_or_empty(unit_of_measure.as<JsonObject>()["unit"]);
}

float normalize_current(float value, const std::string &unit) {
    if (unit == "mA")
        return value / 1000.0f;
    return value;
}

float normalize_power(float value, const std::string &unit) {
    if (unit == "kW")
        return value * 1000.0f;
    return value;
}

float normalize_energy(float value, const std::string &unit) {
    if (unit == "kWh")
        return value;
    if (unit == "MWh")
        return value * 1000.0f;
    return value / 1000.0f;
}

float normalize_voltage(float value, const std::string &unit) {
    if (unit == "mV")
        return value / 1000.0f;
    if (unit == "kV")
        return value * 1000.0f;
    return value;
}

bool is_change_configuration_unique_id(const std::string &unique_id) {
    return unique_id.rfind(CHANGE_CONFIGURATION_UNIQUE_ID_PREFIX, 0) == 0;
}

std::unique_ptr<OcppMessage> parse_boot_notification_1_6(const std::string &unique_id, const JsonObject &payload) {
    return std::unique_ptr<OcppMessage>(new BootNotification(
        unique_id,
        json_string_or_empty(payload["chargePointModel"]),
        json_string_or_empty(payload["chargePointVendor"]),
        json_string_or_empty(payload["firmwareVersion"])
    ));
}

std::unique_ptr<OcppMessage> parse_boot_notification_2_0_1(const std::string &unique_id, const JsonObject &payload) {
    std::string charge_point_model;
    std::string charge_point_vendor;
    std::string firmware_version;
    JsonVariant charging_station = payload["chargingStation"];
    if (charging_station.is<JsonObject>()) {
        JsonObject charging_station_object = charging_station.as<JsonObject>();
        charge_point_model = json_string_or_empty(charging_station_object["model"]);
        charge_point_vendor = json_string_or_empty(charging_station_object["vendorName"]);
        firmware_version = json_string_or_empty(charging_station_object["firmwareVersion"]);
    }
    return std::unique_ptr<OcppMessage>(new BootNotification(
        unique_id,
        charge_point_model,
        charge_point_vendor,
        firmware_version
    ));
}

std::unique_ptr<OcppMessage> parse_get_configuration_response(const std::string &unique_id, const JsonObject &payload) {
    std::string meter_value_sample_interval;
    std::string meter_values_sampled_data;
    std::string connector_switch_3_to_1_phase_supported;

    JsonVariant configuration_key = payload["configurationKey"];
    if (configuration_key.is<JsonArray>()) {
        for (JsonVariant item_variant : configuration_key.as<JsonArray>()) {
            if (!item_variant.is<JsonObject>())
                continue;
            JsonObject item = item_variant.as<JsonObject>();
            std::string key = json_string_or_empty(item["key"]);
            std::string value = json_string_or_empty(item["value"]);
            if (key == GET_CONFIGURATION_KEY_METER_VALUE_SAMPLE_INTERVAL)
                meter_value_sample_interval = std::move(value);
            else if (key == GET_CONFIGURATION_KEY_METER_VALUES_SAMPLED_DATA)
                meter_values_sampled_data = std::move(value);
            else if (key == GET_CONFIGURATION_KEY_CONNECTOR_SWITCH_3_TO_1_PHASE_SUPPORTED)
                connector_switch_3_to_1_phase_supported = std::move(value);
        }
    }

    return std::unique_ptr<OcppMessage>(new GetConfigurationResponse(
        unique_id,
        meter_value_sample_interval,
        meter_values_sampled_data,
        connector_switch_3_to_1_phase_supported
    ));
}

std::unique_ptr<OcppMessage> parse_change_configuration_response(const std::string &unique_id, const JsonObject &payload) {
    return std::unique_ptr<OcppMessage>(new ChangeConfigurationResponse(unique_id, json_string_or_empty(payload["status"])));
}

std::unique_ptr<OcppMessage> parse_status_notification(const std::string &unique_id, const JsonObject &payload) {
    uint32_t connector_id = json_uint_or_default(payload["connectorId"], 1);
    std::string error_code = json_string_or_empty(payload["errorCode"]);
    std::string status = json_string_or_empty(payload["status"]);
    if (status.empty())
        status = json_string_or_empty(payload["connectorStatus"]);

    return std::unique_ptr<OcppMessage>(new StatusNotification(unique_id, connector_id, error_code, status));
}

std::unique_ptr<OcppMessage> parse_meter_values(const std::string &unique_id, const JsonObject &payload) {
    uint32_t connector_id = json_uint_or_default(payload["connectorId"], 1);
    float current = NAN;
    float power = NAN;
    float energy = NAN;
    float voltage = NAN;

    JsonVariant meter_value = payload["meterValue"];
    if (meter_value.is<JsonArray>()) {
        for (JsonVariant meter_value_variant : meter_value.as<JsonArray>()) {
            if (!meter_value_variant.is<JsonObject>())
                continue;
            JsonVariant sampled_value = meter_value_variant.as<JsonObject>()["sampledValue"];
            if (!sampled_value.is<JsonArray>())
                continue;
            for (JsonVariant sampled_value_variant : sampled_value.as<JsonArray>()) {
                if (!sampled_value_variant.is<JsonObject>())
                    continue;
                JsonObject sampled_value_object = sampled_value_variant.as<JsonObject>();
                float raw_value = json_float_or_nan(sampled_value_object["value"]);
                if (std::isnan(raw_value))
                    continue;

                std::string measurand = json_string_or_empty(sampled_value_object["measurand"]);
                if (measurand.empty())
                    measurand = "Energy.Active.Import.Register";
                std::string unit = sampled_value_unit(sampled_value_object);
                float value = apply_metric_multiplier(raw_value, sampled_value_object);

                if (measurand == "Current.Import")
                    current = normalize_current(value, unit);
                else if (measurand == "Power.Active.Import")
                    power = normalize_power(value, unit);
                else if (measurand == "Energy.Active.Import.Register")
                    energy = normalize_energy(value, unit);
                else if (measurand == "Voltage")
                    voltage = normalize_voltage(value, unit);
            }
        }
    }

    return std::unique_ptr<OcppMessage>(new MeterValues(unique_id, connector_id, current, power, energy, voltage));
}

}  // namespace

bool OcppProtocol::set_websocket_protocol(const std::string &protocol) {
    if (protocol.empty() || protocol == "ocpp1.6") {
        this->version_ = OcppProtocolVersion::OCPP_1_6;
        return true;
    }
    if (protocol == "ocpp2.0.1") {
        this->version_ = OcppProtocolVersion::OCPP_2_0_1;
        return true;
    }
    return false;
}

OcppProtocolVersion OcppProtocol::get_version() const { return this->version_; }

std::string OcppProtocol::make_change_configuration_request(
    const std::string &unique_id,
    const std::string &key,
    const std::string &value
) const {
    if (this->version_ != OcppProtocolVersion::OCPP_1_6)
        return "";
    return "[2,\"" + json_escape(unique_id) + "\",\"ChangeConfiguration\",{\"key\":\"" +
           json_escape(key) + "\",\"value\":\"" + json_escape(value) + "\"}]";
}

std::unique_ptr<OcppMessage> OcppProtocol::parse_message(const std::string &message) const {
    JsonDocument document = json::parse_json(message);
    if (document.overflowed() || document.isNull() || !document.is<JsonArray>()) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
        return nullptr;
    }

    JsonArray frame = document.as<JsonArray>();
    if (frame.size() < 2 || !frame[static_cast<size_t>(0)].is<int>() || !frame[static_cast<size_t>(1)].is<const char *>()) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP frame: %s", message.c_str());
        return nullptr;
    }

    OcppMessageType message_type_id{OcppMessageType::CALL};
    if (!parse_message_type_id(frame[static_cast<size_t>(0)].as<int>(), &message_type_id)) {
        ESP_LOGW(TAG, "Ignoring OCPP frame with unsupported MessageTypeId: %s", message.c_str());
        return nullptr;
    }

    std::string unique_id = frame[static_cast<size_t>(1)].as<std::string>();
    if (message_type_id != OcppMessageType::CALL) {
        if (message_type_id == OcppMessageType::CALL_RESULT && unique_id == GET_CONFIGURATION_UNIQUE_ID &&
            frame.size() >= 3 && frame[static_cast<size_t>(2)].is<JsonObject>())
            return parse_get_configuration_response(unique_id, frame[static_cast<size_t>(2)].as<JsonObject>());
        if (message_type_id == OcppMessageType::CALL_RESULT && is_change_configuration_unique_id(unique_id) &&
            frame.size() >= 3 && frame[static_cast<size_t>(2)].is<JsonObject>())
            return parse_change_configuration_response(unique_id, frame[static_cast<size_t>(2)].as<JsonObject>());
        return std::unique_ptr<OcppMessage>(new OcppMessage(message_type_id, unique_id));
    }

    if (frame.size() < 4 || !frame[static_cast<size_t>(2)].is<const char *>() || !frame[static_cast<size_t>(3)].is<JsonObject>()) {
        ESP_LOGW(TAG, "Ignoring invalid OCPP CALL frame: %s", message.c_str());
        return nullptr;
    }

    std::string action = frame[static_cast<size_t>(2)].as<std::string>();
    JsonObject payload = frame[static_cast<size_t>(3)].as<JsonObject>();

    if (action == "BootNotification") {
        if (this->version_ == OcppProtocolVersion::OCPP_2_0_1)
            return parse_boot_notification_2_0_1(unique_id, payload);
        return parse_boot_notification_1_6(unique_id, payload);
    }
    if (action == "StatusNotification")
        return parse_status_notification(unique_id, payload);
    if (action == "MeterValues")
        return parse_meter_values(unique_id, payload);

    return std::unique_ptr<OcppMessage>(new OcppCall(action, unique_id));
}

std::string OcppProtocol::make_get_configuration_request(const std::string &unique_id) const {
    if (this->version_ != OcppProtocolVersion::OCPP_1_6)
        return "";
    // Request only values used by the component. Other useful diagnostics seen
    // during charger commissioning, but intentionally not requested by default:
    // StopTxnSampledData, ConnectorPhaseRotation, NumberOfConnectors,
    // SupportedFeatureProfiles, ChargingScheduleAllowedChargingRateUnit,
    // ChargingScheduleMaxPeriods.
    return "[2,\"" + json_escape(unique_id) + "\",\"GetConfiguration\",{\"key\":[\"MeterValueSampleInterval\","
           "\"MeterValuesSampledData\",\"ConnectorSwitch3to1PhaseSupported\"]}]";
}

std::string OcppProtocol::make_boot_notification_response(const std::string &unique_id) const {
    return "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME +
           "\",\"interval\":300,\"status\":\"Accepted\"}]";
}

std::string OcppProtocol::make_heartbeat_response(const std::string &unique_id) const {
    return "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME + "\"}]";
}

std::string OcppProtocol::make_meter_values_response(const std::string &unique_id) const {
    return "[3,\"" + json_escape(unique_id) + "\",{}]";
}

std::string OcppProtocol::make_status_notification_response(const std::string &unique_id) const {
    return "[3,\"" + json_escape(unique_id) + "\",{}]";
}

std::string OcppProtocol::make_trigger_boot_notification(const std::string &unique_id) const {
    return "[2,\"" + json_escape(unique_id) + "\",\"TriggerMessage\",{\"requestedMessage\":\"BootNotification\"}]";
}

std::string OcppProtocol::make_trigger_status_notification(const std::string &unique_id) const {
    return "[2,\"" + json_escape(unique_id) + "\",\"TriggerMessage\",{\"requestedMessage\":\"StatusNotification\"}]";
}

std::string OcppProtocol::make_ocpp_error(const std::string &unique_id, const char *code, const char *description) const {
    return "[4,\"" + json_escape(unique_id) + "\",\"" + code + "\",\"" + description + "\",{}]";
}

}  // namespace esphome::ocpp
