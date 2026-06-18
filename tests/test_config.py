import unittest
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "esphome/components/ocpp/__init__.py"
MODULE_SPEC = spec_from_file_location("local_ocpp_component", MODULE_PATH)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
OCPP_MODULE = module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(OCPP_MODULE)

CONFIG_SCHEMA = OCPP_MODULE.CONFIG_SCHEMA


class ChargePointSchemaTests(unittest.TestCase):
    def test_debug_ocpp_messages_default(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [{"id": "garage_left", "charge_point_id": "A99999"}],
            }
        )

        self.assertFalse(validated["charge_points"][0]["debug_ocpp_messages"])
        self.assertFalse(validated["charge_points"][0]["force_boot_notification"])

    def test_debug_ocpp_messages_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "debug_ocpp_messages": True,
                    }
                ],
            }
        )

        self.assertTrue(validated["charge_points"][0]["debug_ocpp_messages"])

    def test_force_boot_notification_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
                        "force_boot_notification": True,
                    }
                ],
            }
        )

        self.assertTrue(validated["charge_points"][0]["force_boot_notification"])

    def test_force_protocol_and_protocol_text_sensor_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
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
                        "charger_info": {"name": "Garage Charger Info"},
                    }
                ],
            }
        )

        self.assertIn("charger_info", validated["charge_points"][0])

    def test_ocpp_2_0_1_force_protocol_enabled(self):
        validated = CONFIG_SCHEMA(
            {
                "id": "ocpp_id",
                "charge_points": [
                    {
                        "id": "garage_left",
                        "charge_point_id": "A99999",
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
                            "force_protocol": "ocpp9.9",
                        }
                    ],
                }
            )


if __name__ == "__main__":
    unittest.main()
