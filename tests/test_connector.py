import unittest

from .cpp_test_case import CppTestCase


class ConnectorTests(CppTestCase):
    def test_cpp(self):
        self.assert_cpp_test_passes(
            "test_connector.cpp",
            extra_sources=("esphome/components/ocpp/connector.cpp",),
        )


if __name__ == "__main__":
    unittest.main()