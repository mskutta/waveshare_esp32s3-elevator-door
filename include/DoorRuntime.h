#pragma once
#include <Arduino.h>
#include <Types.h>
struct DoorStatus {
  DoorState state;
  bool homed, limit, beam, energized, healthy, maintenance, upOutput, downOutput;
  bool pendingClosed;
  int32_t encoderPosition, motorPosition, targetPosition;
  int64_t encoderCounts;
  uint32_t pendingAgeMs, pendingGeneration, lostLiveEvents, cycles;
  char fault[48];
};
struct DoorEvent { uint8_t event; uint32_t at; };
bool doorBegin(const MotionConfig &, uint32_t closedExpiryMs);
DoorStatus doorStatus();
void doorOpenRequest();
void doorResetFault();
bool doorEvent(DoorEvent &);
void doorAcknowledgeClosed(uint32_t generation);
// Network task reserves a non-moving controller before persistence; timeout
// auto-releases a lost reservation without ever starting movement.
bool doorReserve(bool changingMotion);
bool doorCommit(const MotionConfig &, uint32_t closedExpiryMs);
void doorReleaseReservation();
void doorNetworkStatus(bool ready, bool qlab, IPAddress ip);
