#pragma once
#include <Wire.h>
// Only the door-control task may call this driver (shared I2C bus).
class Mcp23008 {
public:
  explicit Mcp23008(TwoWire &bus, bool rear):bus_(bus),rear_(rear) {}
  bool begin();
  bool readButtons(bool &up, bool &down);
  bool outputs(bool up, bool down);
private:
  bool write(uint8_t reg, uint8_t value);
  TwoWire &bus_;
  bool rear_, initialized_=false;
  uint8_t latch_=0;
};
