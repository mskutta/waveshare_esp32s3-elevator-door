#include "DoorMachine.h"
#include <stdlib.h>
DoorOutput DoorMachine::fault(const char *reason) {
  DoorOutput o; o.motor = MotorAction::Release; o.changed = state_ != DoorState::Fault;
  state_ = DoorState::Fault; fault_ = reason; homed_ = false; cycle_ = qualified_ = false;
  retryPaused_ = retryActive_ = false; return o;
}
DoorOutput DoorMachine::tick(const DoorInput &i, bool open, bool reset) {
  DoorOutput o;
  if (!i.ticHealthy) return state_ == DoorState::Fault ? o : fault("Tic communication");
  if (state_ == DoorState::Fault) {
    if (!reset || i.energized) return o;
    fault_ = ""; lastLimit_ = false; enter(DoorState::Unknown, i.now, o);
  }
  // An active switch at boot confirms position. During opening it must first
  // release: a sustained closed switch must never repeatedly zero the encoder.
  bool confirms = i.limit && (state_ == DoorState::Unknown ||
    (!lastLimit_ && state_ != DoorState::Open && state_ != DoorState::Reopen &&
      state_ != DoorState::Opening && state_ != DoorState::Reopening));
  lastLimit_ = i.limit;
  if (confirms) {
    o.motor = MotorAction::Release; o.zeroEncoder = true; o.closedCycle = cycle_ && qualified_;
    cycle_ = qualified_ = false;retryPaused_ = retryActive_ = false;
    homed_ = true; enter(DoorState::Closed, i.now, o); return o;
  }
  if (!homed_) return o;
  if (state_ == DoorState::Closed && !i.limit) {
    homed_ = false; cycle_ = qualified_ = false; enter(DoorState::Unknown, i.now, o); return o;
  }
  const bool stopped = i.motorPosition == i.targetPosition;
  const int64_t drift = int64_t(i.motorPosition) - i.encoderPosition;
  auto wait = [&](uint32_t ms) { o.motor = MotorAction::Release; retryPaused_ = retryActive_ = false; dwell_ = ms; enter(DoorState::Waiting, i.now, o); };
  auto prepare = [&](bool reopen) { o.motor = MotorAction::Release; enter(reopen ? DoorState::Reopen : DoorState::Open,i.now,o); };
  switch (state_) {
    case DoorState::Closed:
      if (open) { cycle_ = true; qualified_ = false;retries_ = 0;retryPaused_ = retryActive_ = false;prepare(false); }
      break;
    case DoorState::Open: case DoorState::Reopen:
      if (elapsed(i.now,entered_) >= motion_.settleMs) {
        bool reopen = state_ == DoorState::Reopen;
        retryPaused_ = retryActive_ = false;
        dwell_ = motion_.dwellMs; o.motor = reopen ? MotorAction::Reopen : MotorAction::Open;
        enter(reopen ? DoorState::Reopening : DoorState::Opening, i.now,o);
      }
      break;
    case DoorState::Opening: case DoorState::Reopening:
      if (elapsed(i.now,entered_) >= motion_.openTimeoutMs) return fault("Opening timeout");
      // The motor's commanded position is not proof of actual door travel.
      if (!i.limit && i.encoderPosition >= openingThreshold()) {
        qualified_ = cycle_;wait(state_ == DoorState::Reopening ? motion_.shortDwellMs : dwell_);
      } else if (retryPaused_) {
        if(elapsed(i.now,retryAt_) >= motion_.openRetryPauseMs) {
          retryPaused_ = false;retryActive_ = true;
          o.motor = state_ == DoorState::Reopening ? MotorAction::RetryReopen : MotorAction::RetryOpen;
        }
      } else if (stopped || drift > motion_.openDrift) {
        if(retries_ >= motion_.openRetryLimit) return fault("Opening retries exhausted");
        ++retries_;retryAt_ = i.now;retryPaused_ = true;retryActive_ = false;
        o.motor = MotorAction::Release;
      } else if (drift < -motion_.openDrift) { o.forced = true; wait(motion_.shortDwellMs); }
      // Beam state never prevents an opening attempt or consumes its retry budget.
      break;
    case DoorState::Waiting:
      // A currently broken beam must take priority over an expired dwell.
      if (i.beam) { entered_ = i.now; dwell_ = motion_.shortDwellMs; }
      else if (elapsed(i.now,entered_) >= dwell_) { o.motor = MotorAction::Release; enter(DoorState::Close,i.now,o); }
      break;
    case DoorState::Close:
      if (i.beam || open) prepare(true);
      else if (elapsed(i.now,entered_) >= motion_.settleMs) { o.motor = MotorAction::Close; enter(DoorState::Closing,i.now,o); }
      break;
    case DoorState::Closing:
      if (elapsed(i.now,entered_) >= motion_.closeTimeoutMs) return fault("Closing timeout");
      if (i.beam || drift < -motion_.closeDrift || open) prepare(true);
      else if (drift > motion_.closeDrift) { o.forced=true; wait(motion_.shortDwellMs); }
      else if (stopped) { o.motor = MotorAction::Home; enter(DoorState::Homing,i.now,o); }
      break;
    case DoorState::Homing:
      if (i.beam) return fault("Homing obstruction");
      if (elapsed(i.now,entered_) >= motion_.homingTimeoutMs) return fault("Homing timeout");
      break;
    default: break;
  }
  return o;
}
