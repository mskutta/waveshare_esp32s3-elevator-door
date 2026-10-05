#include "Types.h"
#include <string.h>
#include <math.h>
#include <initializer_list>
#include "MotionFields.h"

const char *eventName(size_t e) {
  static const char *names[] = {"unknown", "homing", "close", "closed", "closing", "open", "opening", "reopen", "reopening", "waiting", "fault", "forced"};
  return e < kEventCount ? names[e] : "invalid";
}
AppConfig defaultConfig(bool rear) {
  AppConfig c{};
  c.motion = {18600, rear ? 20000000u : 90000000u, 90000000, 30000000, 10000000,
    400000, 700000, 100000, 100000, 100000, 100000, 1500, 900, 1500,
    rear ? 5000u : 600000u, 2000, 250, 30000, 30000, 5000, 64, 128, 2, 3, 1000, 128};
  c.qlab.port = 53000; c.qlab.closedExpiryMs = 30000;
  return c;
}
static bool segment(const char *s, bool uuid = false) {
  if (!*s) return false;
  for (; *s; ++s) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
      (*s >= '0' && *s <= '9') || *s == '-' || (!uuid && (*s == '.' || *s == '_')))) return false;
  return true;
}
bool sameMotion(const MotionConfig &a, const MotionConfig &b) {
#define COMPARE(name) if(a.name!=b.name) return false;
  MOTION_FIELDS(COMPARE)
#undef COMPARE
  return true;
}
MotorSettings motorSettings(const MotionConfig &m,MotorAction action) {
  bool retry=action==MotorAction::RetryOpen || action==MotorAction::RetryReopen;
  bool reopen=action==MotorAction::Reopen || action==MotorAction::RetryReopen;
  bool opening=action==MotorAction::Open || reopen || retry;
  bool home=action==MotorAction::Home;
  uint32_t divisor=retry?m.openRetryDivisor:1;
  return {opening,home,
    home?m.homingSpeed:opening?(reopen?m.reopenSpeed:m.openSpeed)/divisor:m.closeSpeed,
    home?m.homingAccel:opening?m.openAccel/divisor:m.closeAccel,
    home?m.homingDecel:opening?m.openDecel:m.closeDecel,
    home?m.homingCurrent:opening?m.openCurrent:m.closeCurrent};
}
bool validateConfig(const AppConfig &c, const char *&error) {
  const auto &m = c.motion;
  error = "invalid motion settings";
  if (m.travel < 160 || m.travel > 1000000 || m.closeDrift < 1 || m.openDrift < 1 ||
      m.closeDrift >= m.travel || m.openDrift >= m.travel) return false;
  for (auto v : {m.openSpeed, m.reopenSpeed, m.closeSpeed, m.homingSpeed}) if (v < 10000 || v > 500000000) return false;
  for (auto v : {m.openAccel, m.openDecel, m.closeAccel, m.closeDecel, m.homingAccel, m.homingDecel}) if (v < 100 || v > 2147483647u) return false;
  for (auto v : {m.openCurrent, m.closeCurrent, m.homingCurrent}) if (v < 100 || v > 3093) return false;
  for (auto v : {m.dwellMs, m.shortDwellMs}) if (v < 100 || v > 3600000) return false;
  if (m.settleMs > 5000) return false;
  for (auto v : {m.openTimeoutMs, m.closeTimeoutMs, m.homingTimeoutMs}) if (v < 100 || v > 600000) return false;
  error = "invalid opening retry/completion settings";
  if(m.openRetryLimit>3 || m.openRetryDivisor<1 || m.openRetryDivisor>10 ||
     m.openRetryPauseMs<100 || m.openRetryPauseMs>5000 ||
     m.openCompletionTolerance<1 || m.openCompletionTolerance>m.travel/20) return false;
  if(m.openRetryLimit && (m.openSpeed/m.openRetryDivisor<10000 ||
     m.reopenSpeed/m.openRetryDivisor<10000 || m.openAccel/m.openRetryDivisor<100)) return false;
  error = "invalid motion settings";
  // Reject deadlines below even an ideal, zero-start-speed trapezoidal move.
  auto minimumMs = [&](uint32_t speed, uint32_t accel, uint32_t decel) {
    double v = speed / 10000.0, a = accel / 100.0, d = decel / 100.0;
    double ramp = v*v/(2*a) + v*v/(2*d);
    double seconds = ramp <= m.travel ? v/a + v/d + (m.travel-ramp)/v :
      sqrt(2.0*m.travel/(1.0/a+1.0/d))*(1.0/a+1.0/d);
    return seconds * 1000.0;
  };
  if (m.openTimeoutMs <= minimumMs(m.openSpeed,m.openAccel,m.openDecel) ||
      m.openTimeoutMs <= minimumMs(m.reopenSpeed,m.openAccel,m.openDecel) ||
      m.closeTimeoutMs <= minimumMs(m.closeSpeed,m.closeAccel,m.closeDecel)) return false;
  error = "invalid QLab settings";
  if (!c.qlab.port || c.qlab.closedExpiryMs < 100 || c.qlab.closedExpiryMs > 3600000) return false;
  if (c.qlab.enabled && !c.qlab.host[0]) {error="QLab host is required when cue triggers are enabled";return false;}
  if (c.qlab.enabled && !segment(c.qlab.workspace,true)) {error="QLab workspace ID is required when cue triggers are enabled; copy the unique ID from QLab Workspace Status > Info";return false;}
  for (const char *p=c.qlab.host; *p; ++p) if (!( (*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || *p=='.' || *p=='-')) return false;
  for (auto &mapping : c.qlab.events) if (mapping.enabled && !segment(mapping.cue)) return false;
  error = nullptr; return true;
}
