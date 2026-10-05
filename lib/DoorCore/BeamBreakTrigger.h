#pragma once
#include "Types.h"

// One fresh obstruction per cue, only before the first position confirmation.
// Observe every sample, even while faulted or disconnected, to avoid replay.
class BeamBreakTrigger {
public:
  bool observe(bool broken, DoorState state, bool homed) {
    initialized_ = initialized_ || homed;
    const bool rising = sampled_ && broken && !lastBroken_;
    sampled_ = true;
    lastBroken_ = broken;
    return rising && !initialized_ && state == DoorState::Unknown;
  }
private:
  bool sampled_ = false, lastBroken_ = false, initialized_ = false;
};
