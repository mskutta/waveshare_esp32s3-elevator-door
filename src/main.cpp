#include <Arduino.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp32-hal-bt.h>
#include "Pins.h"
#include "Config.h"
#include "DoorRuntime.h"
#include "Ethernet.h"
#include "OscTcp.h"
#include "WebUI.h"

namespace {
AppConfig config;
bool controlStarted=false;
char command[48]{};size_t commandSize=0;bool commandOverflow=false;
void serialLoop() {
  for(int budget=0;budget<64 && Serial.available();++budget) {
    char c=Serial.read();
    if(c=='\r') continue;
    if(c=='\n') {
      command[commandSize]=0;
      if(commandOverflow) Serial.println("Command too long");
      else if(!strcmp(command,"network-reset")) {
        if(ethernetRenewDhcp()) Serial.println("DHCP renewal requested; settings unchanged");
        else Serial.printf("DHCP renewal failed: %s\n",ethernetError());
      } else if(!strcmp(command,"status")) {
        auto s=doorStatus();Serial.printf("%s state=%s encoder=%ld motor=%ld limit=%u beam=%u mcp=%u buttons=%u/%u pending=%u fault=%s ip=%s\n",
          kDoorName,eventName(static_cast<size_t>(s.state)),static_cast<long>(s.encoderPosition),static_cast<long>(s.motorPosition),s.limit,s.beam,s.mcpHealthy,s.upButton,s.downButton,s.pendingClosed,s.fault,ethernetIP().toString().c_str());
      } else if(commandSize) Serial.println("Commands: status, network-reset");
      commandSize=0;commandOverflow=false;
    } else if(commandSize<sizeof(command)-1) command[commandSize++]=c;
    else commandOverflow=true;
  }
}
}
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);btStop();esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
  bool loaded=configLoad(config,kRear);
  Serial.printf("%s: %s settings; Ethernet only\n",kDoorName,loaded?"saved":"default");
  controlStarted=doorBegin(config.motion,config.qlab.closedExpiryMs);
  if(!controlStarted) {Serial.println("FATAL: door task/queue allocation failed. Motor must remain inhibited.");return;}
  // getEfuseMac stores MAC bytes little-endian; use the device-specific final
  // three bytes, rather than the shared manufacturer prefix.
  char hostname[40];snprintf(hostname,sizeof(hostname),"%s-%06lX",kDoorName,static_cast<unsigned long>((ESP.getEfuseMac()>>24)&0xffffff));
  if(!ethernetBegin(hostname)) Serial.printf("Ethernet startup failed: %s; door task continues\n",ethernetError());
  oscBegin();oscConfigure(config.qlab);webBegin(config);
  Serial.println("OSC TCP/SLIP :53000; web :80; USB commands: status, network-reset");
}
void loop() {
  if(!controlStarted) {delay(1000);return;}
  oscLoop();webLoop();serialLoop();
  static DoorState previous=DoorState::Unknown;
  auto s=doorStatus();
  if(s.state!=previous) {Serial.printf("Door: %s (%s)\n",eventName(static_cast<size_t>(s.state)),s.fault);previous=s.state;}
  delay(2);
}
