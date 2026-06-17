#include "assertions.cpp"
#include "esphome/components/ocpp/charge_point.h"

#include <string>
#include <vector>

using esphome::ocpp::ChargePoint;
using esphome::ocpp::OcppMessageSink;
using esphome::binary_sensor::BinarySensor;

class TestMessageSink : public OcppMessageSink {
 public:
    void send_ocpp_text(const std::string &connection_id, const std::string &message) override {
        this->connection_ids.push_back(connection_id);
        this->messages.push_back(message);
    }

    std::vector<std::string> connection_ids;
    std::vector<std::string> messages;
};

class TestChargePoint : public ChargePoint {
 public:
    TestChargePoint() {
        this->set_message_sink(&this->sink);
        this->set_online_binary_sensor(&this->online_sensor);
    }

    TestChargePoint(const TestChargePoint &) = delete;
    TestChargePoint &operator=(const TestChargePoint &) = delete;

    TestMessageSink sink;
    BinarySensor online_sensor;
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

        // set_debug_ocpp_messages
        charge_point.set_debug_ocpp_messages(true);
        assert_equal("set_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), true);

        // set_force_boot_notification
        charge_point.set_force_boot_notification(true);
        assert_equal("set_force_boot_notification", charge_point.get_force_boot_notification(), true);
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
        assert_equal("boot_response_count", charge_point.sink.messages.size(), 1);
        assert_equal("boot_response_connection", charge_point.sink.connection_ids[0], "A99999");
        assert_equal("boot_response", charge_point.sink.messages[0], R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");

        charge_point.on_disconnected();
        assert_equal("online_after_disconnect", charge_point.is_online(), false);
        assert_equal("online_sensor_after_disconnect", charge_point.online_sensor.state, false);
    }

    {
        // Heartbeat
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("offline_before_heartbeat", charge_point.is_online(), false);
        charge_point.handle_ocpp_text(R"([2,"d8cc833a-0f43-441a-adbc-5e1f1869f067","Heartbeat",{}])");
        assert_equal("online_after_heartbeat", charge_point.is_online(), true);
        assert_equal("heartbeat_response_count", charge_point.sink.messages.size(), 1);
        assert_equal("heartbeat_response", charge_point.sink.messages[0], R"([3,"d8cc833a-0f43-441a-adbc-5e1f1869f067",{"currentTime":"1970-01-01T00:00:00Z"}])");
    }

    {
        // StatusNotification
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("offline_before_status_notification", charge_point.is_online(), false);
        charge_point.handle_ocpp_text(
            R"([2,"status-1","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"Available"}])");
        assert_equal("online_after_status_notification", charge_point.is_online(), true);
        assert_equal("status_notification_response_count", charge_point.sink.messages.size(), 1);
        assert_equal("status_notification_response", charge_point.sink.messages[0], R"([3,"status-1",{}])");
    }

    {
        // force_boot_notification defaults off
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.loop(5000);
        assert_equal("default_force_boot_trigger_count", charge_point.sink.messages.size(), 0);
    }

    {
        // force_boot_notification waits 5 seconds, then sends one TriggerMessage only
        TestChargePoint charge_point;
        charge_point.set_force_boot_notification(true);
        charge_point.on_connected("A99999");
        charge_point.loop(4999);
        assert_equal("force_boot_trigger_before_delay", charge_point.sink.messages.size(), 0);
        charge_point.loop(5000);
        assert_equal("force_boot_trigger_after_delay", charge_point.sink.messages.size(), 1);
        assert_equal("force_boot_trigger_connection", charge_point.sink.connection_ids[0], "A99999");
        assert_equal("force_boot_trigger", charge_point.sink.messages[0],
                     R"([2,"trigger-boot-notification-1","TriggerMessage",{"requestedMessage":"BootNotification"}])");
        charge_point.handle_ocpp_text(R"([3,"trigger-boot-notification-1",{"status":"Accepted"}])");
        assert_equal("force_boot_trigger_result_ignored", charge_point.sink.messages.size(), 1);
        charge_point.loop(10000);
        charge_point.on_disconnected();
        charge_point.on_connected("A99999", 10000);
        charge_point.loop(15000);
        assert_equal("force boot trigger remains one shot", charge_point.sink.messages.size(), 1);
    }

    {
        // Natural BootNotification before the delay suppresses the forced trigger
        TestChargePoint charge_point;
        charge_point.set_force_boot_notification(true);
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        charge_point.loop(5000);
        assert_equal("natural_boot_suppresses_trigger_count", charge_point.sink.messages.size(), 1);
        assert_equal("natural_boot_response", charge_point.sink.messages[0], R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
    }

    return 0;
}
