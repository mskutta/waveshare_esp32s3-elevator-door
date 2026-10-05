#include "WebUI.h"
#include "WebPage.h"
#include "Config.h"
#include "DoorRuntime.h"
#include "OscTcp.h"
#include "Ethernet.h"
#include "Pins.h"
#include <HttpRequest.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
namespace {
int listener=-1,client=-1;
AppConfig *config=nullptr;
HttpRequest request;
String responseBody;
const char *body=nullptr;
size_t bodyLength=0,bodyOffset=0,headerLength=0,headerOffset=0;
char header[192];
uint32_t clientAt=0;
void closeClient(){if(client>=0)close(client);client=-1;body=nullptr;responseBody="";request.reset();}
bool nonblocking(int fd){return fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)==0;}
void respond(int code,const char *type,const char *text) {
  body=text;bodyLength=strlen(text);bodyOffset=headerOffset=0;
  const char *reason=code==200?"OK":code==202?"Accepted":code==400?"Bad Request":code==404?"Not Found":code==409?"Conflict":code==413?"Content Too Large":"Error";
  headerLength=snprintf(header,sizeof(header),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",code,reason,type,static_cast<unsigned>(bodyLength));
}
void jsonReply(int code,JsonDocument &doc){responseBody="";serializeJson(doc,responseBody);respond(code,"application/json",responseBody.c_str());}
void failure(int code,const String &error){JsonDocument doc;doc["error"]=error;jsonReply(code,doc);}
void state() {
  auto s=doorStatus();auto o=oscStatus();JsonDocument doc;
  doc["door"]=kDoorName;doc["state"]=eventName(static_cast<size_t>(s.state));
  doc["homed"]=s.homed;doc["limit"]=s.limit;doc["beam"]=s.beam;doc["energized"]=s.energized;
  doc["mcpHealthy"]=s.mcpHealthy;doc["upButtonPressed"]=s.upButton;doc["downButtonPressed"]=s.downButton;
  doc["ticHealthy"]=s.healthy;doc["maintenance"]=s.maintenance;doc["upOutputActive"]=s.upOutput;doc["downOutputActive"]=s.downOutput;
  doc["openingRetries"]=s.openingRetries;doc["openingRetryLimit"]=s.openingRetryLimit;
  doc["openingRetryPaused"]=s.openingRetryPaused;doc["openingRetryActive"]=s.openingRetryActive;
  doc["closedCueEligible"]=s.closedCueEligible;doc["openingThreshold"]=s.openingThreshold;
  doc["encoderCounts"]=s.encoderCounts;doc["encoderPosition"]=s.encoderPosition;doc["motorPosition"]=s.motorPosition;doc["targetPosition"]=s.targetPosition;
  doc["fault"]=s.fault;doc["pendingClosed"]=s.pendingClosed;doc["pendingAgeMs"]=s.pendingAgeMs;
  doc["pendingGeneration"]=s.pendingGeneration;doc["cycles"]=s.cycles;doc["droppedLiveEvents"]=s.lostLiveEvents;
  doc["ethernet"]=ethernetReady();doc["ip"]=ethernetIP().toString();doc["ethernetError"]=ethernetError();
#ifdef MOTOR_INHIBITED
  doc["motorInhibited"]=true;
#else
  doc["motorInhibited"]=false;
#endif
  auto osc=doc["osc"].to<JsonObject>();osc["connected"]=o.connected;osc["ready"]=o.ready;osc["error"]=o.error;
  osc["acceptedCommands"]=o.accepted;osc["rejectedFrames"]=o.rejected;osc["reconnects"]=o.reconnects;
  osc["cueSends"]=o.sends;osc["cueAcknowledgments"]=o.acknowledgments;osc["failures"]=o.failures;
  jsonReply(200,doc);
}
void dispatch() {
  const char *path=request.path(),*method=request.method();
  if(!strcmp(method,"GET")&&!strcmp(path,"/"))respond(200,"text/html; charset=utf-8",kWebPage);
  else if(!strcmp(method,"GET")&&!strcmp(path,"/api/state"))state();
  else if(!strcmp(method,"GET")&&!strcmp(path,"/api/config")){JsonDocument doc;configToJson(*config,doc);jsonReply(200,doc);}
  else if(!strcmp(method,"POST")&&!strcmp(path,"/api/door/open")){doorOpenRequest();respond(202,"application/json","{\"requested\":true}");}
  else if(!strcmp(method,"POST")&&!strcmp(path,"/api/fault/reset")){doorResetFault();respond(202,"application/json","{\"requested\":true}");}
  else if(!strcmp(method,"PUT")&&!strcmp(path,"/api/config")) {
    JsonDocument doc;String error;
    if(deserializeJson(doc,request.body(),request.bodySize(),DeserializationOption::NestingLimit(5))){failure(400,"Invalid JSON");return;}
    AppConfig next{};
    if(!configFromJson(doc.as<JsonVariantConst>(),next,error)){failure(400,error);return;}
    if(!saveConfiguration(next,error)){failure(409,error);return;}
    respond(200,"application/json","{\"saved\":true}");
  } else failure(404,"Unknown endpoint");
}
void listenHTTP() {
  listener=socket(AF_INET,SOCK_STREAM,0);if(listener<0)return;
  int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(80);addr.sin_addr.s_addr=INADDR_ANY;
  if(!nonblocking(listener)||bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))||listen(listener,2)){close(listener);listener=-1;}
}
}
bool saveConfiguration(const AppConfig &next,String &error) {
  const char *why;
  if(!validateConfig(next,why)){error=why;return false;}
  bool changing=!sameMotion(config->motion,next.motion);
  if(!doorReserve(changing)){error=changing?"Motion changes require a healthy, confirmed closed, released door":"Configuration save requires a healthy Tic and an idle, released door";return false;}
  if(!configSave(next)){doorReleaseReservation();error="Persistent storage write failed";return false;}
  if(!doorCommit(next.motion,next.qlab.closedExpiryMs)){
    bool restored=configSave(*config);doorReleaseReservation();
    error=restored?"Controller did not accept settings; previous settings restored":"Controller did not accept settings; storage rollback failed. Inspect saved settings before rebooting";
    return false;
  }
  *config=next;oscConfigure(config->qlab);error="";return true;
}
void webBegin(AppConfig &c){config=&c;}
void webLoop() {
  if(!ethernetReady()) {
    closeClient();if(listener>=0){close(listener);listener=-1;}
    if(!ethernetReady())return;
  }
  if(listener<0)listenHTTP();
  if(client<0&&listener>=0) {
    client=accept(listener,nullptr,nullptr);
    if(client>=0){if(!nonblocking(client)){closeClient();return;}clientAt=millis();request.reset();}
  }
  if(client>=0) {
    if(elapsed(millis(),clientAt)>=5000){closeClient();}
    else if(!body) {
      uint8_t buffer[512];int n=recv(client,buffer,sizeof(buffer),0);
      if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){closeClient();}
      for(int i=0;i<n&&client>=0;++i){auto result=request.feed(buffer[i]);
        if(result==HttpResult::Ready){dispatch();break;}
        if(result!=HttpResult::More){failure(result==HttpResult::TooLarge?413:400,"Invalid or oversized HTTP request");break;}
      }
    } else {
      const char *data;size_t remaining;
      bool headers=headerOffset<headerLength;
      if(headers){data=header+headerOffset;remaining=headerLength-headerOffset;}else{data=body+bodyOffset;remaining=bodyLength-bodyOffset;}
      if(remaining>1024)remaining=1024;
      int n=send(client,data,remaining,0);
      if(n>0){if(headers)headerOffset+=n;else bodyOffset+=n;}
      else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)closeClient();
      if(client>=0&&headerOffset==headerLength&&bodyOffset==bodyLength)closeClient();
    }
  }
}
