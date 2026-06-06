import unittest

try:
    from .cpp_test_case import CppTestCase
except ImportError:
    from cpp_test_case import CppTestCase


class ConnectorTests(CppTestCase):
    def test_connector_helpers(self):
        self.assert_cpp_test_passes(
            "test_connector.cpp", ["esphome/components/ocpp/connector.cpp"]
        )


if __name__ == "__main__":
    unittest.main()
