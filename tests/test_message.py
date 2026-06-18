import unittest

from .cpp_test_case import CppTestCase


class OcppMessageTests(CppTestCase):
    def test_cpp(self):
        self.assert_cpp_test_passes("test_message.cpp")


if __name__ == "__main__":
    unittest.main()