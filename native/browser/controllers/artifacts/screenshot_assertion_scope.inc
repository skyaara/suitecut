// SPDX-License-Identifier: MIT
// Included inside Source and its controlled native sender host. All documents
// below come from real Source metadata, never a screenshot request's JSON.
suitecut::ScreenshotAssertionRetirement screenshot_assertion_retirement_;
struct ScreenshotAssertionScope {
  suitecut::ScreenshotAssertionRetirement::Identity identity;
  uint64_t renderer=0;
  std::map<std::string,SnapshotReferenceDocument>documents;
  bool prepared=false,baseline_valid=false,capture_valid=false;
};
std::map<int,ScreenshotAssertionScope>screenshot_assertion_scopes_;
bool ScreenshotAssertionScopeBegin(int owner,uint64_t generation,double deadline){
  using Identity=suitecut::ScreenshotAssertionRetirement::Identity;
  const Identity identity{page_id_,owner,generation};
  if(screenshot_assertion_scopes_.contains(owner)||!operations_.BindOriginalDeadline(owner,generation,deadline))return false;
  try{
    const auto [position,inserted]=screenshot_assertion_scopes_.emplace(owner,ScreenshotAssertionScope{identity,renderer_views_,{}});
    if(!inserted)return false;
    if(!screenshot_assertion_retirement_.Admit(identity,deadline)){screenshot_assertion_scopes_.erase(position);return false;}
    // Baseline decoding requires no realm. A page can still be creating its
    // first utility context; pin a real root now if available, and otherwise
    // require its full native document at the first actual renderer dispatch.
    if(ReferenceDocument(main_frame_id_)&&!ScreenshotAssertionRememberFrame(identity,main_frame_id_)){
      screenshot_assertion_retirement_.Fail(identity,"Native screenshot root document changed at admission",Now());
      screenshot_assertion_retirement_.CleanupClosed(identity);
      screenshot_assertion_retirement_.Retire(identity,Now());screenshot_assertion_scopes_.erase(owner);return false;
    }
    return true;
  }catch(const std::bad_alloc&){return false;}
}
bool ScreenshotAssertionPinsCurrent(suitecut::ScreenshotAssertionRetirement::Identity identity)const{
  const auto found=screenshot_assertion_scopes_.find(identity.owner);
  if(found==screenshot_assertion_scopes_.end()||found->second.identity!=identity||found->second.renderer!=renderer_views_||identity.page!=page_id_)return false;
  for(const auto&[frame,pinned]:found->second.documents){const auto actual=ReferenceDocument(frame);if(!actual||*actual!=pinned)return false;}
  return true;
}
bool ScreenshotAssertionRememberFrame(suitecut::ScreenshotAssertionRetirement::Identity identity,const std::string&frame){
  const auto found=screenshot_assertion_scopes_.find(identity.owner);
  if(found==screenshot_assertion_scopes_.end()||found->second.identity!=identity||!ScreenshotAssertionPinsCurrent(identity))return false;
  try{
    const auto actual=ReferenceDocument(frame);if(!actual)return false;
    const auto pinned=found->second.documents.find(frame);
    if(pinned!=found->second.documents.end())return pinned->second==*actual;
    if(found->second.documents.size()>=1024)return false;
    found->second.documents.emplace(frame,*actual);return true;
  }catch(const std::bad_alloc&){return false;}
}
std::shared_ptr<suitecut::SelectorWaitControls::Physical> ScreenshotAssertionSelectorPhysical(
    suitecut::ScreenshotAssertionRetirement::Identity identity,Dict params,
    const std::string&session,bool cleanup){
  if(cleanup)return {};
  if(!SelectorWaits().Admit(identity.owner,identity.generation)||!ReferenceTemporaryGroups(identity.owner))return {};
  if(params&&params->HasKey("objectGroup")){
    if(params->GetType("objectGroup")!=VTYPE_STRING||!SelectorWaits().Group(identity.owner,params->GetString("objectGroup"),session))return {};
  }
  return SelectorWaits().Begin(identity.owner);
}
bool ScreenshotAssertionScopeRetire(suitecut::ScreenshotAssertionRetirement::Identity identity){
  if(!screenshot_assertion_retirement_.Retire(identity,Now()))return false;
  ScreenshotAssertionCaptureRetired(identity);
  const auto found=screenshot_assertion_scopes_.find(identity.owner);
  if(found!=screenshot_assertion_scopes_.end()&&found->second.identity==identity)screenshot_assertion_scopes_.erase(found);
  return true;
}
