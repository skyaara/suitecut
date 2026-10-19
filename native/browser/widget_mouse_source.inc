// SPDX-License-Identifier: MIT
// Included inside Source after InputWork. Optional raw original-widget mouse
// control never borrows the legacy drag controller's synthetic ACKs.
std::shared_ptr<suitecut::NativeWidgetMouse> native_widget_mouse_;
bool native_widget_probe_started_=false,native_widget_probe_ready_=false;
bool NativeWidgetMouseReady(){
  if(native_widget_probe_ready_)return true;
  if(native_widget_probe_started_)return false;
  native_widget_probe_started_=true;
  suitecut::NativeWidgetMouse::Hooks hooks;
  hooks.send=[this](const std::string&session,const std::string&method,Dict params,auto done,double deadline){NativeNetworkSend(session,method,params,std::move(done),deadline);};
  hooks.now=[] {return Now();};
  hooks.current=[this](const suitecut::NativeWidgetMouse::Pin&pin){
    if(!browser_||closing_page_||renderer_failed_)return false;
    const auto found=frame_states_.find(pin.frame);
    return found!=frame_states_.end()&&found->second.loader==pin.loader&&found->second.session==pin.session&&found->second.context_identity==pin.identity&&native_pristine_renderer_epoch_==pin.generation;
  };
  hooks.fatal=[this](const std::string&error){Error(error.c_str());if(browser_)browser_->GetHost()->CloseBrowser(true);};
  native_widget_mouse_=std::make_shared<suitecut::NativeWidgetMouse>(std::move(hooks));
  native_widget_mouse_->Probe([this](bool,const std::string&){native_widget_probe_ready_=true;if(!closing_page_)MaybeReady();});
  // The native sender can reject synchronously. Its callback already retries
  // MaybeReady, so this first caller must not publish a second ready packet.
  return false;
}
bool NativeWidgetMouseComplete(int id,bool ok,const std::string&error,const std::string&json){
  const auto found=input_work_.find(id);
  if(found==input_work_.end()||found->second->message->GetString("backend")!="widget")return false;
  const auto work=found->second;
  if(closing_page_||work->widget_complete_done)return false;
  if(work->widget_completing){
    if(!ok){work->widget_completion_ok=false;if(work->widget_completion_error.empty())work->widget_completion_error=error;}
    return true;
  }
  work->widget_completing=true;work->widget_completion_ok=ok;work->widget_completion_error=error;work->widget_completion_json=json;
  if(!native_widget_mouse_||!native_widget_mouse_->owns(id)){work->widget_complete_done=true;return false;}
  native_widget_mouse_->Finish(id,!ok,[this,work](bool success,const std::string&reason){
    work->widget_complete_done=true;
    if(native_widget_mouse_&&!native_widget_mouse_->held())mouse_buttons_=0;
    if(!operations_.IsCurrent(work->owner,work->generation))return;
    const bool expired=operations_.IsExpired(work->owner,work->generation,Now());
    const bool accepted=work->widget_completion_ok&&success&&!expired;
    const std::string failure=expired?"Native original-widget operation timed out":work->widget_completion_error.empty()?reason:work->widget_completion_error;
    Complete(work->owner,accepted,accepted?"":failure,work->widget_completion_json);
  });
  return true;
}
bool NativeWidgetMouseCall(const std::shared_ptr<InputWork>&work,const std::string&method,Dict params,std::function<void()>done){
  if(work->message->GetString("backend")!="widget")return false;
  if(method!="Input.dispatchMouseEvent"||params->GetString("type")=="mouseWheel"){
    Complete(work->owner,false,"Original-widget backend supports raw mouse move, press and release only");return true;
  }
  if(!native_widget_mouse_||!native_widget_mouse_->available()){
    Complete(work->owner,false,"Original-widget receipts require the custom CEF backend");return true;
  }
  const auto known=operation_frames_.find(work->owner);const auto id=known==operation_frames_.end()?FrameId(work->message):known->second;
  const auto frame=frame_states_.find(id);const auto operation=operations_.Get(work->owner);
  if(frame==frame_states_.end()||!operation){Complete(work->owner,false,"Native original mouse document is unavailable");return true;}
  // NativeFrame.mouse already carries the actual frame ID. The vendor lease
  // API addresses attached frame widgets directly, so its coordinates stay in
  // that frame's viewport; it never re-hit-tests a replacement/OOPIF widget.
  const auto target=frame->second.session.empty()?sessions_.end():sessions_.find(frame->second.session);
  const auto root=frame->second.session.empty()?main_frame_id_:target==sessions_.end()?std::string():target->second;
  if(frame->second.id!=root){Complete(work->owner,false,"Original-widget backend requires an attached frame widget; this child shares its ancestor's widget");return true;}
  suitecut::NativeWidgetMouse::Pin pin{frame->second.id,frame->second.loader,frame->second.session,frame->second.context_identity,native_pristine_renderer_epoch_};
  work->busy=true;
  native_widget_mouse_->Dispatch(work->owner,pin,params,operation->deadline_ms,[this,work,done=std::move(done)](bool ok,const std::string&error){
    work->busy=false;if(work->widget_completing)return;
    if(!operations_.IsCurrent(work->owner,work->generation))return;
    if(!ok){Complete(work->owner,false,error);return;}
    if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"Native original-widget operation timed out");return;}
    done();
  });
  return true;
}

bool NativeWidgetPointerOwned()const{
  if(native_widget_mouse_&&(native_widget_mouse_->held()||native_widget_mouse_->physical()))return true;
  const auto active=input_work_.find(mouse_input_);
  return active!=input_work_.end()&&active->second->message->GetString("backend")=="widget";
}
