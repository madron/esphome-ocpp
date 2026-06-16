#include "assertions.cpp"
#include "esphome/components/ocpp/charge_point.h"

#include <string>

using esphome::ocpp::ChargePoint;

int main() {
    ChargePoint charge_point;

    // get_charge_point_id
    assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), std::string(""));

    // set_charge_point_id
    charge_point.set_charge_point_id("A99999");
    assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), std::string("A99999"));

    // get_debug_ocpp_messages
    assert_equal("get_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), false);

    // set_debug_ocpp_messages
    charge_point.set_debug_ocpp_messages(true);
    assert_equal("set_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), true);

    return 0;
}
