import unittest

try:
    from .cpp_test_case import CppTestCase
except ImportError:
    from cpp_test_case import CppTestCase


class GridTests(CppTestCase):
    def test_grid_helpers(self):
        self.assert_cpp_test_passes(
            "test_grid.cpp", ["esphome/components/ocpp/grid.cpp"]
        )


if __name__ == "__main__":
    unittest.main()