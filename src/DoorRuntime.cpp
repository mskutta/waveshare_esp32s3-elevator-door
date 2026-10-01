#include "DoorRuntime.h"
#include "Pins.h"
#include "Encoder.h"
#include <DoorMachine.h>
#include <ClosedTrigger.h>
#include <Wire.h>
#include <Tic.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <limits.h>

namespace {
enum class RequestType { Reserve, Commit, Release };
struct Request { RequestType type; uint32_t id; bool changingMotion; MotionConfig motion; uint32_t expiry; };
struct Reply {uint32_t id; bool ok;};
QueueHandle_t requests=nullptr,replies=nullptr,events=nullptr;
TaskHandle_t task=nullptr;
portMUX_TYPE sharedMux=portMUX_INITIALIZER_UNLOCKED;
DoorStatus published{};
bool networkReady=false,qlabReady=false;
uint32_t networkIP=0;
uint32_t flags=0,ackGeneration=0;
bool ack=false;
MotionConfig initial;
uint32_t initialExpiry=30000;
uint32_t requestId=0;

bool exchange(Request &r) {
  r.id=++requestId;
  if(xQueueSend(requests,&r,pdMS_TO_TICKS(100))!=pdTRUE) return false;
  uint32_t started=millis(); Reply result;
  while(elapsed(millis(),started)<2000) {
    if(xQueueReceive(replies,&result,pdMS_TO_TICKS(100))==pdTRUE && result.id==r.id) return result.ok;
  }
  return false;
}
void control(void *) {
  pinMode(Pins::limit,INPUT_PULLUP);pinMode(Pins::beam,INPUT_PULLUP);
  pinMode(Pins::upButton,INPUT_PULLUP);pinMode(Pins::downButton,INPUT_PULLUP);
  digitalWrite(Pins::outputUp,kRear?HIGH:LOW);digitalWrite(Pins::outputDown,kRear?HIGH:LOW);
  pinMode(Pins::outputUp,OUTPUT);pinMode(Pins::outputDown,OUTPUT);
  Wire.begin(Pins::sda,Pins::scl,100000); Wire.setTimeOut(10);
  TicI2C tic; tic.setProduct(TicProduct::T500);
  tic.deenergize(); bool initialHealthy=tic.getLastError()==0;
  bool encoderOK=encoderBegin();
  SSD1306AsciiWire oled;
  Wire.beginTransmission(0x3C); bool displayOK=Wire.endTransmission()==0;
  if(displayOK) {oled.begin(&Adafruit128x64,0x3C);oled.setFont(System5x7);oled.clear();}
  MotionConfig motion=initial;
  DoorMachine machine(motion,kRear);
  ClosedTrigger closed;
  uint32_t expiry=initialExpiry,reservedAt=0,displayAt=0,watchdogAt=0;
  uint32_t losses=0,cycles=0; bool reserved=false,up=false,down=false;
  uint8_t displayRow=0;bool reservedChanging=false;
  if(!initialHealthy) machine.fault("Tic communication");
  if(!encoderOK) machine.fault("Encoder initialization");

  auto motor = [&](MotorAction action, int32_t position) {
    bool ok=true;
#define TIC(call) do { if(ok) {tic.call;ok=tic.getLastError()==0;} } while(0)
    if(action==MotorAction::None) return true;
    if(action==MotorAction::Release) {TIC(deenergize());return ok;}
#ifdef MOTOR_INHIBITED
    (void)position;
    return false;
#else
    bool opening=action==MotorAction::Open || action==MotorAction::Reopen;
    bool home=action==MotorAction::Home;
    TIC(setStepMode(TicStepMode::Microstep8));
    TIC(setCurrentLimit(home?motion.homingCurrent:opening?motion.openCurrent:motion.closeCurrent));
    TIC(setMaxAccel(home?motion.homingAccel:opening?motion.openAccel:motion.closeAccel));
    TIC(setMaxDecel(home?motion.homingDecel:opening?motion.openDecel:motion.closeDecel));
    TIC(haltAndSetPosition(home?0:position));
    if(home) { TIC(setTargetVelocity(-static_cast<int32_t>(motion.homingSpeed))); }
    else {
      TIC(setMaxSpeed(opening?(action==MotorAction::Reopen?motion.reopenSpeed:motion.openSpeed):motion.closeSpeed));
      TIC(setTargetPosition(opening?motion.travel:0));
    }
    TIC(energize());TIC(exitSafeStart()); return ok;
#endif
#undef TIC
  };

  while(true) {
    uint32_t now=millis();
    int64_t counts=encoderOK?encoderCount():0;
    int64_t scaled=encoderToSteps(counts);
    bool scaleOK=scaled>=INT32_MIN && scaled<=INT32_MAX;
    DoorInput input{now,digitalRead(Pins::limit)==LOW,digitalRead(Pins::beam)==LOW,true,false,0,0,
      scaleOK?static_cast<int32_t>(scaled):0};
    input.motorPosition=tic.getCurrentPosition(); input.ticHealthy=tic.getLastError()==0;
    if(input.ticHealthy) {input.targetPosition=tic.getTargetPosition();input.ticHealthy=tic.getLastError()==0;}
    if(input.ticHealthy) {input.energized=tic.getEnergized();input.ticHealthy=tic.getLastError()==0;}
    if(input.ticHealthy && machine.state()!=DoorState::Fault && elapsed(now,watchdogAt)>=100) {
      tic.resetCommandTimeout();input.ticHealthy=tic.getLastError()==0;watchdogAt=now;
    }
    uint32_t commands;bool gotAck, liveReady;uint32_t generation;
    portENTER_CRITICAL(&sharedMux);
    commands=flags;flags=0;gotAck=ack;ack=false;generation=ackGeneration;liveReady=qlabReady;
    portEXIT_CRITICAL(&sharedMux);
    if(gotAck) closed.acknowledge(generation);
    bool commandsAllowed=!reserved;
    if(reserved && elapsed(now,reservedAt)>=10000) reserved=false;
    Request r;
    if(xQueueReceive(requests,&r,0)==pdTRUE) {
      bool ok=true;
      if(r.type==RequestType::Reserve) {
        ok=!reserved && machine.canMaintain(input) && (!r.changingMotion || machine.canTune(input));
        if(ok) {reserved=true;reservedAt=now;reservedChanging=r.changingMotion;}
      } else if(r.type==RequestType::Commit) {
        ok=reserved && machine.canMaintain(input) && (!reservedChanging || machine.canTune(input));
        if(ok) {motion=r.motion;machine.tune(motion);expiry=r.expiry;closed.cancel();reserved=false;}
      } else reserved=false;
      Reply reply{r.id,ok};xQueueSend(replies,&reply,0);
    }
    DoorOutput output;
    if(!encoderOK || !scaleOK) output=machine.fault(!encoderOK?"Encoder initialization":"Encoder range");
    else output=machine.tick(input,commandsAllowed && !reserved && (commands&1),commandsAllowed && !reserved && (commands&2));
    if(!motor(output.motor,input.encoderPosition)) output=machine.fault(
#ifdef MOTOR_INHIBITED
      "Bench: motor inhibited"
#else
      "Tic communication"
#endif
    );
    if(machine.state()==DoorState::Fault) {
      // Never feed the Tic command watchdog in a fault. Even when release
      // cannot be transmitted, its configured hardware timeout can stop it.
      tic.deenergize(); closed.cancel();
    }
    if(output.zeroEncoder && encoderOK) {encoderZero();counts=0;input.encoderPosition=0;}
    if(output.closedCycle) {++cycles;closed.offer(now);}
    closed.eligible(now,expiry,machine.state()==DoorState::Closed && machine.homed() && input.limit);
    if(output.changed) {
      if(!kRear) {
        if(machine.state()==DoorState::Closed) {up=true;down=false;}
        else if(machine.state()==DoorState::Open || machine.state()==DoorState::Fault) {up=down=false;}
      } else {
        if(machine.state()==DoorState::Open) up=down=true;
        if(machine.state()==DoorState::Waiting || machine.state()==DoorState::Fault) up=down=false;
      }
      DoorEvent e{static_cast<uint8_t>(machine.state()),now};
      // Closed uses its dedicated slot, never this lossy live-event queue.
      if(machine.state()!=DoorState::Closed && (!liveReady || xQueueSend(events,&e,0)!=pdTRUE)) ++losses;
    }
    if(output.forced) {DoorEvent e{static_cast<uint8_t>(kForcedEvent),now};if(!liveReady || xQueueSend(events,&e,0)!=pdTRUE)++losses;}
    digitalWrite(Pins::outputUp,kRear?!up:up);digitalWrite(Pins::outputDown,kRear?!down:down);
    DoorStatus s{};s.state=machine.state();s.homed=machine.homed();s.limit=input.limit;s.beam=input.beam;
    s.energized=input.energized;s.healthy=input.ticHealthy;s.maintenance=reserved;s.upOutput=up;s.downOutput=down;
    s.encoderPosition=input.encoderPosition;s.encoderCounts=counts;s.motorPosition=input.motorPosition;s.targetPosition=input.targetPosition;
    s.pendingClosed=closed.pending();s.pendingAgeMs=closed.age(now);s.pendingGeneration=closed.generation();s.cycles=cycles;s.lostLiveEvents=losses;
    strlcpy(s.fault,machine.faultReason(),sizeof(s.fault));
    portENTER_CRITICAL(&sharedMux);published=s; bool net=networkReady,q=qlabReady;uint32_t ip=networkIP;portEXIT_CRITICAL(&sharedMux);
    // Refresh one small text row at a time, keeping display traffic bounded.
    if(displayOK && elapsed(now,displayAt)>=200) {
      displayAt=now;char line[24];
      switch(displayRow) {
        case 0:snprintf(line,sizeof(line),"%s",kDoorName);break;
        case 1:snprintf(line,sizeof(line),"%s",eventName(static_cast<size_t>(s.state)));break;
        case 2:snprintf(line,sizeof(line),"pos:%ld",static_cast<long>(s.encoderPosition));break;
        case 3:snprintf(line,sizeof(line),"limit:%u beam:%u",s.limit,s.beam);break;
        case 4:snprintf(line,sizeof(line),"ETH:%u QLab:%u",net,q);break;
        case 5:snprintf(line,sizeof(line),"%s",IPAddress(ip).toString().c_str());break;
        case 6:snprintf(line,sizeof(line),"closed pending:%u",s.pendingClosed);break;
        default:snprintf(line,sizeof(line),"%.21s",s.fault);break;
      }
      oled.setCursor(0,displayRow);oled.print(line);oled.clearToEOL();displayRow=(displayRow+1)%8;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}
bool doorBegin(const MotionConfig &m,uint32_t expiry) {
  initial=m;initialExpiry=expiry;requests=xQueueCreate(4,sizeof(Request));replies=xQueueCreate(4,sizeof(Reply));events=xQueueCreate(16,sizeof(DoorEvent));
  if(!requests || !replies || !events) return false;
  return xTaskCreatePinnedToCore(control,"door",6144,nullptr,3,&task,1)==pdPASS;
}
DoorStatus doorStatus() {
  portENTER_CRITICAL(&sharedMux);auto s=published;
  if(flags&1) s.pendingClosed=false; // suppress replay as soon as reopening is requested
  portEXIT_CRITICAL(&sharedMux);return s;
}
void doorOpenRequest() {portENTER_CRITICAL(&sharedMux);flags|=1;portEXIT_CRITICAL(&sharedMux);}
void doorResetFault() {portENTER_CRITICAL(&sharedMux);flags|=2;portEXIT_CRITICAL(&sharedMux);}
bool doorEvent(DoorEvent &e) {return xQueueReceive(events,&e,0)==pdTRUE;}
void doorAcknowledgeClosed(uint32_t g) {portENTER_CRITICAL(&sharedMux);ack=true;ackGeneration=g;portEXIT_CRITICAL(&sharedMux);}
bool doorReserve(bool motion) {Request r{};r.type=RequestType::Reserve;r.changingMotion=motion;return exchange(r);}
bool doorCommit(const MotionConfig &m,uint32_t expiry) {Request r{};r.type=RequestType::Commit;r.motion=m;r.expiry=expiry;return exchange(r);}
void doorReleaseReservation() {Request r{};r.type=RequestType::Release;exchange(r);}
void doorNetworkStatus(bool net,bool q,IPAddress ip) {portENTER_CRITICAL(&sharedMux);networkReady=net;qlabReady=q;networkIP=static_cast<uint32_t>(ip);portEXIT_CRITICAL(&sharedMux);}
