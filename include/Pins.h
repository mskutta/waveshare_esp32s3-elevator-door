#pragma once
namespace Pins {
constexpr int sda = 17, scl = 18;
constexpr int encoderA = 15, encoderB = 16;
constexpr int limit = 21, beam = 1;
constexpr int upButton = 38, downButton = 39;
constexpr int outputUp = 40, outputDown = 41;
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
