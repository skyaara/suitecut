// SPDX-License-Identifier: MIT
std::unique_ptr<SuiteCutTouchControls> native_touch_;
void TouchDispatch(SuiteCutTouchControls::Packet packet,SuiteCutTouchControls::Reply reply){
  if(!browser_||closing_page_){reply(false,"Native touch page is closed");return;}
  browser_->GetHost()->SetFocus(true);
  auto params=CefDictionaryValue::Create();std::string method;
  if(packet.kind==SuiteCutTouchControls::Kind::Configure){
    method="Emulation.setTouchEmulationEnabled";params->SetBool("enabled",packet.enabled);
    if(packet.enabled)params->SetInt("maxTouchPoints",1);
  }else{
    method="Input.dispatchTouchEvent";
    params->SetString("type",packet.kind==SuiteCutTouchControls::Kind::Start?"touchStart":
      packet.kind==SuiteCutTouchControls::Kind::End?"touchEnd":"touchCancel");
    auto points=CefListValue::Create();
    if(packet.kind==SuiteCutTouchControls::Kind::Start){
      auto point=CefDictionaryValue::Create();point->SetInt("id",0);
      point->SetDouble("x",packet.x);point->SetDouble("y",packet.y);
      point->SetDouble("radiusX",1);point->SetDouble("radiusY",1);point->SetDouble("force",1);
      points->SetDictionary(0,point);
    }
    params->SetList("touchPoints",points);params->SetInt("modifiers",key_modifiers_);
  }
  const int assigned=AllocateDevToolsId();
  if(!assigned){reply(false,"Native touch physical command capacity exceeded");return;}
  // Owner zero plus retained-control semantics keeps the physical contact
  // cleanup alive after its originating command has timed out or canceled.
  devtools_.emplace(assigned,DevToolsCall{0,[reply=std::move(reply)](bool ok,const std::string& json){
    reply(ok,ok?"":"Native touch dispatch failed: "+json.substr(0,1024));
  },"",true,65536,true});
  if(!SubmitDevTools(assigned,method,params,"")){
    auto found=devtools_.find(assigned);
    if(found!=devtools_.end()){auto callback=std::move(found->second.callback);devtools_.erase(found);callback(false,"Native touch dispatch submission failed");}
  }
}
void EnsureNativeTouch(){
  if(!native_touch_)native_touch_=std::make_unique<SuiteCutTouchControls>(SuiteCutTouchControls::Hooks{
    [this](SuiteCutTouchControls::Packet packet,SuiteCutTouchControls::Reply reply){TouchDispatch(packet,std::move(reply));},
    [this](int owner,bool ok,const std::string& error){
      const auto work=input_work_.find(owner);
      if(ok&&work!=input_work_.end()&&work->second->action=="tap")FinishActionInput(work->second);
      else Complete(owner,ok,error);
    }});
}
void BeginTouch(int id,Dict message){
  EnsureNativeTouch();
  if(message->GetString("action")=="configure"&&message->GetType("enabled")==VTYPE_BOOL){
    native_touch_->Configure(id,message->GetBool("enabled"));return;
  }
  if(message->GetString("action")!="tap"||
    (message->GetType("x")!=VTYPE_INT&&message->GetType("x")!=VTYPE_DOUBLE)||
    (message->GetType("y")!=VTYPE_INT&&message->GetType("y")!=VTYPE_DOUBLE)){
    Complete(id,false,"Invalid native touch command");return;
  }
  native_touch_->Tap(id,Number(message,"x"),Number(message,"y"));
}
