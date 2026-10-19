// SPDX-License-Identifier: MIT
// Included inside Source and its controlled host. None of these admissions are
// decoded from a public command: Source calls them at genuine prepared input,
// capture submission/receipt and native codec/comparison boundaries.
struct ScreenshotAssertionCaptureProof {
  suitecut::ScreenshotAssertionRetirement::Identity identity;
  SnapshotReferenceDocument document;
  uint64_t sample=0,artifact_generation=0;
  int command=0;bool accepted=false,received=false,valid=false,delivered=false,used=false;
};
struct ScreenshotAssertionPixelWork {
  suitecut::ScreenshotAssertionRetirement::Identity identity;
  suitecut::ScreenshotAssertionRetirement::Ticket physical;
  uint64_t sample=0;bool validation=false,submitted=false;
};
std::map<std::pair<int,uint64_t>,ScreenshotAssertionCaptureProof>screenshot_assertion_captures_;
std::map<uint64_t,ScreenshotAssertionPixelWork>screenshot_assertion_pixels_;
bool ScreenshotAssertionPrepared(suitecut::ScreenshotAssertionRetirement::Identity identity){
  const auto found=screenshot_assertion_scopes_.find(identity.owner);
  const auto entry=screenshot_assertion_retirement_.Get(identity);
  if(!entry||entry->frozen||(entry->original_deadline&&Now()>=entry->original_deadline)||found==screenshot_assertion_scopes_.end()||found->second.identity!=identity||found->second.prepared||!ScreenshotAssertionPinsCurrent(identity))return false;
  found->second.prepared=true;return true;
}
bool ScreenshotAssertionCaptureEnter(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t sample,uint64_t artifact_generation,int command,const std::string&frame,const std::string&session){
  const auto entry=screenshot_assertion_retirement_.Get(identity);
  const auto document=ReferenceDocument(frame);const auto scope=screenshot_assertion_scopes_.find(identity.owner);
  if(!entry||entry->frozen||!sample||!artifact_generation||command<1||scope==screenshot_assertion_scopes_.end()||scope->second.identity!=identity||!scope->second.prepared||!document||document->session!=session||
     (entry->original_deadline&&Now()>=entry->original_deadline)||!ScreenshotAssertionRememberFrame(identity,frame)||screenshot_assertion_captures_.size()>=32)return false;
  try{return screenshot_assertion_captures_.emplace(std::make_pair(identity.owner,sample),ScreenshotAssertionCaptureProof{identity,*document,sample,artifact_generation,command}).second;}catch(const std::bad_alloc&){return false;}
}
bool ScreenshotAssertionCaptureReceipt(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t sample,int command,const std::string&session,bool received,bool valid){
  const auto found=screenshot_assertion_captures_.find({identity.owner,sample});
  if(found==screenshot_assertion_captures_.end())return false;
  auto&proof=found->second;
  if(proof.identity!=identity||proof.command!=command||proof.document.session!=session||proof.received)return false;
  proof.accepted=true;
  if(received){proof.received=true;proof.valid=valid&&ScreenshotAssertionPinsCurrent(identity);}
  screenshot_assertion_retirement_.CaptureSubmitted(identity);return true;
}
void ScreenshotAssertionCaptureRejected(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t sample,int command){
  const auto found=screenshot_assertion_captures_.find({identity.owner,sample});
  if(found!=screenshot_assertion_captures_.end()&&found->second.identity==identity&&found->second.command==command&&!found->second.received)screenshot_assertion_captures_.erase(found);
}
bool ScreenshotAssertionCapturePending(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t sample)const{
  const auto found=screenshot_assertion_captures_.find({identity.owner,sample});
  return found!=screenshot_assertion_captures_.end()&&found->second.identity==identity&&found->second.accepted&&!found->second.delivered;
}
bool ScreenshotAssertionCaptureDeliverable(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t sample,uint64_t artifact_generation)const{
  const auto found=screenshot_assertion_captures_.find({identity.owner,sample});
  return found!=screenshot_assertion_captures_.end()&&found->second.identity==identity&&found->second.artifact_generation==artifact_generation&&found->second.received&&!found->second.delivered;
}
void ScreenshotAssertionCaptureDelivered(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t sample){
  const auto found=screenshot_assertion_captures_.find({identity.owner,sample});
  if(found!=screenshot_assertion_captures_.end()&&found->second.identity==identity)found->second.delivered=true;
}
bool ScreenshotAssertionPixelsBegin(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t sample,uint64_t job,bool validation){
  using Policy=suitecut::ScreenshotAssertionRetirement;
  const auto entry=screenshot_assertion_retirement_.Get(identity);
  if(!entry||!job||screenshot_assertion_pixels_.contains(job)||screenshot_assertion_pixels_.size()>=2||!ScreenshotAssertionPinsCurrent(identity)||!screenshot_assertion_scopes_.at(identity.owner).prepared)return false;
  for(const auto&[held,work]:screenshot_assertion_pixels_){(void)held;if(work.identity==identity&&work.sample==sample)return false;}
  const auto proof=screenshot_assertion_captures_.find({identity.owner,sample});
  if(sample){
    if(proof==screenshot_assertion_captures_.end()||proof->second.identity!=identity||!proof->second.accepted||!proof->second.received||!proof->second.valid||proof->second.used)return false;
  }else if(validation||screenshot_assertion_scopes_.at(identity.owner).baseline_valid)return false;
  if(validation&&(!entry->frozen||!entry->original_deadline||Now()<entry->original_deadline||entry->canceled||!entry->error.empty()||!sample))return false;
  auto ticket=screenshot_assertion_retirement_.Begin(identity,validation?Policy::Work::Validation:Policy::Work::Pixels,0,"",Now());
  if(!ticket)return false;
  try{
    const auto [position,inserted]=screenshot_assertion_pixels_.emplace(job,ScreenshotAssertionPixelWork{identity,*ticket,sample,validation});(void)position;
    if(!inserted){screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);return false;}
    if(sample)proof->second.used=true;
    return true;
  }catch(const std::bad_alloc&){screenshot_assertion_retirement_.WithdrawUnsubmitted(*ticket);return false;}
}
bool ScreenshotAssertionPixelsSubmitted(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t job){
  const auto found=screenshot_assertion_pixels_.find(job);
  if(found==screenshot_assertion_pixels_.end()||found->second.identity!=identity||found->second.submitted||!screenshot_assertion_retirement_.EnterSubmission(found->second.physical))return false;
  found->second.submitted=true;return true;
}
bool ScreenshotAssertionPixelsObserved(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t job){
  const auto found=screenshot_assertion_pixels_.find(job);
  return found!=screenshot_assertion_pixels_.end()&&found->second.identity==identity&&found->second.submitted&&found->second.sample&&!found->second.validation&&ScreenshotAssertionPinsCurrent(identity)&&screenshot_assertion_retirement_.Observe(identity,Now());
}
void ScreenshotAssertionPixelsSettled(suitecut::ScreenshotAssertionRetirement::Identity identity,uint64_t job,bool submitted,bool valid,const std::string&error){
  const auto found=screenshot_assertion_pixels_.find(job);
  if(found==screenshot_assertion_pixels_.end()||found->second.identity!=identity)return;
  const auto work=found->second;
  const bool pins=ScreenshotAssertionPinsCurrent(identity);
  const auto failure=!error.empty()?error:!submitted?"Native screenshot codec submission rejected":!pins?"Native screenshot document changed during actual codec work":!valid?"Actual native screenshot codec validation failed":"";
  if(!failure.empty())screenshot_assertion_retirement_.Fail(identity,failure,Now());
  if(failure.empty()&&valid&&pins&&submitted){
    auto&scope=screenshot_assertion_scopes_.at(identity.owner);
    if(work.sample)scope.capture_valid=true;else scope.baseline_valid=true;
    if(scope.baseline_valid&&scope.capture_valid)screenshot_assertion_retirement_.Validated(identity);
  }
  if(submitted)screenshot_assertion_retirement_.Acknowledge(work.physical,failure.empty()&&valid&&pins,failure,Now());
  else if(!screenshot_assertion_retirement_.WithdrawUnsubmitted(work.physical))screenshot_assertion_retirement_.Acknowledge(work.physical,false,failure.empty()?"Native screenshot codec submission rejected":failure,Now());
  screenshot_assertion_pixels_.erase(found);
  if(work.sample)screenshot_assertion_captures_.erase({identity.owner,work.sample});
  ScreenshotAssertionReceiptFinished(identity);
}
void ScreenshotAssertionCaptureChannelClosed(){
  // Raw capture facts are unusable after actual channel death. Native CPU jobs
  // and their admitted tickets remain until their own real completion.
  screenshot_assertion_captures_.clear();
}
void ScreenshotAssertionCaptureRetired(suitecut::ScreenshotAssertionRetirement::Identity identity){
  // Called only after full physical scope retirement, not logical Complete.
  const auto scope=screenshot_assertion_scopes_.find(identity.owner);
  if(scope!=screenshot_assertion_scopes_.end()&&scope->second.identity==identity){scope->second.prepared=false;scope->second.baseline_valid=false;scope->second.capture_valid=false;}
  for(auto it=screenshot_assertion_captures_.begin();it!=screenshot_assertion_captures_.end();)if(it->second.identity==identity)it=screenshot_assertion_captures_.erase(it);else ++it;
}
