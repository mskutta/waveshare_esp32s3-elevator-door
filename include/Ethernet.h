#pragma once
#include <Arduino.h>
bool ethernetBegin(const char *hostname);
bool ethernetRenewDhcp();
bool ethernetReady();
IPAddress ethernetIP();
const char *ethernetError();
