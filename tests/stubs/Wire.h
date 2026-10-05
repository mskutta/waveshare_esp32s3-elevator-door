#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>
#include <cassert>
class TwoWire {
public:
  uint8_t input=0xff;
  int failAt=-1,transactions=0;
  bool shortRead=false;
  std::vector<std::pair<uint8_t,uint8_t>> writes;
  void beginTransmission(uint8_t address) {assert(address==0x20);bytes.clear();}
  size_t write(uint8_t value) {bytes.push_back(value);return 1;}
  uint8_t endTransmission(bool stop=true) {
    if(transactions++==failAt)return 4;
    if(stop){assert(bytes.size()==2);writes.emplace_back(bytes[0],bytes[1]);}
    else assert(bytes.size()==1&&bytes[0]==9);
    return 0;
  }
  uint8_t requestFrom(uint8_t address,uint8_t size) {assert(address==0x20&&size==1);return shortRead?0:1;}
  int read() {return input;}
private:std::vector<uint8_t> bytes;
};
