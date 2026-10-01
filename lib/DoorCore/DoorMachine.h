#pragma once
#include "Types.h"
class DoorMachine {
public:
  DoorMachine(const MotionConfig &m, bool /*rear*/) : motion_(m) {}
  DoorOutput tick(const DoorInput &, bool openRequested = false, bool resetFault = false);
  DoorOutput fault(const char *reason);
  DoorState state() const { return state_; }
  bool homed() const { return homed_; }
  const char *faultReason() const { return fault_; }
  bool canTune(const DoorInput &i) const { return i.ticHealthy && state_ == DoorState::Closed && homed_ && i.limit && !i.energized; }
  bool canMaintain(const DoorInput &i) const { return i.ticHealthy && !i.energized && (canTune(i) || state_ == DoorState::Unknown || state_ == DoorState::Fault); }
  void tune(const MotionConfig &m) { motion_ = m; }
private:
  MotionConfig motion_;
  bool homed_ = false, cycle_ = false, lastLimit_ = false;
  DoorState state_ = DoorState::Unknown;
  uint32_t entered_ = 0, dwell_ = 0;
  const char *fault_ = "";
  void enter(DoorState s, uint32_t now, DoorOutput &o) { if (state_ != s) o.changed = true; state_ = s; entered_ = now; }
};
