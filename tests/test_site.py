import unittest

try:
    from .cpp_test_case import CppTestCase
except ImportError:
    from cpp_test_case import CppTestCase


class SiteTests(CppTestCase):
    def test_site_helpers(self):
        self.assert_cpp_test_passes(
            "test_site.cpp", ["esphome/components/ocpp/site.cpp"]
        )


if __name__ == "__main__":
    unittest.main()