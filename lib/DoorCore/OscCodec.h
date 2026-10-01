#pragma once
#include <stdint.h>
#include <stddef.h>
constexpr size_t kOscCapacity = 2048;
class SlipDecoder {
public:
  // Nonzero means a complete frame is available in data(), until next feed.
  size_t feed(uint8_t b);
  const uint8_t *data() const { return buffer_; }
  void reset() { used_ = 0; escaped_ = bad_ = synced_ = false; }
  uint32_t rejected() const { return rejected_; }
private:
  uint8_t buffer_[kOscCapacity]{};
  size_t used_ = 0;
  bool escaped_ = false, bad_ = false, synced_ = false;
  uint32_t rejected_ = 0;
};
struct OscView { const char *address = nullptr; const char *argument = nullptr; };
bool decodeOsc(const uint8_t *, size_t, OscView &);
size_t encodeOsc(uint8_t *, size_t, const char *address, const char *argument = nullptr);
size_t encodeOscInt(uint8_t *, size_t, const char *address, int32_t argument);
size_t encodeSlip(uint8_t *, size_t, const uint8_t *, size_t);
