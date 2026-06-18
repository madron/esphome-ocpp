#include "assertions.cpp"
#include "esphome/components/ocpp/charge_point.h"

#include <string>
#include <vector>

using esphome::ocpp::ChargePoint;
using esphome::binary_sensor::BinarySensor;
using esphome::text_sensor::TextSensor;

class TestChargePoint : public ChargePoint {
 public:
    TestChargePoint() {
        this->set_online_binary_sensor(&this->online_sensor);
        this->set_protocol_text_sensor(&this->protocol_sensor);
        this->set_charger_info_text_sensor(&this->charger_info_sensor);
    }

    TestChargePoint(const TestChargePoint &) = delete;
    TestChargePoint &operator=(const TestChargePoint &) = delete;

    std::vector<std::string> &messages{this->messages_};
    BinarySensor online_sensor;
    TextSensor protocol_sensor;
    TextSensor charger_info_sensor;
};

int main() {
    {
        TestChargePoint charge_point;

        assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), "");
        assert_equal("get_connection_id", charge_point.get_connection_id(), "");

        // set_charge_point_id also initializes the connection_id
        charge_point.set_charge_point_id("A99999");
        assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), "A99999");
        assert_equal("set_charge_point_id_connection_id", charge_point.get_connection_id(), "A99999");

        // set_connection_id
        charge_point.set_connection_id("B11111");
        assert_equal("set_connection_id", charge_point.get_connection_id(), "B11111");
        assert_equal("set_connection_id_keeps_charge_point_id", charge_point.get_charge_point_id(), "A99999");

        // get_debug_ocpp_messages
        assert_equal("get_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), false);
        assert_equal("get_force_boot_notification", charge_point.get_force_boot_notification(), false);
        assert_equal("get_max_queued_messages", charge_point.get_max_queued_messages(), 8);
        assert_equal("get_force_protocol", charge_point.get_force_protocol(), std::string(""));
        assert_equal("protocol_default", charge_point.protocol_sensor.state, std::string(""));
        assert_equal("charger_info_default", charge_point.charger_info_sensor.state, std::string(""));

        // set_debug_ocpp_messages
        charge_point.set_debug_ocpp_messages(true);
        assert_equal("set_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), true);

        // set_force_boot_notification
        charge_point.set_force_boot_notification(true);
        assert_equal("set_force_boot_notification", charge_point.get_force_boot_notification(), true);

        // set_max_queued_messages
        charge_point.set_max_queued_messages(16);
        assert_equal("set_max_queued_messages", charge_point.get_max_queued_messages(), 16);

        // set_force_protocol
        charge_point.set_force_protocol("ocpp1.6");
        assert_equal("set_force_protocol", charge_point.get_force_protocol(), std::string("ocpp1.6"));
    }

    {
        // Protocol sensor is published on connect and cleared on disconnect
        TestChargePoint charge_point;
        charge_point.on_connected("A99999", "ocpp1.6");
        assert_equal("protocol_after_connect", charge_point.protocol_sensor.state,
                     std::string("ocpp1.6"));
        charge_point.on_disconnected();
        assert_equal("protocol_after_disconnect", charge_point.protocol_sensor.state, std::string(""));
    }

    {
        // BootNotification updates online state and emits a response
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("online_before_boot", charge_point.is_online(), false);
        assert_equal("online_sensor_before_boot", charge_point.online_sensor.state, false);
        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("online_after_boot", charge_point.is_online(), true);
        assert_equal("online_sensor_after boot", charge_point.online_sensor.state, true);
        assert_equal("charger_info_after_boot", charge_point.charger_info_sensor.state,
                     std::string("vendor: Acme, model: Wallbox"));
        assert_equal("boot_response_count", charge_point.messages.size(), 1);
        assert_equal("boot_response", charge_point.messages[0], R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");

        charge_point.on_disconnected();
        assert_equal("online_after_disconnect", charge_point.is_online(), false);
        assert_equal("online_sensor_after_disconnect", charge_point.online_sensor.state, false);
        assert_equal("charger_info_after_disconnect", charge_point.charger_info_sensor.state, std::string(""));
        assert_equal("queued_messages_cleared_after_disconnect", charge_point.messages.size(), 0);
    }

    {
        // OCPP 2.0.1 BootNotification uses a different payload shape but the same charge point logic
        TestChargePoint charge_point;
        charge_point.on_connected("A99999", "ocpp2.0.1");
        charge_point.handle_ocpp_text(
            R"([2,"boot-2","BootNotification",{"chargingStation":{"model":"Prism Solar","vendorName":"Silla Industries","firmwareVersion":"3.2.77"},"reason":"PowerUp"}])");
        assert_equal("ocpp201_online_after_boot", charge_point.is_online(), true);
        assert_equal("ocpp201_charger_info_after_boot", charge_point.charger_info_sensor.state,
                     std::string("vendor: Silla Industries, model: Prism Solar, firmware: 3.2.77"));
        assert_equal("ocpp201_boot_response_count", charge_point.messages.size(), 1);
        assert_equal("ocpp201_boot_response", charge_point.messages[0],
                     R"([3,"boot-2",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
    }

    {
        // Heartbeat
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("offline_before_heartbeat", charge_point.is_online(), false);
        charge_point.handle_ocpp_text(R"([2,"d8cc833a-0f43-441a-adbc-5e1f1869f067","Heartbeat",{}])");
        assert_equal("online_after_heartbeat", charge_point.is_online(), true);
        assert_equal("heartbeat_response_count", charge_point.messages.size(), 1);
        assert_equal("heartbeat_response", charge_point.messages[0], R"([3,"d8cc833a-0f43-441a-adbc-5e1f1869f067",{"currentTime":"1970-01-01T00:00:00Z"}])");
    }

    {
        // StatusNotification
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("offline_before_status_notification", charge_point.is_online(), false);
        charge_point.handle_ocpp_text(
            R"([2,"status-1","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"Available"}])");
        assert_equal("online_after_status_notification", charge_point.is_online(), true);
        assert_equal("status_notification_response_count", charge_point.messages.size(), 1);
        assert_equal("status_notification_response", charge_point.messages[0], R"([3,"status-1",{}])");
    }

    {
        // force_boot_notification defaults off
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.loop(5000);
        assert_equal("default_force_boot_trigger_count", charge_point.messages.size(), 0);
    }

    {
        // force_boot_notification waits 5 seconds, then sends one TriggerMessage only
        TestChargePoint charge_point;
        charge_point.set_force_boot_notification(true);
        charge_point.on_connected("A99999");
        charge_point.loop(4999);
        assert_equal("force_boot_trigger_before_delay", charge_point.messages.size(), 0);
        charge_point.loop(5000);
        assert_equal("force_boot_trigger_after_delay", charge_point.messages.size(), 1);
        assert_equal("force_boot_trigger", charge_point.messages[0],
                     R"([2,"trigger-boot-notification","TriggerMessage",{"requestedMessage":"BootNotification"}])");
        std::string force_boot_trigger;
        assert_equal("force_boot_trigger_dequeued", charge_point.pop_queued_message(&force_boot_trigger), true);
        charge_point.handle_ocpp_text(R"([3,"trigger-boot-notification",{"status":"Accepted"}])");
        assert_equal("force_boot_trigger_result_ignored", charge_point.messages.size(), 0);
        charge_point.loop(10000);
        charge_point.on_disconnected();
        charge_point.on_connected("A99999", 10000);
        charge_point.loop(15000);
        assert_equal("force boot trigger remains one shot", charge_point.messages.size(), 0);
    }

    {
        // Natural BootNotification before the delay suppresses the forced trigger
        TestChargePoint charge_point;
        charge_point.set_force_boot_notification(true);
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        charge_point.loop(5000);
        assert_equal("natural_boot_suppresses_trigger_count", charge_point.messages.size(), 1);
        assert_equal("natural_boot_response", charge_point.messages[0], R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
    }

    {
        // Queued messages are FIFO and bounded
        TestChargePoint charge_point;
        charge_point.set_max_queued_messages(2);
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"heartbeat-1","Heartbeat",{}])");
        charge_point.handle_ocpp_text(R"([2,"heartbeat-2","Heartbeat",{}])");
        charge_point.handle_ocpp_text(R"([2,"heartbeat-3","Heartbeat",{}])");
        assert_equal("bounded_queue_count", charge_point.messages.size(), 2);
        assert_equal("bounded_queue_first", charge_point.messages[0], R"([3,"heartbeat-1",{"currentTime":"1970-01-01T00:00:00Z"}])");
        assert_equal("bounded_queue_second", charge_point.messages[1], R"([3,"heartbeat-2",{"currentTime":"1970-01-01T00:00:00Z"}])");

        std::string message;
        assert_equal("pop_first_message", charge_point.pop_queued_message(&message), true);
        assert_equal("first_popped_message", message, R"([3,"heartbeat-1",{"currentTime":"1970-01-01T00:00:00Z"}])");
        assert_equal("remaining_queue_count", charge_point.messages.size(), 1);
        assert_equal("remaining_first_message", charge_point.messages[0], R"([3,"heartbeat-2",{"currentTime":"1970-01-01T00:00:00Z"}])");
    }

    return 0;
}
