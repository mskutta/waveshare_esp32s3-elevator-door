#pragma once
namespace Pins {
// Physical header pins 10 (SDA) and 11 (SCL); no camera attached.
constexpr int sda = 41, scl = 40;
// Adjacent physical header pins 4 (A) and 5 (B).
constexpr int encoderA = 48, encoderB = 47;
constexpr int limit = 21, beam = 1;
}
#if defined(FRONT_DOOR) == defined(REAR_DOOR)
#error Select exactly one door build
#endif
#ifdef FRONT_DOOR
constexpr bool kRear = false;
constexpr const char *kDoorName = "elev-door-front";
constexpr const char *kOpenAddress = "/elev-door-front/door/open";
#else
constexpr bool kRear = true;
constexpr const char *kDoorName = "elev-door-rear";
constexpr const char *kOpenAddress = "/elev-door-rear/door/open";
#endif
