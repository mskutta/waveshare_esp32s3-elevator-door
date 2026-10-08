#include "QLabSession.h"
#include <string.h>
#include <stdio.h>
namespace {
bool authenticatedForControl(const char *text) {
  if(!text) return false;
  if(!strcmp(text,"ok")) return true; // Older QLab reply format.
  if(strncmp(text,"ok:",3)) return false;
  bool control=false;
  // QLab 5 can append the permissions granted to this connection.
  const char *permission=text+3;
  while(*permission) {
    const char *separator=strchr(permission,'|');
    size_t length=separator?static_cast<size_t>(separator-permission):strlen(permission);
    if(length==7 && !strncmp(permission,"control",7)) control=true;
    else if(!((length==4 && !strncmp(permission,"view",4)) ||
              (length==4 && !strncmp(permission,"edit",4)))) return false;
    if(!separator) return control;
    permission=separator+1;
  }
  return false;
}
}
void QLabSession::connected(const QLabConfig &c,uint32_t now) {
  strcpy(workspace_,c.workspace);
  if(workspace_[0]) snprintf(method_,sizeof(method_),"/workspace/%s/connect",workspace_);
  else strcpy(method_,"/connect");
  stage_=QLabStage::Authenticate;sent_=now;generation_=0;
}
bool QLabSession::startCue(const char *cue,uint32_t generation,uint32_t now) {
  if(!ready()) return false;
  if(workspace_[0]) snprintf(method_,sizeof(method_),"/workspace/%s/cue/%s/start",workspace_,cue);
  else snprintf(method_,sizeof(method_),"/cue/%s/start",cue);
  generation_=generation;sent_=now;stage_=QLabStage::Cue;return true;
}
bool QLabSession::matchesReply(const char *invokedMethod,const char *replyMethod,const char *workspace) const {
  if(!awaiting() || strcmp(invokedMethod,method_) ||
     (workspace_[0] && workspace && *workspace && strcmp(workspace,workspace_))) return false;
  if(!strcmp(replyMethod,method_)) return true;
  // QLab 5.6.3 replies to /connect with /reply/connect in the OSC envelope,
  // but /workspace/<reply workspace_id>/connect in the JSON payload.
  // Cue replies can use the same expansion. Never relax the command suffix
  // or permit a prefix that disagrees with the reply's workspace_id.
  if(workspace_[0] || (stage_!=QLabStage::Authenticate && stage_!=QLabStage::Cue) ||
     !workspace || !*workspace || strncmp(replyMethod,"/workspace/",11)) return false;
  const char *id=replyMethod+11;
  const char *suffix=strchr(id,'/');
  return suffix && static_cast<size_t>(suffix-id)==strlen(workspace) &&
    !strncmp(id,workspace,suffix-id) && !strcmp(suffix,method_);
}
uint32_t QLabSession::reply(const char *address,const char *workspace,const char *status,const char *dataText,bool enabled,uint32_t now) {
  if(!matchesReply(method_,address,workspace)) return 0;
  if(strcmp(status,"ok") || (stage_==QLabStage::Authenticate && !authenticatedForControl(dataText))) {stage_=QLabStage::Failed;return 0;}
  if(stage_==QLabStage::Authenticate) {
    stage_=QLabStage::EnableReplies;strcpy(method_,"/alwaysReply");sent_=now;
  } else if(stage_==QLabStage::EnableReplies) {
    if(enabled) {stage_=QLabStage::Ready;method_[0]=0;}
  } else {
    uint32_t acknowledged=generation_;generation_=0;stage_=QLabStage::Ready;method_[0]=0;return acknowledged;
  }
  return 0;
}
