#pragma once
#include "Types.h"
// One RAM slot. Repeated offers do not refresh its age.
class ClosedTrigger {
public:
  void offer(uint32_t now) { if (!pending_) { pending_ = true; born_ = now; if(!++generation_) ++generation_; } }
  void cancel() { pending_ = false; }
  bool eligible(uint32_t now, uint32_t expiry, bool confirmedClosed) {
    if (!confirmedClosed || (pending_ && elapsed(now,born_) >= expiry)) pending_ = false;
    return pending_;
  }
  bool pending() const { return pending_; }
  uint32_t generation() const { return generation_; }
  uint32_t age(uint32_t now) const { return pending_ ? elapsed(now,born_) : 0; }
  void acknowledge(uint32_t generation) { if (generation == generation_) pending_ = false; }
private:
  bool pending_ = false;
  uint32_t born_ = 0, generation_ = 0;
};
