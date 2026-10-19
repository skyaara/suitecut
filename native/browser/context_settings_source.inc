// SPDX-License-Identifier: MIT
// Included inside Source. The first empty CEF document is kept free of authored
// context scripts. Native renderer overrides ACK, then current authored init
// scripts are installed in that same actual initial empty document. Public operations still wait for Source's actual ready packet.
std::shared_ptr<const suitecut::ContextSettings> context_settings_;
std::shared_ptr<suitecut::ContextSettingsPage> settings_page_;
bool initial_empty_document_=false,initial_empty_ready_=false;
std::string initial_document_url_,initial_document_state_,initial_cef_frame_;int initial_document_history_=0;
bool settings_bootstrap_=false,settings_empty_seen_=false,settings_started_=false,settings_applied_=false,settings_committed_=false;
std::atomic_bool context_auth_open_{true};
std::unique_ptr<suitecut::NativeNetworkFatalTeardown> network_fatal_;
bool authentication_ready_=false;
CefRefPtr<CefRequest> settings_initial_request_;
std::weak_ptr<suitecut::NativeCreationWatchdog> initial_empty_watchdog_,settings_watchdog_;
void CancelSettingsWatchdog(std::weak_ptr<suitecut::NativeCreationWatchdog>& handle){
  if(!CefCurrentlyOn(TID_UI))throw std::runtime_error("Native creation watchdog canceled outside UI");
  const auto actual=handle.lock();
  handle.reset();
  if(actual)actual->Cancel();
}
void CancelSettingsWatchdogs(){CancelSettingsWatchdog(initial_empty_watchdog_);CancelSettingsWatchdog(settings_watchdog_);}
bool PostSettingsWatchdog(std::weak_ptr<suitecut::NativeCreationWatchdog>& handle,std::function<void()> callback){
  if(!CefCurrentlyOn(TID_UI))throw std::runtime_error("Native creation watchdog posted outside UI");
  if(!handle.expired())throw std::runtime_error("Native creation watchdog admission replayed");
  const auto actual=std::make_shared<suitecut::NativeCreationWatchdog>(std::move(callback),native_creation_origin_.Origin());
  handle=actual;
  if(CefPostDelayedTask(TID_UI,new CreationWatchdogTask(actual),30000))return true;
  actual->Cancel();handle.reset();return false;
}
void NetworkFatal(const std::string&error){
  if(closing_page_)return;
  if(!network_fatal_)network_fatal_=std::make_unique<suitecut::NativeNetworkFatalTeardown>(
    [this](std::function<void()> task){return CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this),task=std::move(task)]{if(!self->closing_page_)task();}));},
    [this](const std::string&message){if(closed_reason_.empty())closed_reason_=message;},
    [this]{if(browser_&&!closing_page_)browser_->GetHost()->CloseBrowser(true);});
  network_fatal_->Fail(error);WakeDeadlines();
}
void SettingsFail(const std::string&error){CancelSettingsWatchdogs();NetworkFatal(error);}
void SettingsPrepare(){
  if(initial_empty_document_){
    if(!PostSettingsWatchdog(initial_empty_watchdog_,[self=CefRefPtr<Source>(this)]{if(!self->initial_empty_ready_&&!self->closing_page_)self->SettingsFail("Native initial empty document initialization deadline exceeded");})){SettingsFail("CEF refused native initial document watchdog");return;}
    CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this)]{if(!self->closing_page_&&self->browser_&&!self->initial_empty_ready_)self->browser_->GetMainFrame()->ExecuteJavaScript(suitecut::kNativeEmptyScript.source,suitecut::kNativeEmptyScript.name,1);}));
  }
  if(!context_settings_||!context_settings_->BootstrapRequired()){settings_applied_=true;GeolocationStart();return;}
  settings_page_=std::make_shared<suitecut::ContextSettingsPage>(
    [this](const std::string&session,const std::string&method,Dict params,suitecut::ContextSettingsPage::Result done){
      if(method=="Emulation.setTouchEmulationEnabled"&&session.empty()){
        EnsureNativeTouch();native_touch_->ConfigureBootstrap(params->GetBool("enabled"),std::move(done));return;
      }
      if(method=="Emulation.setEmulatedMedia"&&session.empty()){
        NativeMediaApply(0,params,[this,params,done=std::move(done)](bool ok,const std::string&json){
          if(ok){native_media_params_=params->Copy(false);}
          done(ok,json);
        });return;
      }
      const int id=AllocateDevToolsId();
      if(!id||network_calls_.size()>=1024||closing_page_){done(false,"Native context settings command capacity exceeded or page closed");return;}
      if(method=="Emulation.setLocaleOverride"&&session.empty()&&engine_.contexts()->IsPopup(page_id_)){
        auto original=std::move(done);done=[this,original=std::move(original)](bool ok,const std::string&error){
          if(ok||error.find("Another locale override is already in effect")==std::string::npos){original(ok,error);return;}
          // A same-process popup may already inherit its opener's ICU locale.
          // Read a fresh isolated native realm; never infer success from error text.
          auto params=CefDictionaryValue::Create();params->SetString("frameId",main_frame_id_);params->SetString("worldName","__suitecut_context_settings");
          SettingsReadCommand("Page.createIsolatedWorld",params,[this,original=std::move(original),error](bool world_ok,const std::string&json){
            auto value=CefParseJSON(json,JSON_PARSER_RFC);auto result=value?value->GetDictionary():nullptr;
            if(!world_ok||!result||result->GetType("executionContextId")!=VTYPE_INT){original(false,error);return;}
            auto query=CefDictionaryValue::Create();query->SetInt("contextId",result->GetInt("executionContextId"));query->SetString("expression","Intl.DateTimeFormat().resolvedOptions().locale");query->SetBool("returnByValue",true);
            SettingsReadCommand("Runtime.evaluate",query,[this,original=std::move(original),error](bool read_ok,const std::string&json){
              auto value=CefParseJSON(json,JSON_PARSER_RFC);auto result=value?value->GetDictionary():nullptr;auto observed=result?result->GetDictionary("result"):nullptr;
              const bool inherited=read_ok&&result&&!result->HasKey("exceptionDetails")&&observed&&observed->GetType("value")==VTYPE_STRING&&context_settings_->locale&&observed->GetString("value")==*context_settings_->locale;
              original(inherited,inherited?std::string{}:error);
            });
          });
        };
      }
      network_calls_.emplace(id,NetworkCall{session,std::move(done),Now()+30000});WakeDeadlines();
      if(!SubmitDevTools(id,method,params,session)){
        const auto found=network_calls_.find(id);if(found!=network_calls_.end()){auto done=std::move(found->second.callback);network_calls_.erase(found);done(false,"Native context settings dispatch failed");}
      }
    });
  if(!PostSettingsWatchdog(settings_watchdog_,[self=CefRefPtr<Source>(this)]{if(!self->settings_applied_&&!self->closing_page_)self->SettingsFail("Native context settings bootstrap deadline exceeded");})){SettingsFail("CEF refused native context settings watchdog");return;}
  SettingsStart();
}
void SettingsReadCommand(const std::string&method,Dict params,suitecut::ContextSettingsPage::Result done){
  const int id=AllocateDevToolsId();if(!id||network_calls_.size()>=1024||closing_page_){done(false,"Native settings read capacity exceeded or page closed");return;}
  network_calls_.emplace(id,NetworkCall{"",std::move(done),Now()+30000});WakeDeadlines();
  if(!SubmitDevTools(id,method,params,"")){auto found=network_calls_.find(id);if(found!=network_calls_.end()){auto callback=std::move(found->second.callback);network_calls_.erase(found);callback(false,"Native settings read dispatch failed");}}
}
void SettingsStart(){
  if(!settings_empty_seen_||settings_started_||!settings_page_||!browser_||closing_page_)return;
  settings_started_=true;
  CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this)]{
    if(self->closing_page_||!self->browser_||!self->settings_page_)return;
    auto plan=self->context_settings_->Plan(true,self->width_,self->height_,self->scale_);plan.erase(std::remove_if(plan.begin(),plan.end(),[](const auto&step){return step.method=="Emulation.setGeolocationOverride";}),plan.end());
    self->settings_page_->Attach("",std::move(plan),[self](bool ok,const std::string&error){
      if(self->closing_page_)return;
      if(!ok){self->SettingsFail("Native context settings initialization failed: "+error);return;}
      self->settings_applied_=true;self->CancelSettingsWatchdog(self->settings_watchdog_);self->GeolocationStart();
    });
  }));
}
void SettingsCommit(){
  if(!settings_bootstrap_){MaybeReady();return;}
  if(!native_websocket_policy_root_ready_||!settings_bootstrap_||settings_committed_||!settings_empty_seen_||!settings_applied_||!geolocation_ready_||!authentication_ready_||!network_policy_ready_||!browser_||closing_page_)return;
  if(!NativePristineInitialGate())return;
  // Never navigate synchronously inside a native DevTools observer callback.
  CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this)]{
    if(!self->settings_bootstrap_||self->closing_page_||!self->browser_)return;
    self->settings_committed_=true;
    self->engine_.contexts()->CredentialReadyStore()->Bootstrap(self->browser_->GetIdentifier(),false);
    self->browser_->GetMainFrame()->SendProcessMessage(PID_RENDERER,CefProcessMessage::Create("suitecutApplyInitialSettings"));
  }));
}
bool SettingsMessage(CefRefPtr<CefFrame>frame,CefProcessId sender,CefRefPtr<CefProcessMessage>message){
  if(sender==PID_RENDERER&&message->GetName()=="suitecutInitialEmptyFailure"){
    if(initial_empty_document_&&!initial_empty_ready_&&frame&&frame->IsMain()&&(!browser_||frame->GetBrowser()->GetIdentifier()==browser_->GetIdentifier()))SettingsFail(message->GetArgumentList()->GetString(0));
    return true;
  }
  if(sender==PID_RENDERER&&message->GetName()=="suitecutInitialEmptyReady"){
    if(initial_empty_document_&&frame&&frame->IsMain()&&(!browser_||frame->GetBrowser()->GetIdentifier()==browser_->GetIdentifier())){
      const auto args=message->GetArgumentList();
      if(args->GetSize()!=5||args->GetType(0)!=VTYPE_INT||args->GetType(1)!=VTYPE_STRING||args->GetType(2)!=VTYPE_STRING||args->GetType(3)!=VTYPE_STRING||args->GetType(4)!=VTYPE_INT||args->GetInt(0)!=frame->GetBrowser()->GetIdentifier()||args->GetString(1)!=frame->GetIdentifier()){SettingsFail("Native initial document identity evidence is invalid");return true;}
      initial_document_url_=args->GetString(2);initial_document_state_=args->GetString(3);initial_document_history_=args->GetInt(4);initial_cef_frame_=args->GetString(1);
      if(initial_document_state_!="complete"){SettingsFail("Actual initial document is not complete after native settings initialization");return true;}
      initial_empty_ready_=true;CancelSettingsWatchdog(initial_empty_watchdog_);settings_bootstrap_=false;initial_loaded_=true;MaybeReady();
    }
    return true;
  }
  if(sender!=PID_RENDERER||message->GetName()!="suitecutContextSettingsEmptyReady")return false;
  if(!settings_bootstrap_||!frame||!frame->IsMain()||(browser_&&frame->GetBrowser()->GetIdentifier()!=browser_->GetIdentifier()))return true;
  settings_empty_seen_=true;SettingsStart();SettingsCommit();return true;
}
bool OnBeforeBrowse(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest>request,bool,bool)override{
  return !engine_.contexts()->PageOriginObserved(page_id_,request->GetURL());
}
suitecut::NativeContextAuthenticationControls::Snapshot authentication_snapshot_;
void SettingsAuthenticate(){
  const int context=engine_.contexts()->ContextId(page_id_);
  const bool accepted=engine_.authentication().Attach(context,"page:"+std::to_string(page_id_),
    [self=CefRefPtr<Source>(this)](suitecut::NativeContextAuthenticationControls::Value,suitecut::NativeContextAuthenticationControls::Result done){
      if(self->closing_page_||!self->network_){done(false,"Native authentication page is closed");return;}
      auto provider=suitecut::NativeContextAuthentication(self->context_settings_,self->authentication_snapshot_);
      if(provider){auto original=std::move(provider);provider=[source=self.get(),original=std::move(original)](Dict input){if(source->context_auth_open_.load())return original(input);auto canceled=CefDictionaryValue::Create();canceled->SetString("response","CancelAuth");return canceled;};}
      self->network_->SetAuthentication(std::move(provider),std::move(done));
    },[self=CefRefPtr<Source>(this)](bool ok,const std::string&error){
      if(self->closing_page_)return;
      if(!ok){self->SettingsFail("Native context authentication initialization failed: "+error);return;}
      self->authentication_ready_=true;self->SettingsCommit();self->MaybeReady();
    });
  if(!accepted)SettingsFail("Native context authentication target capacity exceeded");
}
void SettingsChild(const std::string&session,suitecut::ContextSettingsPage::Result done){
  if(!context_settings_||!context_settings_->RendererOverrides()){GeolocationAttach(session,std::move(done));return;}
  if(!settings_page_){done(false,"Native context settings root has not initialized");return;}
  auto steps=context_settings_->Plan(false,width_,height_,scale_);
  // Media uses the actual page controller, including later page-level updates.
  steps.erase(std::remove_if(steps.begin(),steps.end(),[](const auto&step){return step.method=="Emulation.setEmulatedMedia"||step.method=="Emulation.setGeolocationOverride";}),steps.end());
  settings_page_->Attach(session,std::move(steps),[this,session,done=std::move(done)](bool ok,const std::string&error)mutable{if(!ok){done(false,error);return;}GeolocationAttach(session,std::move(done));});
}

// CEF invokes auth on IO. context_settings_ is an immutable constructor-owned
// snapshot, published with the actual CefClient and never reset on page close.
bool GetAuthCredentials(CefRefPtr<CefBrowser>,const CefString&url,bool proxy,const CefString&,int,const CefString&,const CefString&,CefRefPtr<CefAuthCallback>callback)override{
  return suitecut::NativeContextAuthenticationCallback(context_settings_,authentication_snapshot_,context_auth_open_.load(),url,proxy,std::move(callback));
}
bool OnCertificateError(CefRefPtr<CefBrowser>,cef_errorcode_t,const CefString&,CefRefPtr<CefSSLInfo>,CefRefPtr<CefCallback>callback)override{
  if(!context_settings_||!context_settings_->ignore_https_errors.value_or(false))return false;
  callback->Continue();return true;
}
