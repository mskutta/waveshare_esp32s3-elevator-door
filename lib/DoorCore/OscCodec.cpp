#include "OscCodec.h"
#include <string.h>
size_t SlipDecoder::feed(uint8_t b) {
  if (b == 0xC0) {
    size_t ready = synced_ && !bad_ && !escaped_ ? used_ : 0;
    if (bad_ || escaped_) ++rejected_;
    used_ = 0; bad_ = escaped_ = false; synced_ = true; return ready;
  }
  if (!synced_ || bad_) return 0;
  if (escaped_) {
    escaped_ = false;
    if (b == 0xDC) b = 0xC0;
    else if (b == 0xDD) b = 0xDB;
    else { bad_ = true; return 0; }
  } else if (b == 0xDB) { escaped_ = true; return 0; }
  if (used_ == sizeof(buffer_)) { bad_ = true; return 0; }
  buffer_[used_++] = b; return 0;
}
bool decodeOsc(const uint8_t *data, size_t length, OscView &v) {
  v = {};
  if (!length || length % 4 || length > kOscCapacity) return false;
  size_t offset = 0;
  auto string = [&](const char *&result) {
    size_t begin = offset;
    while (offset < length && data[offset]) ++offset;
    if (offset == length) return false;
    size_t end = (offset + 4) & ~size_t(3);
    while (offset < end) { if (offset >= length || data[offset++]) return false; }
    result = reinterpret_cast<const char *>(data + begin); return true;
  };
  const char *tags;
  if (!string(v.address) || v.address[0] != '/' || !string(tags)) return false;
  if (!strcmp(tags,",")) return offset == length;
  if (strcmp(tags,",s")) return false;
  return string(v.argument) && offset == length;
}
size_t encodeOsc(uint8_t *out, size_t capacity, const char *address, const char *arg) {
  if (!address || address[0] != '/') return 0;
  size_t used = 0;
  auto string = [&](const char *s) {
    size_t n = strlen(s) + 1, padded = (n + 3) & ~size_t(3);
    if (padded > capacity - used) return false;
    memcpy(out+used,s,n); memset(out+used+n,0,padded-n); used += padded; return true;
  };
  if (!string(address) || !string(arg ? ",s" : ",") || (arg && !string(arg))) return 0;
  return used;
}
size_t encodeSlip(uint8_t *out, size_t capacity, const uint8_t *data, size_t length) {
  size_t needed = 2;
  for (size_t i=0;i<length;++i) needed += (data[i] == 0xC0 || data[i] == 0xDB) ? 2 : 1;
  if (needed > capacity) return 0;
  size_t n=0; out[n++]=0xC0;
  for (size_t i=0;i<length;++i) {
    uint8_t b=data[i];
    if (b==0xC0 || b==0xDB) { out[n++]=0xDB; out[n++]=b==0xC0 ? 0xDC : 0xDD; }
    else out[n++]=b;
  }
  out[n++]=0xC0; return n;
}
size_t encodeOscInt(uint8_t *out, size_t capacity, const char *address, int32_t argument) {
  size_t n=encodeOsc(out,capacity,address);
  if(!n || capacity-n<4) return 0;
  out[n-3]='i';
  uint32_t v=static_cast<uint32_t>(argument);
  out[n++]=v>>24;out[n++]=v>>16;out[n++]=v>>8;out[n++]=v;
  return n;
}
