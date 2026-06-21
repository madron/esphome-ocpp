import unittest
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

from esphome.core import CORE


MODULE_PATH = Path(__file__).resolve().parents[1] / "esphome/components/ocpp/__init__.py"
MODULE_SPEC = spec_from_file_location("local_ocpp_component", MODULE_PATH)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
OCPP_MODULE = module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(OCPP_MODULE)

RAW_CONFIG_SCHEMA = OCPP_MODULE.CONFIG_SCHEMA
TEST_CONFIG_PATH = Path(__file__).with_name("test_config.yaml")


def CONFIG_SCHEMA(config):
    CORE.reset()
    CORE.config_path = TEST_CONFIG_PATH
    return RAW_CONFIG_SCHEMA(config)


class ChargePointSchemaTests(unittest.TestCase):
    def test_debug_ocpp_messages_default(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {"id": "garage_left", "charge_point_id": "A99999", "phases": 3, "max_current": 32}
                ],
            }
        )

        self.assertFalse(validated["charge_points"][0]["debug_ocpp_messages"])
        self.assertEqual(validated["charge_points"][0]["debug_ocpp_exclude_actions"], [])
        self.assertEqual(validated["charge_points"][0]["startup_notifications_delay"], 300)

    def test_debug_ocpp_messages_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "debug_ocpp_messages": True,
                    }
                ],
            }
        )

        self.assertTrue(validated["charge_points"][0]["debug_ocpp_messages"])

    def test_debug_ocpp_exclude_actions_configured(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "debug_ocpp_messages": True,
                        "debug_ocpp_exclude_actions": ["MeterValues", "Heartbeat"],
                    }
                ],
            }
        )

        self.assertEqual(validated["charge_points"][0]["debug_ocpp_exclude_actions"], ["MeterValues", "Heartbeat"])

    def test_debug_ocpp_exclude_actions_rejects_empty_action(self):
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 3,
                            "max_current": 32,
                            "debug_ocpp_exclude_actions": ["MeterValues", ""],
                        }
                    ],
                }
            )

    def test_startup_notifications_delay_configured(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "startup_notifications_delay": 0,
                    }
                ],
            }
        )

        self.assertEqual(validated["charge_points"][0]["startup_notifications_delay"], 0)

    def test_force_boot_notification_rejected(self):
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 3,
                            "max_current": 32,
                            "force_boot_notification": True,
                        }
                    ],
                }
            )

    def test_force_protocol_and_protocol_text_sensor_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "force_protocol": "ocpp1.6",
                        "protocol": {"name": "Garage Protocol"},
                    }
                ],
            }
        )

        self.assertEqual(validated["charge_points"][0]["force_protocol"], "ocpp1.6")
        self.assertIn("protocol", validated["charge_points"][0])

    def test_charger_info_text_sensor_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "charger_info": {"name": "Garage Charger Info"},
                    }
                ],
            }
        )

        self.assertIn("charger_info", validated["charge_points"][0])

    def test_default_connector_id(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {"id": "garage_left", "charge_point_id": "A99999", "phases": 3, "max_current": 32}
                ],
            }
        )

        connectors = validated["charge_points"][0]["connectors"]
        self.assertEqual(len(connectors), 1)
        self.assertEqual(connectors[0]["connector_id"], 1)
        self.assertFalse(connectors[0]["log_meter_values"])

    def test_connector_meter_value_sensors_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "connectors": [
                            {
                                "connector_id": 2,
                                "log_meter_values": True,
                                "current": {"name": "Garage Current"},
                                "power": {"name": "Garage Power"},
                                "total_energy": {"name": "Garage Total Energy"},
                                "session_energy": {"name": "Garage Session Energy"},
                                "session_time": {"name": "Garage Session Time"},
                                "voltage": {"name": "Garage Voltage"},
                            }
                        ],
                    }
                ],
            }
        )

        connector = validated["charge_points"][0]["connectors"][0]
        self.assertEqual(connector["connector_id"], 2)
        self.assertTrue(connector["log_meter_values"])
        self.assertIn("current", connector)
        self.assertIn("power", connector)
        self.assertIn("total_energy", connector)
        self.assertIn("session_energy", connector)
        self.assertIn("session_time", connector)
        self.assertIn("voltage", connector)

    def test_connector_status_and_error_text_sensors_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "connectors": [
                            {
                                "connector_id": 2,
                                "plugged": {"name": "Garage Plugged"},
                                "status": {"name": "Garage Status"},
                                "error": {"name": "Garage Error"},
                            }
                        ],
                    }
                ],
            }
        )

        connector = validated["charge_points"][0]["connectors"][0]
        self.assertEqual(connector["connector_id"], 2)
        self.assertIn("plugged", connector)
        self.assertIn("status", connector)
        self.assertIn("error", connector)

    def test_connector_current_numbers_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "connectors": [
                            {
                                "connector_id": 2,
                                "current_limit": {"name": "Garage Current Limit"},
                                "requested_current": {"name": "Garage Requested Current"},
                            }
                        ],
                    }
                ],
            }
        )

        connector = validated["charge_points"][0]["connectors"][0]
        self.assertIn("current_limit", connector)
        self.assertIn("requested_current", connector)

    def test_connector_control_current_sensor_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "connectors": [
                            {
                                "connector_id": 2,
                                "control_current": {"name": "Garage Control Current"},
                            }
                        ],
                    }
                ],
            }
        )

        connector = validated["charge_points"][0]["connectors"][0]
        self.assertIn("control_current", connector)

    def test_connector_phase_mapping_defaults_to_connector_phases(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "connectors": [{"connector_id": 1, "phases": 1}],
                    }
                ],
            }
        )

        connector = validated["charge_points"][0]["connectors"][0]
        self.assertEqual(connector["phase_mapping"], [1])

    def test_connector_phase_mapping_accepts_rotation(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 12,
                        "connectors": [
                            {"connector_id": 1, "phases": 3, "phase_mapping": ["l1", "l2", "l3"]},
                            {"connector_id": 2, "phases": 3, "phase_mapping": ["l2", "l3", "l1"]},
                        ],
                    }
                ],
            }
        )

        connectors = validated["charge_points"][0]["connectors"]
        self.assertEqual(connectors[0]["phase_mapping"], [1, 2, 3])
        self.assertEqual(connectors[1]["phase_mapping"], [2, 3, 1])

    def test_connector_phase_mapping_rejects_invalid_mapping(self):
        invalid_connectors = [
            {"connector_id": 1, "phases": 3, "phase_mapping": ["l1", "l2"]},
            {"connector_id": 1, "phases": 3, "phase_mapping": ["l1", "l1", "l2"]},
        ]
        for connector in invalid_connectors:
            with self.subTest(connector=connector), self.assertRaises(Exception):
                CONFIG_SCHEMA(
                    {
                        "id": "ocpp_id",
                        "charge_points": [
                            {
                                "id": "garage_left",
                                "charge_point_id": "A99999",
                                "phases": 3,
                                "max_current": 6,
                                "connectors": [connector],
                            }
                        ],
                    }
                )
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 1,
                            "max_current": 6,
                            "connectors": [{"connector_id": 1, "phases": 1, "phase_mapping": ["l2"]}],
                        }
                    ],
                }
            )

    def test_connector_current_limit_max_value_override(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "connectors": [
                            {
                                "connector_id": 2,
                                "current_limit": {
                                    "name": "Garage Current Limit",
                                    "max_value": 16,
                                },
                            }
                        ],
                    }
                ],
            }
        )

        connector = validated["charge_points"][0]["connectors"][0]
        self.assertEqual(connector["current_limit"]["max_value"], 16)

    def test_connector_current_limit_max_value_above_max_current_rejected(self):
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 3,
                            "max_current": 16,
                            "connectors": [
                                {
                                    "connector_id": 2,
                                    "current_limit": {
                                        "name": "Garage Current Limit",
                                        "max_value": 32,
                                    },
                                }
                            ],
                        }
                    ],
                }
            )

    def test_max_current_required(self):
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [{"id": "garage_left", "charge_point_id": "A99999", "phases": 3}],
                }
            )

    def test_max_current_minimum_and_no_upper_limit(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {"id": "garage_left", "charge_point_id": "A99999", "phases": 3, "max_current": 80}
                ],
            }
        )

        self.assertEqual(validated["charge_points"][0]["max_current"], 80)
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {"id": "garage_left", "charge_point_id": "A99999", "phases": 3, "max_current": 5}
                    ],
                }
            )

    def test_max_current_must_allow_minimum_current_for_each_connector(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 12,
                        "connectors": [{"connector_id": 1}, {"connector_id": 2}],
                    }
                ],
            }
        )

        self.assertEqual(validated["charge_points"][0]["max_current"], 12)
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 3,
                            "max_current": 10,
                            "connectors": [{"connector_id": 1}, {"connector_id": 2}],
                        }
                    ],
                }
            )

    def test_duplicate_connector_id_rejected(self):
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 3,
                            "max_current": 32,
                            "connectors": [{"connector_id": 1}, {"connector_id": 1}],
                        }
                    ],
                }
            )

    def test_charge_point_level_meter_sensor_rejected(self):
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 3,
                            "max_current": 32,
                            "current": {"name": "Garage Current"},
                        }
                    ],
                }
            )

    def test_ocpp_2_0_1_force_protocol_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "phases": 3,
                        "max_current": 32,
                        "force_protocol": "ocpp2.0.1",
                    }
                ],
            }
        )

        self.assertEqual(validated["charge_points"][0]["force_protocol"], "ocpp2.0.1")

    def test_unsupported_force_protocol_rejected(self):
        with self.assertRaises(Exception):
            CONFIG_SCHEMA(
                {
                    "id": "ocpp_id",
                    "charge_points": [
                        {
                            "id": "garage_left",
                            "charge_point_id": "A99999",
                            "phases": 3,
                            "max_current": 32,
                            "force_protocol": "ocpp9.9",
                        }
                    ],
                }
            )


if __name__ == "__main__":
    unittest.main()
