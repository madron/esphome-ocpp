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


if __name__ == "__main__":
    unittest.main()
