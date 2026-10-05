#include "Mcp23008.h"
namespace {
constexpr uint8_t address=0x20, iodir=0x00, ipol=0x01, iocon=0x05,
                  gppu=0x06, gpio=0x09, olat=0x0a;
}
bool Mcp23008::write(uint8_t reg,uint8_t value) {
  bus_.beginTransmission(address);bus_.write(reg);bus_.write(value);
  return bus_.endTransmission()==0;
}
bool Mcp23008::begin() {
  initialized_=false;
  // A controller reboot need not reset the powered expander. Explicitly
  // release all pins, set off levels, then enable only the lighting outputs.
  uint8_t off=rear_?0xc0:0;
  if(!write(iodir,0xff) || !write(iocon,0) || !write(ipol,0) ||
     !write(gppu,rear_?0:0x50) || !write(olat,off) ||
     !write(iodir,rear_?0x3f:0x5f)) return false;
  latch_=off;initialized_=true;return true;
}
bool Mcp23008::outputs(bool up,bool down) {
  if(!initialized_) return false;
  uint8_t value=rear_?static_cast<uint8_t>((up?0:0x40)|(down?0:0x80)):
                       static_cast<uint8_t>((up?0x80:0)|(down?0x20:0));
  if(value==latch_) return true;
  if(!write(olat,value)) {initialized_=false;return false;}
  latch_=value;return true;
}
bool Mcp23008::readButtons(bool &up,bool &down) {
  up=down=false;
  if(!initialized_) return false;
  bus_.beginTransmission(address);bus_.write(gpio);
  if(bus_.endTransmission(false)!=0 || bus_.requestFrom(address,uint8_t(1))!=1) {
    initialized_=false;return false;
  }
  uint8_t value=bus_.read();
  if(!rear_) {up=!(value&0x40);down=!(value&0x10);}
  return true;
}
