// SPDX-License-Identifier: MIT
// Included in Source. No controller or idle IO exists until the first API use.
std::shared_ptr<suitecut::NativeIdleControls> native_idle_;
int native_idle_dispatch_=0;
bool NativeIdleDispatchCurrent(int id)const{return id!=native_idle_dispatch_||(native_idle_&&native_idle_->DispatchCurrent());}
void NativeIdleEnsure(){
  if(native_idle_)return;
  native_idle_=std::make_shared<suitecut::NativeIdleControls>(
    [this](int owner,const std::string&session,std::optional<suitecut::NativeIdleControls::State> state,suitecut::NativeIdleControls::Result done){
      if(!browser_||closing_page_){done(false,"Native idle page is closed");return;}
      const int id=AllocateDevToolsId();if(!id){done(false,"Native idle command capacity exceeded");return;}
      auto params=CefDictionaryValue::Create();if(state){params->SetBool("isUserActive",state->user_active);params->SetBool("isScreenUnlocked",state->screen_unlocked);}
      auto receipt=std::make_shared<suitecut::NativeIdleControls::Receipt>(std::move(done));
      native_idle_dispatch_=id;
      devtools_.emplace(id,DevToolsCall{owner,[self=CefRefPtr<Source>(this),id,receipt](bool ok,const std::string&error){if(self->native_idle_dispatch_==id)self->native_idle_dispatch_=0;receipt->Settle(ok,error);},session,true,65536,true});
      if(!SubmitDevTools(id,state?"Emulation.setIdleOverride":"Emulation.clearIdleOverride",params,session)){
        const auto found=devtools_.find(id);if(found!=devtools_.end()){auto callback=std::move(found->second.callback);devtools_.erase(found);callback(false,"Native idle command dispatch failed");}
      }
    },
    [this]{std::vector<std::string> actual;actual.reserve(sessions_.size());for(const auto&[session,frame]:sessions_)actual.push_back(session);return actual;},
    [this](const std::string&session){return browser_&&!closing_page_&&(session.empty()||sessions_.contains(session));},[]{return Now();});
}
void NativeIdleBegin(int id,uint64_t generation,Dict input,double deadline){
  std::optional<suitecut::NativeIdleControls::State>state;
  if(input->GetType("state")!=VTYPE_NULL){
    const auto value=input->GetDictionary("state");CefDictionaryValue::KeyList keys;if(value)value->GetKeys(keys);
    if(!value||keys.size()!=2||value->GetType("isUserActive")!=VTYPE_BOOL||value->GetType("isScreenUnlocked")!=VTYPE_BOOL){Complete(id,false,"Native idle state requires exactly isUserActive and isScreenUnlocked booleans, or null reset");return;}
    state=suitecut::NativeIdleControls::State{value->GetBool("isUserActive"),value->GetBool("isScreenUnlocked")};
  }
  NativeIdleEnsure();
  if(!native_idle_->Apply(id,generation,state,deadline,[this,id,generation](bool ok,const std::string&error){
    if(!operations_.IsCurrent(id,generation))return;
    if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native idle original operation deadline expired");return;}
    Complete(id,ok,error,"{}");
  }))Complete(id,false,"Native idle operation is invalid or native queue capacity exceeded");
}
void NativeIdleChild(const std::string&session,double deadline,suitecut::ContextSettingsPage::Result done){
  if(!native_idle_){done(true,"");return;}
  auto failed=done;if(!native_idle_->Child(session,deadline,std::move(done)))failed(false,"Native idle child capacity or deadline is invalid");
}
