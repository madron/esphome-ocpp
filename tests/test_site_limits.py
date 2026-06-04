import unittest

try:
    from .cpp_test_case import CppTestCase
except ImportError:
    from cpp_test_case import CppTestCase


class SiteLimitTests(CppTestCase):
    def test_site_limit(self):
        self.assert_cpp_test_passes("test_site_limits.cpp")


if __name__ == "__main__":
    unittest.main()
