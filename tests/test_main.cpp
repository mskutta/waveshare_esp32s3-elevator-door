#include <DoorMachine.h>
#include <ClosedTrigger.h>
#include <OscCodec.h>
#include <QLabSession.h>
#include <HttpRequest.h>
#include "Config.h"
#include "OscTcp.h"
#include "DoorRuntime.h"
#include "Ethernet.h"
#include "WebUI.h"
#include <Preferences.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <deque>
#include <random>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

uint32_t testMillis=0;
static DoorStatus fakeDoor{};
static bool ethOnline=true;
static uint32_t opens=0,acknowledged=0;
static std::deque<DoorEvent> live;
static bool reserveAllowed=true,commitAllowed=true,reserved=false;
static unsigned resetRequests=0,networkChanges=0;
DoorStatus doorStatus(){return fakeDoor;}
void doorOpenRequest(){++opens;}
void doorAcknowledgeClosed(uint32_t g){acknowledged=g;}
bool doorEvent(DoorEvent &e){if(live.empty())return false;e=live.front();live.pop_front();return true;}
void doorNetworkStatus(bool,bool,IPAddress){}
bool ethernetReady(){return ethOnline;}
IPAddress ethernetIP(){return IPAddress(0x0100007f);}
const char *ethernetError(){return "";}
bool ethernetConfigure(const NetworkConfig &){++networkChanges;return true;}
void doorResetFault(){++resetRequests;}
bool doorReserve(bool changing){reserved=reserveAllowed&&(!changing||fakeDoor.state==DoorState::Closed);return reserved;}
bool doorCommit(const MotionConfig &,uint32_t){if(!commitAllowed)return false;reserved=false;return true;}
void doorReleaseReservation(){reserved=false;}

