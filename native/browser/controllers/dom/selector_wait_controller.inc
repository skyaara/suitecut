// SPDX-License-Identifier: MIT
std::unique_ptr<suitecut::SelectorWaitControls> selector_waits_;
suitecut::SelectorWaitControls& SelectorWaits(){
  if(!selector_waits_)selector_waits_=std::make_unique<suitecut::SelectorWaitControls>([]{return Now();},
    [this](const std::string& message){NetworkFatal(message);},
    [this](int owner,const std::string& group,const std::string& session,std::function<void()> settled){
      // Only an actual Inspector channel-close receipt removes sessions_.
      // A prior group ACK can continue this sequential release after that close.
      if(!session.empty()&&!sessions_.contains(session)){settled();return;}
      const int call=AllocateDevToolsId();if(!browser_||!call){settled();return;}
      auto params=CefDictionaryValue::Create();params->SetString("objectGroup",group);
      auto completed=[this,owner,session,settled=std::move(settled)](bool ok,const std::string&error)mutable{
        if(!ok&&browser_&&(session.empty()||sessions_.contains(session))){AssertionDeadlineReleaseFailed(owner);ScreenshotAssertionReleaseFailed(owner);}
        settled();if(!ok&&browser_&&(session.empty()||sessions_.contains(session)))NetworkFatal("Native selector wait object-group release failed: "+error.substr(0,1024));
      };
      DevToolsCall pending{owner,std::move(completed),session,true,kMaxResponseBytes,true};
      pending.selector_group_release=true;
      devtools_.emplace(call,std::move(pending));
      if(!SubmitDevTools(call,"Runtime.releaseObjectGroup",params,session)){
        const auto found=devtools_.find(call);if(found!=devtools_.end()){auto done=std::move(found->second.callback);devtools_.erase(found);done(false,"{}");}
      }
    });
  return *selector_waits_;
}
// A flattened child-channel rejection is returned on the parent channel by
// Chromium. Accept only the exact denied selector-cleanup receipt, never a
// successful/other-session response or a generic author/protocol error.
bool SelectorWaitRejectedSession(int id,bool success,const void* result,size_t size,const std::string& actual){
  if(success||!actual.empty()||!size||size>512)return false;
  const auto found=devtools_.find(id);
  if(found==devtools_.end()||!found->second.selector_group_release||!found->second.retained_control||found->second.session.empty())return false;
  const auto json=std::string(static_cast<const char*>(result),size);
  const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto error=parsed?parsed->GetDictionary():nullptr;
  if(!error||error->GetType("code")!=VTYPE_INT||error->GetInt("code")!=-32001||error->GetType("message")!=VTYPE_STRING||error->GetString("message")!="Session with given id not found.")return false;
  const auto expected=found->second.session;auto acknowledged=std::move(found->second.callback);devtools_.erase(found);
  // This actual backend denial proves the exact accepted channel is gone.
  // Retire other physical work through the existing real-channel boundary;
  // the denied command's own callback remains alive until it settles below.
  NativeTargetSessionDetached(expected);
  acknowledged(false,json);return true;
}
