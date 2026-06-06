import unittest

try:
    from .cpp_test_case import CppTestCase
except ImportError:
    from cpp_test_case import CppTestCase


class ChargerTests(CppTestCase):
    def test_charger_helpers(self):
        self.assert_cpp_test_passes(
            "test_charger.cpp", ["esphome/components/ocpp/charger.cpp"]
        )


if __name__ == "__main__":
    unittest.main()
