#include "QLabSession.h"
#include <string.h>
#include <stdio.h>
void QLabSession::connected(const QLabConfig &c,uint32_t now) {
  strcpy(workspace_,c.workspace);snprintf(method_,sizeof(method_),"/workspace/%s/connect",workspace_);
  stage_=QLabStage::Authenticate;sent_=now;generation_=0;
}
bool QLabSession::startCue(const char *cue,uint32_t generation,uint32_t now) {
  if(!ready()) return false;
  snprintf(method_,sizeof(method_),"/workspace/%s/cue/%s/start",workspace_,cue);
  generation_=generation;sent_=now;stage_=QLabStage::Cue;return true;
}
uint32_t QLabSession::reply(const char *address,const char *workspace,const char *status,const char *dataText,bool enabled,uint32_t now) {
  if(!awaiting() || strcmp(address,method_) || (workspace && *workspace && strcmp(workspace,workspace_))) return 0;
  if(strcmp(status,"ok") || (stage_==QLabStage::Authenticate && (!dataText || strcmp(dataText,"ok")))) {stage_=QLabStage::Failed;return 0;}
  if(stage_==QLabStage::Authenticate) {
    stage_=QLabStage::EnableReplies;strcpy(method_,"/alwaysReply");sent_=now;
  } else if(stage_==QLabStage::EnableReplies) {
    if(enabled) {stage_=QLabStage::Ready;method_[0]=0;}
  } else {
    uint32_t acknowledged=generation_;generation_=0;stage_=QLabStage::Ready;method_[0]=0;return acknowledged;
  }
  return 0;
}
