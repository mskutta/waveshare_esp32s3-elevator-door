#pragma once
#include "Types.h"
enum class QLabStage { Offline, Authenticate, EnableReplies, Ready, Cue, Failed };
class QLabSession {
public:
  void connected(const QLabConfig &c, uint32_t now);
  void disconnected() {stage_=QLabStage::Offline;generation_=0;method_[0]=0;}
  bool ready() const {return stage_==QLabStage::Ready;}
  bool operational() const {return ready() || stage_==QLabStage::Cue;}
  bool awaiting() const {return stage_==QLabStage::Authenticate || stage_==QLabStage::EnableReplies || stage_==QLabStage::Cue;}
  bool expired(uint32_t now) const {return awaiting() && elapsed(now,sent_)>=2000;}
  QLabStage stage() const {return stage_;}
  const char *method() const {return method_;}
  bool startCue(const char *cue,uint32_t generation,uint32_t now);
  // The envelope identifies the sent method; QLab may expand the JSON method.
  bool matchesReply(const char *invokedMethod,const char *replyMethod,const char *workspace) const;
  // Returns the closed-slot generation acknowledged, or zero for other replies.
  uint32_t reply(const char *address,const char *workspace,const char *status,const char *dataText,bool repliesEnabled,uint32_t now);
private:
  QLabStage stage_=QLabStage::Offline;
  char workspace_[40]{},method_[128]{};
  uint32_t sent_=0,generation_=0;
};