static DoorInput input(uint32_t now=0){return {now,false,false,true,false,0,0,0};}
static void stateTests() {
  for(bool rear:{false,true}) {
    auto c=defaultConfig(rear);const char *error;assert(validateConfig(c,error));
    assert(c.motion.dwellMs==(rear?5000:600000));assert(c.motion.openSpeed==(rear?20000000:90000000));
    DoorMachine door(c.motion,rear);auto i=input(0xfffffff0);
    assert(door.tick(i,true).motor==MotorAction::None);assert(!door.homed());
    i.limit=true;auto o=door.tick(i);assert(o.zeroEncoder && !o.closedCycle);assert(door.canTune(i));
    i.energized=true;assert(!door.canTune(i)&&!door.canMaintain(i));i.energized=false;
    i.ticHealthy=false;assert(!door.canTune(i)&&!door.canMaintain(i));i.ticHealthy=true;
    i.now+=10;o=door.tick(i,true);assert(door.state()==DoorState::Open && o.motor==MotorAction::Release);
    i.now+=c.motion.settleMs;o=door.tick(i,true);assert(o.motor==MotorAction::Open);
    i.limit=false;i.motorPosition=500;i.targetPosition=c.motion.travel;i.encoderPosition=500;
    i.now+=5;assert(door.tick(i,true).motor==MotorAction::None);assert(door.state()==DoorState::Opening);
    i.motorPosition=i.encoderPosition=i.targetPosition;i.now+=100;
    o=door.tick(i);assert(door.state()==DoorState::Waiting && o.motor==MotorAction::Release);
    i.now+=c.motion.dwellMs-1;door.tick(i,true);assert(door.state()==DoorState::Waiting);
    i.now+=1;i.beam=true;door.tick(i);assert(door.state()==DoorState::Waiting);
    i.beam=false;i.now+=c.motion.shortDwellMs;door.tick(i);assert(door.state()==DoorState::Close);
    i.now+=c.motion.settleMs;o=door.tick(i);assert(o.motor==MotorAction::Close);
    i.motorPosition=15000;i.encoderPosition=15000;i.targetPosition=0;i.now+=5;
    i.beam=true;o=door.tick(i);assert(door.state()==DoorState::Reopen && o.motor==MotorAction::Release);
    i.beam=false;i.now+=c.motion.settleMs;o=door.tick(i);assert(o.motor==MotorAction::Reopen);
    i.motorPosition=i.encoderPosition=i.targetPosition=c.motion.travel;i.now+=5;door.tick(i);
    i.now+=c.motion.shortDwellMs;door.tick(i);i.now+=c.motion.settleMs;door.tick(i);
    i.motorPosition=i.encoderPosition=i.targetPosition=0;i.now+=5;o=door.tick(i);assert(o.motor==MotorAction::Home);
    i.limit=true;i.now+=5;o=door.tick(i);assert(o.closedCycle && o.zeroEncoder && door.state()==DoorState::Closed);
    i.limit=false;i.now+=5;door.tick(i);assert(door.state()==DoorState::Unknown && !door.homed());
    i.limit=true;i.now+=5;assert(!door.tick(i).closedCycle);
  }
  auto c=defaultConfig(false);DoorMachine door(c.motion,false);auto i=input();i.limit=true;door.tick(i);
  door.tick(i,true);i.now=250;door.tick(i);i.limit=false;i.targetPosition=c.motion.travel;i.motorPosition=i.encoderPosition=500;
  i.now+=c.motion.openTimeoutMs;auto o=door.tick(i);assert(door.state()==DoorState::Fault && o.motor==MotorAction::Release);
  i.energized=true;door.tick(i,false,true);assert(door.state()==DoorState::Fault);
  i.energized=false;door.tick(i,false,true);assert(door.state()==DoorState::Unknown);
  i.limit=true;assert(!door.tick(i).closedCycle);
  i.ticHealthy=false;o=door.tick(i,true);assert(door.state()==DoorState::Fault && o.motor==MotorAction::Release);

  // Fault and reopening precedence on the final closing segment.
  for(int test=0;test<4;++test) {
    DoorMachine d(c.motion,false);auto x=input();x.limit=true;d.tick(x);d.tick(x,true);x.now=250;d.tick(x);
    x.limit=false;x.motorPosition=x.targetPosition=x.encoderPosition=c.motion.travel;x.now+=5;d.tick(x);
    x.now+=c.motion.dwellMs;d.tick(x);x.now+=250;d.tick(x);x.targetPosition=0;x.motorPosition=x.encoderPosition=100;x.now+=5;
    if(test==0){x.now+=c.motion.closeTimeoutMs;assert(d.tick(x).motor==MotorAction::Release);assert(d.state()==DoorState::Fault);}
    else if(test==1){assert(d.tick(x,true).motor==MotorAction::Release);assert(d.state()==DoorState::Reopen);}
    else {x.motorPosition=x.encoderPosition=0;d.tick(x);assert(d.state()==DoorState::Homing);x.now+=5;
      if(test==2)x.beam=true;else x.now+=c.motion.homingTimeoutMs;
      assert(d.tick(x).motor==MotorAction::Release);assert(d.state()==DoorState::Fault);}
  }
  assert(encoderToSteps(600)==1600 && encoderToSteps(-600)==-1600 && encoderToSteps(90000)==240000);
  for(int direction:{-1,1}) {
    DoorMachine d(c.motion,false);auto x=input();x.limit=true;d.tick(x);d.tick(x,true);x.now=250;d.tick(x);
    x.limit=false;x.targetPosition=c.motion.travel;x.encoderPosition=1000;x.motorPosition=1000+direction*129;x.now+=5;
    auto forced=d.tick(x);assert(forced.forced&&d.state()==DoorState::Waiting&&forced.motor==MotorAction::Release);
  }
  std::cout<<"PASS door state machine, defaults, deadlines, faults, rollover timing\n";
}
static void configTests() {
  const char *why;auto c=defaultConfig(false);JsonDocument doc;configToJson(c,doc);AppConfig decoded;String error;
  assert(configFromJson(doc.as<JsonVariantConst>(),decoded,error));assert(sameMotion(c.motion,decoded.motion));
  assert(!configLoad(decoded,false));assert(configSave(c));assert(configLoad(decoded,false));
  assert(sameMotion(decoded.motion,c.motion));assert(!configLoad(decoded,true));assert(decoded.motion.dwellMs==5000);
  Preferences::failWrite=true;assert(!configSave(c));Preferences::failWrite=false;
  Preferences::storage["door-front"]["config"]="{invalid";assert(!configLoad(decoded,false));assert(decoded.motion.travel==18600);
  doc["version"]=2;assert(!configFromJson(doc.as<JsonVariantConst>(),decoded,error));doc["version"]=1;
  doc["motion"]["openSpeed"]=-1;assert(!configFromJson(doc.as<JsonVariantConst>(),decoded,error));configToJson(c,doc);
  doc["motion"]["closeCurrent"]=70000;assert(!configFromJson(doc.as<JsonVariantConst>(),decoded,error));configToJson(c,doc);
  doc["motion"]["travel"]=2.5;assert(!configFromJson(doc.as<JsonVariantConst>(),decoded,error));configToJson(c,doc);
  doc["qlab"]["passcode"]=String(100,'x');assert(!configFromJson(doc.as<JsonVariantConst>(),decoded,error));
  c.motion.openTimeoutMs=100;assert(!validateConfig(c,why));c=defaultConfig(false);
  c.motion.closeCurrent=3094;assert(!validateConfig(c,why));c=defaultConfig(false);
  c.network.dhcp=false;assert(validateConfig(c,why));strcpy(c.network.mask,"255.0.255.0");assert(!validateConfig(c,why));
  c=defaultConfig(false);c.network.dhcp=false;strcpy(c.network.ip,"192.168.1.255");assert(!validateConfig(c,why));
  uint32_t ip;for(auto text:{"1.2.3","256.1.1.1","1.2.3.4junk","1.2.3.-1",".1.2.3"})assert(!parseIPv4(text,ip));
  assert(parseIPv4("192.168.1.10",ip)&&ip==0xc0a8010a);
  c=defaultConfig(false);c.qlab.enabled=true;strcpy(c.qlab.host,"127.0.0.1");assert(!validateConfig(c,why));
  strcpy(c.qlab.workspace,"abc-123");assert(validateConfig(c,why));c.qlab.events[3].enabled=true;strcpy(c.qlab.events[3].cue,"../go");assert(!validateConfig(c,why));
  std::cout<<"PASS JSON round trip, persistence, corruption/version recovery, validation\n";
}
static void codecTests() {
  uint8_t osc[256],framed[512];size_t n=encodeOsc(osc,sizeof(osc),"/elev-door-front/door/open");assert(n);
  OscView v;assert(decodeOsc(osc,n,v)&&!v.argument);
  size_t size=encodeSlip(framed,sizeof(framed),osc,n);SlipDecoder decoder;unsigned count=0;
  // Every byte is a separate simulated TCP read; also decode back-to-back frames.
  for(int repeat=0;repeat<3;++repeat)for(size_t i=0;i<size;++i)if(auto ready=decoder.feed(framed[i])){++count;assert(decodeOsc(decoder.data(),ready,v));}
  assert(count==3);n=encodeOsc(osc,sizeof(osc),"/reply/test","escaped\xC0\xDB");size=encodeSlip(framed,sizeof(framed),osc,n);
  for(size_t i=0;i<size;++i)if(auto ready=decoder.feed(framed[i])){assert(decodeOsc(decoder.data(),ready,v));assert(!strcmp(v.argument,"escaped\xC0\xDB"));}
  decoder.reset();decoder.feed(0xC0);decoder.feed(0xDB);decoder.feed(1);assert(!decoder.feed(0xC0));assert(decoder.rejected()==1);
  for(size_t i=0;i<kOscCapacity+1;++i)decoder.feed(1);assert(!decoder.feed(0xC0));assert(decoder.rejected()==2);
  for(size_t i=0;i<size;++i)if(auto ready=decoder.feed(framed[i]))assert(decodeOsc(decoder.data(),ready,v));
  n=encodeOsc(osc,sizeof(osc),"/a");osc[3]=1;assert(!decodeOsc(osc,n,v));assert(!decodeOsc(osc,n-1,v));
  n=encodeOscInt(osc,sizeof(osc),"/alwaysReply",1);assert(n && osc[n-3]==0 && osc[n-1]==1);
  std::mt19937 rng(7);for(int j=0;j<30000;++j){uint8_t data[128];size_t length=rng()%sizeof(data);for(size_t k=0;k<length;++k){data[k]=rng();decoder.feed(data[k]);}decodeOsc(data,length,v);}
  std::cout<<"PASS OSC/SLIP fragmentation, coalescing, escaping, malformed/oversized input, fuzz\n";
}
static void triggerTests() {
  ClosedTrigger pending;pending.offer(0xfffffff0);uint32_t first=pending.generation();pending.offer(10);
  assert(pending.age(10)==26);assert(pending.eligible(20,100,true));pending.acknowledge(first+1);assert(pending.pending());
  pending.acknowledge(first);assert(!pending.pending());pending.offer(100);assert(pending.generation()!=first);
  assert(!pending.eligible(200,100,true));pending.offer(300);assert(!pending.eligible(301,100,false));
  QLabConfig q{};strcpy(q.workspace,"abc");QLabSession s;s.connected(q,0);
  s.reply("/workspace/wrong/connect","wrong","ok","ok",false,1);assert(s.stage()==QLabStage::Authenticate);
  s.reply(s.method(),"abc","ok","badpass",false,1);assert(s.stage()==QLabStage::Failed);
  s.connected(q,0);s.reply(s.method(),"abc","ok","ok",false,1);assert(s.stage()==QLabStage::EnableReplies);
  s.reply("/alwaysReply","","ok",nullptr,false,2);assert(!s.ready());s.reply("/alwaysReply","","ok",nullptr,true,3);assert(s.ready());
  assert(s.startCue("closed",42,4));assert(!s.startCue("other",43,5));assert(s.reply(s.method(),"abc","ok",nullptr,false,6)==42);
  assert(s.ready());assert(s.startCue("closed",43,7));assert(s.expired(2007));s.disconnected();assert(!s.ready());
  std::cout<<"PASS single closed slot, coalescing, expiry, stale acknowledgments, QLab authentication/replies\n";
}
static void httpParserTests() {
  HttpRequest request;
  auto feed=[&](const String &text){request.reset();HttpResult result=HttpResult::More;for(unsigned char c:text){result=request.feed(c);if(result!=HttpResult::More)break;}return result;};
  assert(feed("GET /api/state HTTP/1.1\r\nHost: localhost\r\n\r\n")==HttpResult::Ready);
  assert(!strcmp(request.path(),"/api/state"));
  assert(feed("PUT /api/config HTTP/1.1\r\nContent-Length: 2\r\nContent-Type: application/json\r\n\r\n{}")==HttpResult::Ready);
  assert(request.bodySize()==2&&!strcmp(request.body(),"{}"));
  assert(feed("PUT /api/config HTTP/1.1\r\nContent-Length: 9999999999999999\r\n\r\n")==HttpResult::TooLarge);
  assert(feed("PUT /api/config HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n")==HttpResult::BadRequest);
  assert(feed("PUT /api/config HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n")==HttpResult::BadRequest);
  assert(feed("GET / HTTP/1.1\r\nX: "+String(2100,'a'))==HttpResult::TooLarge);
  assert(feed("PUT /api/config HTTP/1.1\r\nContent-Length: 3\r\nContent-Type: application/json\r\n\r\n{}")==HttpResult::More);
  std::cout<<"PASS bounded HTTP parsing, fragmented body, oversized headers/body, duplicate length and chunked rejection\n";
}
static std::vector<uint8_t> frame(const char *method,const char *arg=nullptr) {
  uint8_t message[1024],wire[2048];size_t n=encodeOsc(message,sizeof(message),method,arg);size_t w=encodeSlip(wire,sizeof(wire),message,n);
  return {wire,wire+w};
}
static void sendAll(int fd,const std::vector<uint8_t> &data) {assert(send(fd,data.data(),data.size(),0)==static_cast<ssize_t>(data.size()));}
static int connectLocal(uint16_t port) {int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);assert(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);return fd;}
static void tcpIntegration() {
  int server=socket(AF_INET,SOCK_STREAM,0);assert(server>=0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  assert(bind(server,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0 && listen(server,4)==0);socklen_t len=sizeof(a);assert(getsockname(server,reinterpret_cast<sockaddr*>(&a),&len)==0);
  fcntl(server,F_SETFL,O_NONBLOCK);QLabConfig cfg{};cfg.enabled=true;strcpy(cfg.host,"127.0.0.1");strcpy(cfg.workspace,"workspace-123");strcpy(cfg.passcode,"secret");cfg.port=ntohs(a.sin_port);cfg.closedExpiryMs=30000;
  cfg.events[3].enabled=true;strcpy(cfg.events[3].cue,"closed");cfg.events[9].enabled=true;strcpy(cfg.events[9].cue,"waiting");
  testMillis=0;oscBegin();oscConfigure(cfg);int peer=-1;SlipDecoder decoder;unsigned closedStarts=0,liveStarts=0,auth=0;bool suppressAck=false,badpass=false;
  auto pump=[&] {
    testMillis+=5;oscLoop();
    if(peer<0){peer=accept(server,nullptr,nullptr);if(peer>=0){fcntl(peer,F_SETFL,O_NONBLOCK);decoder.reset();}}
    if(peer>=0){uint8_t buf[2048];int n=recv(peer,buf,sizeof(buf),0);if(n==0){close(peer);peer=-1;}
      for(int j=0;j<n;++j)if(size_t length=decoder.feed(buf[j])){
        // /alwaysReply 1 is an int message; the following read query verifies it.
        OscView v;if(!decodeOsc(decoder.data(),length,v))continue;
        String method=v.address;JsonDocument reply;reply["address"]=method;reply["status"]="ok";
        if(method.find("/connect")!=String::npos){++auth;assert(v.argument&&!strcmp(v.argument,"secret"));reply["workspace_id"]="workspace-123";reply["data"]=badpass?"badpass":"ok";}
        else if(method=="/alwaysReply")reply["data"]=1;
        else if(method.find("/cue/")!=String::npos){reply["workspace_id"]="workspace-123";if(method.find("/closed/")!=String::npos){++closedStarts;if(suppressAck)continue;}else ++liveStarts;}
        String json;serializeJson(reply,json);auto bytes=frame(("/reply"+method).c_str(),json.c_str());
        // Deliberately fragment replies over successive sends.
        assert(send(peer,bytes.data(),3,0)==3);assert(send(peer,bytes.data()+3,bytes.size()-3,0)==static_cast<ssize_t>(bytes.size()-3));
      }
    }
    usleep(1000);
  };
  auto until=[&](auto condition,int limit=2000){for(int i=0;i<limit&&!condition();++i)pump();assert(condition());};
  until([]{return oscStatus().ready;});assert(auth==1);
  int command=connectLocal(53000);auto open=frame("/elev-door-front/door/open");
  assert(send(command,open.data(),4,0)==4);pump();assert(opens==0);assert(send(command,open.data()+4,open.size()-4,0)==static_cast<ssize_t>(open.size()-4));
  until([]{return opens==1;});sendAll(command,frame("/elev-door-rear/door/open"));sendAll(command,frame("/elev-door-front/door/open","invalid"));for(int i=0;i<20;++i)pump();assert(opens==1);
  fakeDoor.pendingClosed=true;fakeDoor.pendingGeneration=1;
  until([]{return acknowledged==1;});for(int i=0;i<30;++i)pump();assert(closedStarts==1); // motor task has not cleared its snapshot yet
  fakeDoor.pendingClosed=false;live.push_back({9,testMillis});until([&]{return liveStarts==1;});
  suppressAck=true;fakeDoor.pendingClosed=true;fakeDoor.pendingGeneration=2;until([&]{return closedStarts>=3;});assert(acknowledged==1);
  suppressAck=false;until([]{return acknowledged==2;});fakeDoor.pendingClosed=false;
  ethOnline=false;pump();assert(!oscStatus().ready);fakeDoor.pendingClosed=true;fakeDoor.pendingGeneration=3;live.push_back({9,testMillis});pump();unsigned before=liveStarts;
  ethOnline=true;until([]{return acknowledged==3;});assert(liveStarts==before);fakeDoor.pendingClosed=false;
  // Cancellation on reopening must abandon an unacknowledged transaction.
  suppressAck=true;fakeDoor.pendingClosed=true;fakeDoor.pendingGeneration=4;unsigned previous=closedStarts;
  until([&]{return closedStarts>previous;});fakeDoor.pendingClosed=false;for(int i=0;i<100;++i)pump();assert(closedStarts==previous+1);
  badpass=true;oscConfigure(cfg);for(int i=0;i<200;++i)pump();assert(!oscStatus().ready);assert(std::string(oscStatus().error).find("badpass")!=std::string::npos);
  ethOnline=false;pump();close(command);if(peer>=0)close(peer);close(server);
  std::cout<<"PASS real localhost TCP command server and QLab mock: SLIP, handshake, ACK, retry, outage, cancellation, badpass\n";
}
static void webIntegration() {
  AppConfig config=defaultConfig(false);webBegin(config);ethOnline=true;webLoop();
  auto transact=[&](const String &method,const String &path,const String &content=String(),const String &extra=String()) {
    int fd=connectLocal(80);fcntl(fd,F_SETFL,O_NONBLOCK);
    String wire=method+" "+path+" HTTP/1.1\r\nHost: localhost\r\n"+extra;
    if(method=="PUT")wire+="Content-Type: application/json\r\nContent-Length: "+std::to_string(content.size())+"\r\n";
    wire+="\r\n"+content;assert(send(fd,wire.data(),wire.size(),0)==static_cast<ssize_t>(wire.size()));
    String response;bool done=false;
    for(int i=0;i<1500&&!done;++i){testMillis+=5;webLoop();char buf[2048];int n=recv(fd,buf,sizeof(buf),0);if(n>0)response.append(buf,n);else if(n==0)done=true;usleep(500);}
    assert(done);close(fd);return response;
  };
  auto response=transact("GET","/");assert(response.find("200 OK")!=String::npos&&response.find("Motion tuning")!=String::npos);
  response=transact("GET","/api/config");assert(response.find("200 OK")!=String::npos);
  JsonDocument doc;assert(!deserializeJson(doc,response.substr(response.find("\r\n\r\n")+4)));
  response=transact("GET","/api/state");assert(response.find("elev-door-front")!=String::npos);
  assert(transact("POST","/api/fault/reset").find("202 Accepted")!=String::npos&&resetRequests==1);
  String json;doc["motion"]["travel"]=19000;serializeJson(doc,json);fakeDoor.state=DoorState::Opening;
  assert(transact("PUT","/api/config",json).find("409 Conflict")!=String::npos&&config.motion.travel==18600);
  fakeDoor.state=DoorState::Closed;Preferences::failWrite=true;
  assert(transact("PUT","/api/config",json).find("409 Conflict")!=String::npos&&!reserved&&config.motion.travel==18600);Preferences::failWrite=false;
  commitAllowed=false;assert(transact("PUT","/api/config",json).find("409 Conflict")!=String::npos&&config.motion.travel==18600);commitAllowed=true;
  assert(transact("PUT","/api/config",json).find("200 OK")!=String::npos&&config.motion.travel==19000);
  assert(transact("PUT","/api/config","{bad").find("400 Bad Request")!=String::npos);
  assert(transact("PUT","/api/config",String(8193,'x')).find("413 Content Too Large")!=String::npos);
  auto next=config;next.network.dhcp=false;String error;assert(saveConfiguration(next,error));webLoop();assert(networkChanges==1);
  next.network.dhcp=true;assert(saveConfiguration(next,error));webLoop();assert(config.network.dhcp&&config.motion.travel==19000&&networkChanges==2);
  ethOnline=false;webLoop();
  std::cout<<"PASS real localhost HTTP page/API, settings guards, persistence rollback, invalid/oversized bodies, DHCP recovery\n";
}
int main(){stateTests();configTests();codecTests();triggerTests();httpParserTests();tcpIntegration();webIntegration();std::cout<<"All native tests passed.\n";}
