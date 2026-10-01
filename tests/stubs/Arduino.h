#pragma once
#include <stdint.h>
#include <string>
#include <cstring>
#include <cstdio>
using String=std::string;
#define PROGMEM
struct SerialStub {template<class... Args> void printf(const char *,Args...) {}};
inline SerialStub Serial;
inline size_t strlcpy(char *out,const char *in,size_t size) {size_t n=strlen(in);if(size){size_t copy=n<size-1?n:size-1;memcpy(out,in,copy);out[copy]=0;}return n;}
extern uint32_t testMillis;
inline uint32_t millis() {return testMillis;}
class IPAddress {
public:
  IPAddress(uint32_t ip=0):ip_(ip){}
  explicit operator uint32_t() const {return ip_;}
  String toString() const {char out[24];snprintf(out,sizeof(out),"%u.%u.%u.%u",ip_&255,(ip_>>8)&255,(ip_>>16)&255,ip_>>24);return out;}
private:uint32_t ip_;
};
