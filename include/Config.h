#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Types.h>
constexpr uint32_t kConfigVersion = 1;
void configToJson(const AppConfig &, JsonDocument &);
bool configFromJson(JsonVariantConst, AppConfig &, String &error);
bool configLoad(AppConfig &, bool rear);
bool configSave(const AppConfig &);
