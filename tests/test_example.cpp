#include <cstdlib>
#include <iostream>

template<typename T> void assert_equal(const char *description, const T &actual, const T &expected) {
  if (actual == expected)
    return;
  std::cerr << description << "\n";
  std::cerr << "Expected: " << expected << "\n";
  std::cerr << "Actual:   " << actual << "\n";
  std::abort();
}

int sum(int a, int b) {
  return a + b;
}

int main() {
  assert_equal("sum_1_1", sum(1, 1), 2);

  return 0;
}
