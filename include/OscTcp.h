#pragma once
#include <Arduino.h>
#include <Types.h>
struct OscStatus {bool connected, ready;uint32_t accepted, rejected, reconnects, sends, acknowledgments, failures;char error[64];};
void oscBegin();
void oscConfigure(const QLabConfig &);
void oscLoop();
OscStatus oscStatus();
#ifdef ELEVATOR_NATIVE_TEST
uint16_t oscTestListenPort();
#endif
