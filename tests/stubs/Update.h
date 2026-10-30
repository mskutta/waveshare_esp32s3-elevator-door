#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
constexpr int U_FLASH = 0;
struct UpdateFake {
  bool failBegin = false, failWrite = false, failEnd = false, running = false, aborted = false;
  size_t expected = 0;
  std::vector<uint8_t> bytes;
  bool begin(size_t n, int) {
    expected = n;
    bytes.clear();
    running = !failBegin;
    aborted = false;
    return running;
  }
  size_t write(uint8_t *p, size_t n) {
    if (failWrite)
      return 0;
    bytes.insert(bytes.end(), p, p + n);
    return n;
  }
  bool end() {
    running = false;
    return !failEnd && bytes.size() == expected;
  }
  void abort() {
    running = false;
    aborted = true;
  }
  const char *errorString() { return "Mock update failure"; }
};
inline UpdateFake Update;
