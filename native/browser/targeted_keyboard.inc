// SPDX-License-Identifier: MIT
// Included inside Source. Reuse the existing native device queue and original
// operation; never focus a target before that operation owns the keyboard.
void BeginTargetedKeyboard(int id,uint64_t generation,Dict target,const std::string& action,const std::string& text) {
  auto input=CefDictionaryValue::Create();
  input->SetString("kind","keyboard");input->SetString("action",action);
  input->SetString("key",target->GetString("key"));input->SetString("text",text);
  if(target->HasKey("delay"))input->SetValue("delay",target->GetValue("delay"));
  BeginInput(id,generation,input,target);
}
bool PrepareTargetedKeyboard(const std::shared_ptr<InputWork>& work) {
  if(!work->keyboard_target)return false;
  if(keyboard_input_!=work->owner||!operations_.IsCurrent(work->owner,work->generation))return true;
  if(operations_.IsExpired(work->owner,work->generation,Now())){
    Complete(work->owner,false,"Native targeted keyboard original deadline expired");return true;
  }
  if(work->keyboard_target_prepared)return false;
  work->busy=true;
  LocatorAction(work->owner,work->generation,
    suitecut::InboundDisplay(suitecut::InboundString(work->keyboard_target,"selector")),
    work->action,work->text,work->keyboard_target);
  return true;
}
void TargetedKeyboardPrepared(int id,uint64_t generation) {
  const auto found=input_work_.find(id);
  if(found==input_work_.end()||found->second->generation!=generation||!found->second->keyboard_target||keyboard_input_!=id){
    if(operations_.IsCurrent(id,generation))Complete(id,false,"Native targeted keyboard preparation has no owning queue lease");
    return;
  }
  if(!operations_.IsCurrent(id,generation))return;
  if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native targeted keyboard original deadline expired");return;}
  const auto work=found->second;
  work->keyboard_target_prepared=true;work->busy=false;
  context_retries_.erase(id);
  ContinueInput(work);
}
// Keep potential held-key ownership until genuine keyUp ACK. The same retained
// native cleanup path used by sendKeys releases these after logical cancel.
void TargetedKeyboardSubmitted(const std::shared_ptr<InputWork>& work,const std::string& method,Dict params) {
  if(!work->keyboard_target||method!="Input.dispatchKeyEvent"||params->GetString("type")=="keyUp")return;
  const auto found=pressed_keys_.find(params->GetString("code").ToString());
  if(found==pressed_keys_.end())return;
  const auto& key=found->second;
  if(std::none_of(work->send_keys_held.begin(),work->send_keys_held.end(),[&](const SuiteCutKey& held){return held.code==key.code;}))
    work->send_keys_held.push_back(key);
}
// Reuse sendKeys' existing retained command counter/real acknowledgement path.
// Logical cancellation may stop input, never release its still-live focus/key.
bool RetainsKeyboardCall(int owner,const std::string& method) const {
  const auto input=input_work_.find(owner);
  if(input==input_work_.end())return false;
  const auto& work=input->second;
  const bool prepare=method=="Runtime.evaluate"||method=="Runtime.callFunctionOn";
  const bool key=method=="Input.dispatchKeyEvent"||method=="Input.insertText";
  return (work->action=="prepareSendKeys"&&prepare)||work->keyboard_target||(work->standards_send_keys&&key);
}
bool RetainKeyboardCall(int owner,const std::string& method,Dict params,std::function<void(bool,const std::string&)>& callback) {
  const auto input=input_work_.find(owner);
  if(input==input_work_.end())return false;
  const auto generation=input->second->generation;
  if(send_keys_physical_count_>=64){Complete(owner,false,"Native send-keys physical preparation capacity exceeded");return false;}
  ++send_keys_physical_[owner];++send_keys_physical_count_;
  callback=[this,owner,generation,method,params,original=std::move(callback)](bool ok,const std::string& json) mutable {
    SendKeysSequenceAcknowledged(owner,method,params,ok);
    SendKeysPhysicalFinished(owner);
    if(operations_.IsCurrent(owner,generation))original(ok,json);
  };
  return true;
}
