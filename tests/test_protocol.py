import unittest

from .cpp_test_case import CppTestCase


class OcppProtocolTests(CppTestCase):
    def test_cpp(self):
        self.assert_cpp_test_passes(
            "test_protocol.cpp",
            extra_sources=("esphome/components/ocpp/protocol.cpp",),
        )


if __name__ == "__main__":
    unittest.main()