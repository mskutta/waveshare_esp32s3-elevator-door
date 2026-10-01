#pragma once
#include "Arduino.h"
#include <map>
class Preferences {
public:
  inline static std::map<String,std::map<String,String>> storage;
  inline static bool failWrite=false;
  bool begin(const char *name,bool readonly=false) {name_=name;readonly_=readonly;return !readonly || storage.count(name_);}
  String getString(const char *key) {return storage[name_][key];}
  size_t putString(const char *key,const String &value) {if(readonly_ || failWrite)return 0;storage[name_][key]=value;return value.length();}
  void end(){}
private:String name_;bool readonly_=false;
};
