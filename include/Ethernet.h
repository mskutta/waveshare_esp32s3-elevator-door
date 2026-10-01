#pragma once
#include <Arduino.h>
#include <Types.h>
bool ethernetBegin(const char *hostname, const NetworkConfig &);
bool ethernetConfigure(const NetworkConfig &);
bool ethernetReady();
IPAddress ethernetIP();
const char *ethernetError();
