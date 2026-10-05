#include "Config.h"
#include <Preferences.h>
#include <MotionFields.h>

void configToJson(const AppConfig &c, JsonDocument &doc) {
  doc.clear();
  doc["version"]=kConfigVersion;
  auto m=doc["motion"].to<JsonObject>();
#define WRITE(name) m[#name]=c.motion.name;
  MOTION_FIELDS(WRITE)
#undef WRITE
  auto q=doc["qlab"].to<JsonObject>(); q["enabled"]=c.qlab.enabled; q["host"]=c.qlab.host;
  q["port"]=c.qlab.port; q["workspace"]=c.qlab.workspace; q["passcode"]=c.qlab.passcode;
  q["closedExpiryMs"]=c.qlab.closedExpiryMs;
  auto events=q["events"].to<JsonObject>();
  for(size_t i=0;i<kEventCount;++i) { auto e=events[eventName(i)].to<JsonObject>(); e["enabled"]=c.qlab.events[i].enabled; e["cue"]=c.qlab.events[i].cue; }
}
namespace {
template<size_t N> bool text(JsonVariantConst value, char (&out)[N]) {
  if (!value.is<const char*>()) return false;
  const char *s=value.as<const char*>(); if(strlen(s)>=N) return false;
  for(const char *p=s;*p;++p) if(static_cast<unsigned char>(*p)<32) return false;
  strlcpy(out,s,N); return true;
}
bool readBool(JsonVariantConst v, bool &out) { if(!v.is<bool>()) return false; out=v.as<bool>(); return true; }
}
bool configFromJson(JsonVariantConst doc, AppConfig &out, String &error) {
  AppConfig c{};
  error="Missing field, incorrect type, unsupported version, or oversized string";
  if(!doc["version"].is<uint32_t>() || doc["version"].as<uint32_t>()!=kConfigVersion) return false;
  if(!doc["network"].isUnbound()) {
    error="Ethernet is DHCP-only; remove network settings and reload the current configuration";return false;
  }
  auto m=doc["motion"];
#define READ(name) if(!m[#name].is<decltype(c.motion.name)>()) return false; c.motion.name=m[#name].as<decltype(c.motion.name)>();
  MOTION_FIELDS(READ)
#undef READ
  auto q=doc["qlab"];
  if(!readBool(q["enabled"],c.qlab.enabled) || !text(q["host"],c.qlab.host) || !text(q["workspace"],c.qlab.workspace) ||
     !text(q["passcode"],c.qlab.passcode) || !q["port"].is<uint16_t>() || !q["closedExpiryMs"].is<uint32_t>()) return false;
  c.qlab.port=q["port"].as<uint16_t>(); c.qlab.closedExpiryMs=q["closedExpiryMs"].as<uint32_t>();
  for(size_t i=0;i<kEventCount;++i) {
    auto e=q["events"][eventName(i)];
    if(!readBool(e["enabled"],c.qlab.events[i].enabled) || !text(e["cue"],c.qlab.events[i].cue)) return false;
  }
  const char *why;
  if(!validateConfig(c,why)) { error=why; return false; }
  out=c; error=""; return true;
}
bool configLoad(AppConfig &c, bool rear) {
  c=defaultConfig(rear);
  Preferences p;
  if(!p.begin(rear?"door-rear":"door-front",true)) return false;
  size_t n=p.getString("config").length();
  if(!n || n>8192) { p.end(); return false; }
  String stored=p.getString("config"); p.end();
  JsonDocument doc; String error;
  if(deserializeJson(doc,stored)) return false;
  // Version 1 had configurable static IP fields. Retire those fields only;
  // retain validated motion settings, credentials, and cue mappings.
  if(doc["version"].is<uint32_t>() && doc["version"].as<uint32_t>()==1) {
    doc.remove("network");doc["version"]=kConfigVersion;
  }
  return configFromJson(doc,c,error);
}
bool configSave(const AppConfig &c) {
  const char *why; if(!validateConfig(c,why)) return false;
  JsonDocument doc; configToJson(c,doc); String json; serializeJson(doc,json);
  Preferences p;
#ifdef REAR_DOOR
  const char *ns="door-rear";
#else
  const char *ns="door-front";
#endif
  if(!p.begin(ns,false)) return false;
  bool ok=p.putString("config",json)==json.length(); p.end(); return ok;
}
