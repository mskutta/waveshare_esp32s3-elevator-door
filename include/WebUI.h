#pragma once
#include <Arduino.h>
#include <Types.h>
void webBegin(AppConfig &);
void webLoop();
bool saveConfiguration(const AppConfig &, String &error);
