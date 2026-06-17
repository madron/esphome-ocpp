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

int main() {
    ChargePoint charge_point;

    // get_charge_point_id
    assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), std::string(""));
    assert_equal("get_connection_id", charge_point.get_connection_id(), std::string(""));

    // set_charge_point_id also initializes the connection_id
    charge_point.set_charge_point_id("A99999");
    assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), std::string("A99999"));
    assert_equal("set_charge_point_id connection_id", charge_point.get_connection_id(), std::string("A99999"));

    // set_connection_id
    charge_point.set_connection_id("B11111");
    assert_equal("set_connection_id", charge_point.get_connection_id(), std::string("B11111"));
    assert_equal("set_connection_id keeps charge_point_id", charge_point.get_charge_point_id(), std::string("A99999"));

    // get_debug_ocpp_messages
    assert_equal("get_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), false);

    // set_debug_ocpp_messages
    charge_point.set_debug_ocpp_messages(true);
    assert_equal("set_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), true);

    // BootNotification updates online state and emits a response
    TestMessageSink sink;
    BinarySensor online_sensor;
    charge_point.set_message_sink(&sink);
    charge_point.set_online_binary_sensor(&online_sensor);
    charge_point.on_connected("A99999");
    assert_equal("online before boot", charge_point.is_online(), false);
    assert_equal("online sensor before boot", online_sensor.state, false);
    charge_point.handle_ocpp_text("[2,\"boot-1\",\"BootNotification\",{\"chargePointVendor\":\"Acme\",\"chargePointModel\":\"Wallbox\"}]");
    assert_equal("online after boot", charge_point.is_online(), true);
    assert_equal("online sensor after boot", online_sensor.state, true);
    assert_equal("boot response count", sink.messages.size(), static_cast<size_t>(1));
    assert_equal("boot response connection", sink.connection_ids[0], std::string("A99999"));
    assert_equal("boot response", sink.messages[0], std::string("[3,\"boot-1\",{\"currentTime\":\"1970-01-01T00:00:00Z\",\"interval\":300,\"status\":\"Accepted\"}]"));

    charge_point.on_disconnected();
    assert_equal("online after disconnect", charge_point.is_online(), false);
    assert_equal("online sensor after disconnect", online_sensor.state, false);

    return 0;
}
