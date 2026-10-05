#pragma once
#include <stdint.h>
#include <stddef.h>

enum class DoorState : uint8_t { Unknown, Homing, Close, Closed, Closing, Open, Opening, Reopen, Reopening, Waiting, Fault };
constexpr size_t kStateCount = 11;
constexpr size_t kForcedEvent = kStateCount;
constexpr size_t kBeamBreakEvent = kForcedEvent + 1;
constexpr size_t kEventCount = kStateCount + 2;
const char *eventName(size_t event);
inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

struct MotionConfig {
  int32_t travel;
  uint32_t openSpeed, reopenSpeed, closeSpeed, homingSpeed;
  uint32_t openAccel, openDecel, closeAccel, closeDecel, homingAccel, homingDecel;
  uint16_t openCurrent, closeCurrent, homingCurrent;
  uint32_t dwellMs, shortDwellMs, settleMs, openTimeoutMs, closeTimeoutMs, homingTimeoutMs;
  int32_t closeDrift, openDrift;
  uint8_t openRetryLimit, openRetryDivisor;
  uint32_t openRetryPauseMs;
  int32_t openCompletionTolerance;
};
struct CueMapping { bool enabled; char cue[40]; };
struct QLabConfig {
  bool enabled;
  char host[64], workspace[40], passcode[64];
  uint16_t port;
  uint32_t closedExpiryMs;
  CueMapping events[kEventCount];
};
struct AppConfig { MotionConfig motion; QLabConfig qlab; };
AppConfig defaultConfig(bool rear);
bool validateConfig(const AppConfig &, const char *&error);
bool sameMotion(const MotionConfig &, const MotionConfig &);
inline int64_t encoderToSteps(int64_t counts) { return counts * 1600 / 600; }

struct DoorInput {
  uint32_t now;
  bool limit, beam, ticHealthy, energized;
  int32_t motorPosition, targetPosition, encoderPosition;
};
enum class MotorAction : uint8_t { None, Release, Open, Reopen, Close, Home, RetryOpen, RetryReopen };
struct MotorSettings {
  bool opening, homing;
  uint32_t speed, acceleration, deceleration;
  uint16_t current;
};
MotorSettings motorSettings(const MotionConfig &, MotorAction);
struct DoorOutput {
  MotorAction motor = MotorAction::None;
  bool zeroEncoder = false;
  bool closedCycle = false;
  bool forced = false;
  bool changed = false;
};
