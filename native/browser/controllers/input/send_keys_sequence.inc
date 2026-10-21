// SPDX-License-Identifier: MIT
// Included inside Source. Only the distinct standards sendKeys intent enters
// this scheduler; ordinary type/press retain their existing behavior.
Dict SendKeysPacket(const SuiteCutKey& key,bool down) {
  const bool repeat=down&&pressed_keys_.contains(key.code);
  if(down)pressed_keys_[key.code]=key;else pressed_keys_.erase(key.code);
  key_modifiers_=0;for(const auto& [code,held]:pressed_keys_)key_modifiers_|=held.modifier;
  auto params=CefDictionaryValue::Create();
  params->SetString("type",down?(key.text.empty()?"rawKeyDown":"keyDown"):"keyUp");
  params->SetString("key",key.key);params->SetString("code",key.code);
  params->SetInt("windowsVirtualKeyCode",key.virtual_key);params->SetInt("nativeVirtualKeyCode",key.virtual_key);
#if defined(__APPLE__)
  if(down&&key_modifiers_==4&&key.code=="KeyA"){
    auto commands=CefListValue::Create();commands->SetString(0,"selectAll");params->SetList("commands",commands);
  }
#endif
  params->SetInt("modifiers",key_modifiers_);params->SetBool("autoRepeat",repeat);
  params->SetInt("location",key.location);params->SetBool("isKeypad",key.location==3);
  if(down&&!key.text.empty()){params->SetString("text",key.text);params->SetString("unmodifiedText",key.unmodified_text);}
  return params;
}
void SendKeysSequenceAcknowledged(int owner,const std::string& method,Dict params,bool ok) {
  if(!ok||!params||method!="Input.dispatchKeyEvent"||params->GetString("type")!="keyUp")return;
  const auto input=input_work_.find(owner);if(input==input_work_.end()||(!input->second->standards_send_keys&&!input->second->keyboard_target))return;
  auto& work=*input->second;const auto code=params->GetString("code").ToString();
  work.send_keys_held.erase(std::remove_if(work.send_keys_held.begin(),work.send_keys_held.end(),[&](const SuiteCutKey& key){return key.code==code;}),work.send_keys_held.end());
  if(code=="ShiftLeft")work.send_keys_auto_shift=false;
}
std::set<int> send_keys_cleanup_commands_;
bool SendKeysCleanupCommand(int command) const { return send_keys_cleanup_commands_.contains(command); }
void FailSendKeysCleanup(const std::shared_ptr<InputWork>& work,const char* reason) {
  if(work->send_keys_cleanup_failed)return;
  work->send_keys_cleanup_failed=true;
  if(closed_reason_.empty())closed_reason_=reason;
  // No protocol counter or local map mutation proves a physical key release.
  // Keep the queue owner/descriptors until actual OnBeforeClose clears them.
  CefPostTask(TID_UI,new Task([self=CefRefPtr<Source>(this)]{
    if(self->browser_)self->browser_->GetHost()->CloseBrowser(true);
  }));
}
bool CleanupSendKeysSequence(const std::shared_ptr<InputWork>& work) {
  if(work->send_keys_held.empty())return false;
  if(work->send_keys_cleanup_failed)return true;
  const auto key=work->send_keys_held.back();
  const int assigned=AllocateDevToolsId();
  if(!assigned){FailSendKeysCleanup(work,"Native send-keys cleanup could not allocate a physical key-release command");return true;}
  // Build the real release packet without deleting actual held-state ownership
  // until a successful native ACK confirms dispatch, or the target closes.
  auto params=CefDictionaryValue::Create();
  int remaining_modifiers=0;
  for(const auto& [code,held]:pressed_keys_)if(code!=key.code)remaining_modifiers|=held.modifier;
  params->SetString("type","keyUp");params->SetString("key",key.key);params->SetString("code",key.code);
  params->SetInt("windowsVirtualKeyCode",key.virtual_key);params->SetInt("nativeVirtualKeyCode",key.virtual_key);
  params->SetInt("modifiers",remaining_modifiers);params->SetInt("location",key.location);params->SetBool("isKeypad",key.location==3);
  ++send_keys_physical_[work->owner];++send_keys_physical_count_;
  send_keys_cleanup_commands_.insert(assigned);
  auto callback=[this,work,key,assigned](bool ok,const std::string&){
    send_keys_cleanup_commands_.erase(assigned);
    if(ok){
      work->send_keys_held.erase(std::remove_if(work->send_keys_held.begin(),work->send_keys_held.end(),[&](const SuiteCutKey& held){return held.code==key.code;}),work->send_keys_held.end());
      pressed_keys_.erase(key.code);key_modifiers_=0;
      for(const auto& [code,held]:pressed_keys_)key_modifiers_|=held.modifier;
      if(key.code=="ShiftLeft")work->send_keys_auto_shift=false;
    }else FailSendKeysCleanup(work,"Native send-keys cleanup physical key-release acknowledgement failed");
    SendKeysPhysicalFinished(work->owner);
  };
  devtools_.emplace(assigned,DevToolsCall{work->owner,std::move(callback),"",true,kMaxResponseBytes,true,false});
  if(!SubmitDevTools(assigned,"Input.dispatchKeyEvent",params,"")){
    devtools_.erase(assigned);send_keys_cleanup_commands_.erase(assigned);
    FailSendKeysCleanup(work,"Native send-keys cleanup physical key-release submission failed");
    SendKeysPhysicalFinished(work->owner);
  }
  return true;
}
void SendKeysSequenceStep(const std::shared_ptr<InputWork>& work) {
  using Kind=SuiteCutSendKeysToken::Kind;
  const int delay=work->message->GetInt("delay");
  auto release=[this,work](size_t index){
    const auto key=work->send_keys_held[index];
    if(key.code=="ShiftLeft")work->send_keys_auto_shift=false;
    InputCall(work,"Input.dispatchKeyEvent",SendKeysPacket(key,false),[this,work]{ContinueInput(work);});
  };
  if(work->phase==1){
    const auto key=work->send_keys_token.key;
    work->phase=0;work->cursor+=work->send_keys_token.bytes;
    InputCall(work,"Input.dispatchKeyEvent",SendKeysPacket(key,false),[this,work]{ContinueInput(work);});return;
  }
  if(work->phase==2){
    if(!work->send_keys_held.empty()){release(work->send_keys_held.size()-1);return;}
    work->phase=0;
    if(work->cursor>=work->text.size()){Complete(work->owner,true);return;}
  }
  if(work->cursor>=work->text.size()){
    work->phase=2;
    if(work->send_keys_held.empty())Complete(work->owner,true);else release(work->send_keys_held.size()-1);
    return;
  }
  auto token=SuiteCutNextSendKeysToken(work->text,work->cursor,key_modifiers_);
  if(token.kind==Kind::Invalid){Complete(work->owner,false,"Invalid native send-keys UTF-8 text");return;}
  if(token.kind==Kind::ReleaseModifiers){work->cursor+=token.bytes;work->phase=2;ContinueInput(work);return;}
  const bool modifier=token.kind==Kind::Key&&token.key.modifier;
  const bool shifted=token.kind==Kind::Key&&!token.private_key&&SuiteCutSendKeysShifted(token.text);
  if(!modifier&&!shifted&&work->send_keys_auto_shift){
    const auto shift=std::find_if(work->send_keys_held.begin(),work->send_keys_held.end(),[](const SuiteCutKey& key){return key.code=="ShiftLeft";});
    if(shift!=work->send_keys_held.end()){release(static_cast<size_t>(shift-work->send_keys_held.begin()));return;}
    work->send_keys_auto_shift=false;
  }
  if(shifted&&!(key_modifiers_&8)){
    auto shift=*SuiteCutDescribeKey("ShiftLeft",key_modifiers_);
    work->send_keys_held.push_back(shift);work->send_keys_auto_shift=true;
    InputCall(work,"Input.dispatchKeyEvent",SendKeysPacket(shift,true),[this,work]{ContinueInput(work);});return;
  }
  if(token.kind==Kind::Text){
    auto params=CefDictionaryValue::Create();params->SetString("text",token.text);work->cursor+=token.bytes;
    InputCall(work,"Input.insertText",params,[this,work,delay]{ContinueInput(work,delay);});return;
  }
  if(token.kind!=Kind::Key){Complete(work->owner,false,"Invalid native send-keys token");return;}
  if(work->send_keys_held.size()>=12){Complete(work->owner,false,"Native send-keys held-key capacity exceeded");return;}
  if(modifier){
    if(token.key.code=="ShiftLeft")work->send_keys_auto_shift=false;
    if(!pressed_keys_.contains(token.key.code))work->send_keys_held.push_back(token.key);
    work->cursor+=token.bytes;
    InputCall(work,"Input.dispatchKeyEvent",SendKeysPacket(token.key,true),[this,work,delay]{ContinueInput(work,delay);});return;
  }
  work->send_keys_token=token;work->send_keys_held.push_back(token.key);work->phase=1;
  InputCall(work,"Input.dispatchKeyEvent",SendKeysPacket(token.key,true),[this,work,delay]{ContinueInput(work,delay);});
}
