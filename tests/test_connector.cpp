#include "assertions.cpp"
#include "esphome/components/ocpp/connector.h"

#include <cmath>

using esphome::ocpp::Connector;
using esphome::ocpp::CurrentLimit;
using esphome::ocpp::MeterValues;
using esphome::ocpp::RequestedCurrent;
using esphome::ocpp::SampledValue;
using esphome::ocpp::StatusNotification;
using esphome::ocpp::calculate_control_current;
using esphome::binary_sensor::BinarySensor;
using esphome::sensor::Sensor;

class TestCurrentLimit : public CurrentLimit {
 public:
    using CurrentLimit::control;
};

class TestRequestedCurrent : public RequestedCurrent {
 public:
    using RequestedCurrent::control;
};

class TestSessionConnector : public Connector {
    protected:
        void on_session_start() override { this->session_start_count++; }
        void on_session_stop() override { this->session_stop_count++; }

    public:
        uint32_t session_start_count{0};
        uint32_t session_stop_count{0};
};

int main() {
    {
        Connector connector;
        assert_equal("connector_log_meter_values_default", connector.get_log_meter_values(), false);
        connector.set_log_meter_values(true);
        assert_equal("connector_log_meter_values", connector.get_log_meter_values(), true);
    }

    {
        // Connector current numbers clamp to max_current; current_limit is integer, requested_current has 1 decimal
        Connector connector;
        TestCurrentLimit current_limit_number;
        TestRequestedCurrent requested_current_number;
        current_limit_number.set_connector(&connector);
        requested_current_number.set_connector(&connector);
        connector.set_max_current(32);
        connector.set_current_limit_number(&current_limit_number);
        connector.set_requested_current_number(&requested_current_number);

        assert_equal("current_limit_initial", current_limit_number.state, 32.0f);
        assert_equal("requested_current_initial", requested_current_number.state, 32.0f);
        assert_equal("control_current_initial", connector.get_control_current(), 32.0f);

        current_limit_number.control(16.6f);
        assert_equal("current_limit_integer", connector.get_current_limit(), 17.0f);
        assert_equal("current_limit_number_state", current_limit_number.state, 17.0f);
        assert_equal("control_current_tracks_limit", connector.get_control_current(), 17.0f);
        current_limit_number.control(40.0f);
        assert_equal("current_limit_max", connector.get_current_limit(), 32.0f);
        current_limit_number.control(-1.0f);
        assert_equal("current_limit_min", connector.get_current_limit(), 0.0f);

        requested_current_number.control(12.34f);
        assert_equal("requested_current_one_decimal", connector.get_requested_current(), 12.3f);
        assert_equal("requested_current_number_state", requested_current_number.state, 12.3f);
        assert_equal("control_current_respects_limit", connector.get_control_current(), 0.0f);
        requested_current_number.control(99.0f);
        assert_equal("requested_current_max", connector.get_requested_current(), 32.0f);
        assert_equal("control_current_zero_with_zero_limit", connector.get_control_current(), 0.0f);
        requested_current_number.control(-1.0f);
        assert_equal("requested_current_min", connector.get_requested_current(), 0.0f);
    }

    {
        // needed_current stays at 0 A until the connector is charging
        Connector connector;
        Sensor needed_current_l1_sensor;
        Sensor needed_current_l2_sensor;
        Sensor needed_current_l3_sensor;

        connector.set_max_current(32);
        connector.set_needed_current_l1_sensor(&needed_current_l1_sensor);
        connector.set_needed_current_l2_sensor(&needed_current_l2_sensor);
        connector.set_needed_current_l3_sensor(&needed_current_l3_sensor);

        assert_equal("needed_current_l1_initial", connector.get_needed_current_l1(), 0.0f);
        assert_equal("needed_current_l2_initial", connector.get_needed_current_l2(), 0.0f);
        assert_equal("needed_current_l3_initial", connector.get_needed_current_l3(), 0.0f);
        assert_equal("needed_current_l1_sensor_initial", needed_current_l1_sensor.state, 0.0f);
        assert_equal("needed_current_l2_sensor_initial", needed_current_l2_sensor.state, 0.0f);
        assert_equal("needed_current_l3_sensor_initial", needed_current_l3_sensor.state, 0.0f);
    }

    {
        // needed_current follows plugged state and active phase inference using straight L1/L2/L3 mapping
        // TODO: add a dedicated test for custom phase_mapping once update_needed_current_ applies it.
        Connector connector;
        Sensor needed_current_l1_sensor;
        Sensor needed_current_l2_sensor;
        Sensor needed_current_l3_sensor;
        MeterValues one_phase_meter_values(
            "", 1,
            {SampledValue(10.0f, "Current.Import", "A"), SampledValue(2300.0f, "Power.Active.Import", "W"),
             SampledValue(230.0f, "Voltage", "V")});
        connector.set_phases(3);
        connector.set_max_current(32);
        connector.set_needed_current_l1_sensor(&needed_current_l1_sensor);
        connector.set_needed_current_l2_sensor(&needed_current_l2_sensor);
        connector.set_needed_current_l3_sensor(&needed_current_l3_sensor);

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Preparing"));
        assert_equal("needed_current_unknown_active_phases_uses_all_l1", connector.get_needed_current_l1(), 32.0f);
        assert_equal("needed_current_unknown_active_phases_uses_all_l2", connector.get_needed_current_l2(), 32.0f);
        assert_equal("needed_current_unknown_active_phases_uses_all_l3", connector.get_needed_current_l3(), 32.0f);

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Charging"));
        assert_equal("needed_current_unchanged_while_still_plugged_l1", connector.get_needed_current_l1(), 32.0f);
        assert_equal("needed_current_unchanged_while_still_plugged_l2", connector.get_needed_current_l2(), 32.0f);
        assert_equal("needed_current_unchanged_while_still_plugged_l3", connector.get_needed_current_l3(), 32.0f);

        connector.set_current_limit(16.0f);
        assert_equal("needed_current_tracks_current_limit_l1", connector.get_needed_current_l1(), 16.0f);
        assert_equal("needed_current_tracks_current_limit_l2", connector.get_needed_current_l2(), 16.0f);
        assert_equal("needed_current_tracks_current_limit_l3", connector.get_needed_current_l3(), 16.0f);

        connector.publish_meter_values("", one_phase_meter_values);
        assert_equal("needed_current_straight_mapping_single_phase_l1", connector.get_needed_current_l1(), 16.0f);
        assert_equal("needed_current_straight_mapping_single_phase_l2", connector.get_needed_current_l2(), 0.0f);
        assert_equal("needed_current_straight_mapping_single_phase_l3", connector.get_needed_current_l3(), 0.0f);
        assert_equal("needed_current_straight_mapping_sensor_l1", needed_current_l1_sensor.state, 16.0f);
        assert_equal("needed_current_straight_mapping_sensor_l2", needed_current_l2_sensor.state, 0.0f);
        assert_equal("needed_current_straight_mapping_sensor_l3", needed_current_l3_sensor.state, 0.0f);

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "SuspendedEV"));
        assert_equal("needed_current_kept_while_still_plugged_l1", connector.get_needed_current_l1(), 16.0f);
        assert_equal("needed_current_kept_while_still_plugged_l2", connector.get_needed_current_l2(), 0.0f);
        assert_equal("needed_current_kept_while_still_plugged_l3", connector.get_needed_current_l3(), 0.0f);

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Available"));
        assert_equal("needed_current_zero_when_unplugged_l1", connector.get_needed_current_l1(), 0.0f);
        assert_equal("needed_current_zero_when_unplugged_l2", connector.get_needed_current_l2(), 0.0f);
        assert_equal("needed_current_zero_when_unplugged_l3", connector.get_needed_current_l3(), 0.0f);
    }

    {
        // Pure control-current calculation is independent from connector state and publishing
        assert_equal("calculate_control_current_from_request", calculate_control_current(20.0f, 32.0f, 32U), 20.0f);
        assert_equal("calculate_control_current_clamped_by_limit", calculate_control_current(20.0f, 10.0f, 32U), 10.0f);
        assert_equal("calculate_control_current_clamped_by_max", calculate_control_current(20.0f, 32.0f, 16U), 16.0f);
        assert_equal("calculate_control_current_zero_max_unlimited", calculate_control_current(20.0f, 32.0f, 0U), 20.0f);
        assert_equal("calculate_control_current_sub_minimum_disabled", calculate_control_current(4.0f, 32.0f, 32U), 0.0f);
    }

    {
        // control_current is the applied connector current after request/limit clamping and sub-6 A disable logic
        Connector connector;
        Sensor control_current_sensor;
        TestCurrentLimit current_limit_number;
        TestRequestedCurrent requested_current_number;
        connector.set_control_current_sensor(&control_current_sensor);
        current_limit_number.set_connector(&connector);
        requested_current_number.set_connector(&connector);
        connector.set_max_current(32);
        connector.set_current_limit_number(&current_limit_number);
        connector.set_requested_current_number(&requested_current_number);

        assert_equal("control_current_sensor_initial", control_current_sensor.state, 32.0f);
        assert_equal("control_current_startup_uses_limit", connector.get_control_current(), 32.0f);

        requested_current_number.control(20.0f);
        assert_equal("control_current_from_request", connector.get_control_current(), 20.0f);
        assert_equal("control_current_sensor_from_request", control_current_sensor.state, 20.0f);

        current_limit_number.control(10.0f);
        assert_equal("control_current_clamped_by_limit", connector.get_control_current(), 10.0f);
        assert_equal("control_current_sensor_clamped_by_limit", control_current_sensor.state, 10.0f);

        requested_current_number.control(4.0f);
        assert_equal("control_current_sub_minimum_disabled", connector.get_control_current(), 0.0f);
        assert_equal("control_current_sensor_sub_minimum_disabled", control_current_sensor.state, 0.0f);
    }

    {
        // Connector phase mapping stores connector-pin to supply-phase order
        Connector connector;
        connector.set_phases(3);
        connector.set_phase_mapping({2, 3, 1});

        assert_equal("phase_mapping_l1", connector.get_phase_mapping(1), static_cast<uint8_t>(2));
        assert_equal("phase_mapping_l2", connector.get_phase_mapping(2), static_cast<uint8_t>(3));
        assert_equal("phase_mapping_l3", connector.get_phase_mapping(3), static_cast<uint8_t>(1));
        assert_equal("phase_mapping_invalid", connector.get_phase_mapping(4), static_cast<uint8_t>(0));

    }

    {
        // current_limit can use a lower connector-specific max while requested_current still uses max_current
        Connector connector;
        TestCurrentLimit current_limit_number;
        TestRequestedCurrent requested_current_number;
        current_limit_number.set_connector(&connector);
        requested_current_number.set_connector(&connector);
        connector.set_max_current(32);
        connector.set_current_limit_max(16);
        connector.set_current_limit_number(&current_limit_number);
        connector.set_requested_current_number(&requested_current_number);

        assert_equal("current_limit_override_max", connector.get_current_limit_max(), 16U);
        assert_equal("current_limit_override_initial", current_limit_number.state, 16.0f);
        assert_equal("requested_current_override_initial", requested_current_number.state, 32.0f);
        assert_equal("control_current_override_initial", connector.get_control_current(), 16.0f);
        current_limit_number.control(20.0f);
        assert_equal("current_limit_override_clamp", connector.get_current_limit(), 16.0f);
        requested_current_number.control(20.0f);
        assert_equal("requested_current_ignores_limit_override", connector.get_requested_current(), 20.0f);
    }

    {
        // Plugged binary sensor starts OFF when configured before any StatusNotification
        Connector connector;
        BinarySensor plugged_sensor;
        plugged_sensor.publish_state(true);
        connector.set_plugged_binary_sensor(&plugged_sensor);
        assert_equal("plugged_sensor_has_initial_state_when_configured", plugged_sensor.has_state, true);
        assert_equal("plugged_sensor_off_when_configured", plugged_sensor.state, false);
    }

    {
        // StatusNotification keeps internal plugged state even without a configured sensor
        Connector connector_without_sensor;
        assert_equal("connector_without_sensor_initial_plugged", connector_without_sensor.is_plugged(), false);
        connector_without_sensor.publish_status_notification(StatusNotification("", 1, "NoError", "Preparing"));
        assert_equal("connector_without_sensor_preparing_plugged", connector_without_sensor.is_plugged(), true);
        connector_without_sensor.publish_status_notification(StatusNotification("", 1, "GroundFailure", "Faulted"));
        assert_equal("connector_without_sensor_faulted_unplugged", connector_without_sensor.is_plugged(), false);
        connector_without_sensor.publish_status_notification(StatusNotification("", 1, "NoError", "Available"));
        assert_equal("connector_without_sensor_available_unplugged", connector_without_sensor.is_plugged(), false);
    }

    {
        // A session starts only when plugged changes from false to true and stops only when it changes back to false
        TestSessionConnector connector;
        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Available"));
        assert_equal("session_no_start_when_initially_available", connector.session_start_count, 0U);
        assert_equal("session_no_stop_when_initially_available", connector.session_stop_count, 0U);

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Preparing"));
        assert_equal("session_start_on_plugged", connector.session_start_count, 1U);
        assert_equal("session_stop_not_called_on_plugged", connector.session_stop_count, 0U);
        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Charging"));
        assert_equal("session_start_not_repeated_while_plugged", connector.session_start_count, 1U);

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Available"));
        assert_equal("session_start_count_after_unplugged", connector.session_start_count, 1U);
        assert_equal("session_stop_on_unplugged", connector.session_stop_count, 1U);
    }

    {
        // Session energy and time reset on plug-in and keep the last session values after unplugging
        Connector connector;
        Sensor total_energy_sensor;
        Sensor session_energy_sensor;
        Sensor session_time_sensor;
        connector.set_total_energy_sensor(&total_energy_sensor);
        connector.set_session_energy_sensor(&session_energy_sensor);
        connector.set_session_time_sensor(&session_time_sensor);

        connector.publish_meter_values("", MeterValues("", 1, {SampledValue(10.0f, "Energy.Active.Import.Register", "kWh")}));
        assert_equal("session_total_energy_before_start", total_energy_sensor.state, 10.0f);
        assert_equal("session_energy_unknown_before_start", std::isnan(session_energy_sensor.state), true);

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Preparing"), 10000);
        assert_equal("session_energy_reset_on_start", session_energy_sensor.state, 0.0f);
        assert_equal("session_time_reset_on_start", session_time_sensor.state, 0.0f);
        connector.loop(10999);
        assert_equal("session_time_not_updated_within_same_second", session_time_sensor.state, 0.0f);
        connector.loop(12000);
        assert_equal("session_time_updates_while_plugged", session_time_sensor.state, 2.0f);
        connector.loop(12999);
        assert_equal("session_time_not_updated_twice_in_same_second", session_time_sensor.state, 2.0f);

        connector.publish_meter_values("", MeterValues("", 1, {SampledValue(10.75f, "Energy.Active.Import.Register", "kWh")}));
        assert_equal("session_energy_from_total_delta", session_energy_sensor.state, 0.75f);
        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Charging"), 15000);
        assert_equal("session_energy_not_reset_while_still_plugged", session_energy_sensor.state, 0.75f);

        connector.publish_meter_values("", MeterValues("", 1, {SampledValue(11.0f, "Energy.Active.Import.Register", "kWh")}));
        assert_equal("session_energy_before_stop", session_energy_sensor.state, 1.0f);
        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Available"), 70000);
        assert_equal("session_time_on_stop", session_time_sensor.state, 60.0f);
        connector.loop(90000);
        assert_equal("session_time_stops_after_unplugged", session_time_sensor.state, 60.0f);
        connector.publish_meter_values("", MeterValues("", 1, {SampledValue(12.0f, "Energy.Active.Import.Register", "kWh")}));
        assert_equal("session_total_energy_after_stop_updates", total_energy_sensor.state, 12.0f);
        assert_equal("session_energy_after_stop_kept", session_energy_sensor.state, 1.0f);
    }

    {
        // Active phase inference is bound to plugged/unplugged transitions, not transaction lifetime
        Connector connector;
        Sensor active_phases_sensor;
        connector.set_phases(3);
        connector.set_active_phases_sensor(&active_phases_sensor);
        MeterValues one_phase_meter_values(
            "", 1,
            {SampledValue(10.0f, "Current.Import", "A"), SampledValue(2300.0f, "Power.Active.Import", "W"),
             SampledValue(230.0f, "Voltage", "V")});

        connector.publish_meter_values("", one_phase_meter_values);
        assert_equal("active_phases_not_latched_before_plugged", connector.get_active_phases(), static_cast<uint8_t>(0));

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Preparing"));
        connector.publish_meter_values("", one_phase_meter_values);
        assert_equal("active_phases_latched_after_plugged", connector.get_active_phases(), static_cast<uint8_t>(1));
        connector.set_active_transaction_id(1);
        assert_equal("active_phases_kept_after_start_transaction", connector.get_active_phases(), static_cast<uint8_t>(1));
        connector.clear_active_transaction();
        assert_equal("active_phases_kept_after_stop_transaction", connector.get_active_phases(), static_cast<uint8_t>(1));

        connector.publish_status_notification(StatusNotification("", 1, "NoError", "Available"));
        assert_equal("active_phases_reset_after_unplugged", connector.get_active_phases(), static_cast<uint8_t>(0));
        assert_equal("active_phases_sensor_reset_after_unplugged", std::isnan(active_phases_sensor.state), true);
    }

    return 0;
}
