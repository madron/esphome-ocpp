#include <cstdlib>
#include <iostream>

template<typename Actual, typename Expected>
void assert_equal(const char *description, const Actual &actual, const Expected &expected) {
  if (actual == expected)
    return;
  std::cerr << description << "\n";
  std::cerr << "Expected: " << expected << "\n";
  std::cerr << "Actual:   " << actual << "\n";
  std::abort();
}
