import unittest
from pathlib import Path

try:
    from .cpp_test_case import CppTestCase
except ImportError:
    from cpp_test_case import CppTestCase


class ConnectorTests(CppTestCase):
    def test_connector_helpers(self):
        self.assert_cpp_test_passes(
            "test_connector.cpp", ["esphome/components/ocpp/connector.cpp"]
        )

    def test_connector_power_option_is_not_exposed(self):
        repo_root = Path(__file__).resolve().parents[1]
        source_paths = [
            "esphome/components/ocpp/__init__.py",
            "esphome/components/ocpp/connector.h",
            "esphome/components/ocpp/ocpp.h",
            "esphome/components/ocpp/ocpp.cpp",
        ]
        source_text = "\n".join((repo_root / path).read_text() for path in source_paths)

        removed_names = [
            "CONF" + "_POWER",
            "set_connector_" + "power" + "_sensor",
            "power" + "_sensor",
        ]
        for removed_name in removed_names:
            self.assertNotIn(removed_name, source_text)


if __name__ == "__main__":
    unittest.main()
