#include "OscTcp.h"
#include "Pins.h"
#include "Ethernet.h"
#include "DoorRuntime.h"
#include <OscCodec.h>
#include <QLabSession.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

namespace {
struct InputClient {int fd=-1;SlipDecoder decoder;uint32_t fragmentAt=0;bool fragment=false;};
InputClient clients[2];
int listener=-1,output=-1;
bool connecting=false;
uint32_t connectAt=0,retryAt=0;
QLabConfig config{};
QLabSession session;
SlipDecoder replies;
OscStatus status{};
uint8_t tx[1024];size_t txSize=0,txOffset=0;
uint32_t inflightGeneration=0;
uint32_t acknowledgedGeneration=0;
void error(const char *why) {strlcpy(status.error,why,sizeof(status.error));++status.failures;}
void closeOutput(uint32_t backoff=2500) {
  if(output>=0) {close(output);output=-1;}
  connecting=false;session.disconnected();replies.reset();txSize=txOffset=0;inflightGeneration=0;
  status.connected=status.ready=false;retryAt=millis()+backoff;
}
bool due(uint32_t now,uint32_t deadline) {return static_cast<int32_t>(now-deadline)>=0;}
bool appendMessage(const char *address,const char *arg=nullptr,bool integer=false) {
  uint8_t message[256];size_t n=integer?encodeOscInt(message,sizeof(message),address,1):encodeOsc(message,sizeof(message),address,arg);
  if(!n) return false;
  size_t framed=encodeSlip(tx+txSize,sizeof(tx)-txSize,message,n);
  if(!framed) return false;
  txSize+=framed;return true;
}
void authenticated() {
  if(!appendMessage("/alwaysReply",nullptr,true) || !appendMessage("/alwaysReply")) {error("Setup frame overflow");closeOutput();}
}
void connected(uint32_t now) {
  connecting=false;status.connected=true;session.connected(config,now);
  if(!appendMessage(session.method(),config.passcode[0]?config.passcode:nullptr)) {error("Connect frame overflow");closeOutput();}
}
bool nonblocking(int fd) {return fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)==0;}
void receiveReply(const uint8_t *data,size_t n) {
  OscView view;
  if(!decodeOsc(data,n,view) || !view.argument || strncmp(view.address,"/reply/",7)) {++status.rejected;return;}
  JsonDocument json;
  if(deserializeJson(json,view.argument) || !json["address"].is<const char*>() || !json["status"].is<const char*>()) {++status.rejected;return;}
  const char *method=json["address"];
  // Both the OSC envelope and JSON must identify the invoked method.
  if(strcmp(view.address+6,method)) {++status.rejected;return;}
  QLabStage previous=session.stage();
  uint32_t generation=session.reply(method,json["workspace_id"]|"",json["status"],json["data"].is<const char*>()?json["data"].as<const char*>():nullptr,
    json["data"].is<int>() && json["data"].as<int>()!=0,millis());
  if(session.stage()==QLabStage::Failed) {error("QLab denied/error/badpass");closeOutput(30000);return;}
  if(previous==QLabStage::Authenticate && session.stage()==QLabStage::EnableReplies) authenticated();
  if(session.ready()) status.error[0]=0;
  if(previous==QLabStage::Cue && session.ready()) {
    ++status.acknowledgments;inflightGeneration=0;
    if(generation) {acknowledgedGeneration=generation;doorAcknowledgeClosed(generation);}
  }
}
void serveInput(uint32_t now) {
  if(listener<0) {
    listener=socket(AF_INET,SOCK_STREAM,0);
    if(listener<0) {error("OSC listener socket");return;}
    int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(53000);addr.sin_addr.s_addr=INADDR_ANY;
    if(!nonblocking(listener) || bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) || listen(listener,2)) {
      error("OSC listen/bind");close(listener);listener=-1;return;
    }
  }
  int incoming=accept(listener,nullptr,nullptr);
  if(incoming>=0) {
    bool placed=false;
    for(auto &c:clients) if(c.fd<0 && nonblocking(incoming)) {c.fd=incoming;c.decoder.reset();c.fragment=false;placed=true;break;}
    if(!placed) close(incoming);
  }
  for(auto &c:clients) {
    if(c.fd<0) continue;
    if(c.fragment && elapsed(now,c.fragmentAt)>=2000) {close(c.fd);c.fd=-1;++status.rejected;continue;}
    uint8_t buf[512];int n=recv(c.fd,buf,sizeof(buf),0);
    if(n==0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)) {close(c.fd);c.fd=-1;continue;}
    for(int i=0;i<n;++i) {
      if(buf[i]!=0xC0 && !c.fragment) {c.fragment=true;c.fragmentAt=now;}
      uint32_t rejectedBefore=c.decoder.rejected();size_t length=c.decoder.feed(buf[i]);
      status.rejected+=c.decoder.rejected()-rejectedBefore;
      if(buf[i]==0xC0) c.fragment=false;
      if(!length) continue;
      OscView message;
      if(decodeOsc(c.decoder.data(),length,message) && !message.argument && !strcmp(message.address,kOpenAddress)) {
        doorOpenRequest();++status.accepted;
      } else ++status.rejected;
    }
  }
}
void startOutput(uint32_t now) {
  addrinfo hints{},*resolved=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
  // DNS may wait on core 0. Door control and PCNT run independently on core 1.
  if(getaddrinfo(config.host,nullptr,&hints,&resolved) || !resolved) {error("QLab hostname lookup");closeOutput();return;}
  sockaddr_in address=*reinterpret_cast<sockaddr_in*>(resolved->ai_addr);freeaddrinfo(resolved);address.sin_port=htons(config.port);
  output=socket(AF_INET,SOCK_STREAM,0);
  if(output<0 || !nonblocking(output)) {error("QLab socket");closeOutput();return;}
  ++status.reconnects;connectAt=now;
  int result=connect(output,reinterpret_cast<sockaddr*>(&address),sizeof(address));
  if(result==0) connected(now);
  else if(errno==EINPROGRESS) connecting=true;
  else {error("QLab connect");closeOutput();}
}
bool startCue(const char *cue,uint32_t generation,uint32_t now) {
  if(!session.startCue(cue,generation,now)) return false;
  if(!appendMessage(session.method())) {error("Cue frame overflow");closeOutput();return false;}
  inflightGeneration=generation;++status.sends;return true;
}
}
void oscBegin() {retryAt=millis();}
void oscConfigure(const QLabConfig &c) {
  config=c;closeOutput(0);
  for(auto &input:clients) if(input.fd>=0) {close(input.fd);input.fd=-1;}
  if(listener>=0) {close(listener);listener=-1;}
}
OscStatus oscStatus() {status.ready=session.operational();return status;}
void oscLoop() {
  uint32_t now=millis();DoorStatus door=doorStatus();
  if(!ethernetReady()) {
    for(auto &c:clients) if(c.fd>=0) {close(c.fd);c.fd=-1;}
    if(listener>=0) {close(listener);listener=-1;}
    if(output>=0) closeOutput();
    DoorEvent e;while(doorEvent(e)){};
    doorNetworkStatus(false,false,ethernetIP());return;
  }
  serveInput(now);
  door=doorStatus();
  if(!config.enabled) {if(output>=0)closeOutput();}
  else if(output<0 && due(now,retryAt)) startOutput(now);
  if(connecting) {
    fd_set writes;FD_ZERO(&writes);FD_SET(output,&writes);timeval wait{};
    int result=select(output+1,nullptr,&writes,nullptr,&wait);
    if(result>0) {int e=0;socklen_t len=sizeof(e);getsockopt(output,SOL_SOCKET,SO_ERROR,&e,&len);if(e){error("QLab connect refused");closeOutput();}else connected(now);}
    else if(result<0 || elapsed(now,connectAt)>=2000) {error("QLab connect timeout");closeOutput();}
  }
  // Cancel unsent/retrying closed data as soon as its slot becomes ineligible.
  if(inflightGeneration && (!door.pendingClosed || door.pendingGeneration!=inflightGeneration)) closeOutput(0);
  if(output>=0 && !connecting) {
    if(txOffset<txSize) {
      int n=send(output,tx+txOffset,txSize-txOffset,0);
      if(n>0) txOffset+=n;
      else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK) {error("QLab send");closeOutput();}
      if(txOffset==txSize) txOffset=txSize=0;
    }
    uint8_t buf[512];int n=output>=0?recv(output,buf,sizeof(buf),0):-1;
    if(output>=0 && (n==0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK))) {error("QLab disconnected");closeOutput();}
    for(int i=0;i<n && output>=0;++i) {size_t length=replies.feed(buf[i]);if(length)receiveReply(replies.data(),length);}
    if(session.expired(now)) {error("QLab reply timeout");closeOutput();}
  }
  auto &closedMapping=config.events[static_cast<size_t>(DoorState::Closed)];
  if(session.ready() && !txSize && door.pendingClosed && door.pendingGeneration!=acknowledgedGeneration && closedMapping.enabled)
    startCue(closedMapping.cue,door.pendingGeneration,now);
  DoorEvent event;
  // Only closed has offline/retry storage. Live events are never retained here.
  if(!session.operational()) {while(doorEvent(event)){};}
  else if(session.ready() && !txSize) while(doorEvent(event)) {
    if(event.event<kEventCount && config.events[event.event].enabled && elapsed(now,event.at)<1000) {
      startCue(config.events[event.event].cue,0,now);break;
    }
  }
  status.ready=session.operational();doorNetworkStatus(true,status.ready,ethernetIP());
}
