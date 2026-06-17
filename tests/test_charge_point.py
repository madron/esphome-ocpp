import unittest
from .cpp_test_case import CppTestCase


class ExampleTests(CppTestCase):
    def test_example(self):
        self.assert_cpp_test_passes(
            "test_charge_point.cpp",
            extra_sources=(
                "esphome/components/ocpp/charge_point.cpp",
                "esphome/components/ocpp/protocol.cpp",
            ),
        )


if __name__ == "__main__":
    unittest.main()
