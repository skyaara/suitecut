// SPDX-License-Identifier: MIT
// Ordinary native IPC only. This cache is NOT used by author bootstrap yet.
void SnapshotFlush(CefRefPtr<CefFrame>frame){
  for(const auto&pin:snapshot_receiver_.TakeReleased()){
    const auto owner=browsers_.find(static_cast<int>(pin.owner.browser));
    auto route=owner!=browsers_.end()?owner->second.snapshot_route:CefRefPtr<CefFrame>{};
    if(!route&&frame&&frame->IsValid()&&static_cast<uint64_t>(frame->GetBrowser()->GetIdentifier())==pin.owner.browser)route=frame;
    // CEF explicitly does not guarantee delivery during navigation/close. The
    // browser keeps its physical receipt until this arrives or real death.
    if(route&&route->IsValid())route->SendProcessMessage(PID_BROWSER,suitecut::InitSnapshotControl::Receipt(suitecut::InitSnapshotControl::kReleased,pin));
  }
}
void SnapshotRetire(Browser&owner,CefRefPtr<CefFrame>frame){
  if(owner.snapshot_epoch){
    suitecut::InitSnapshotOwner actual{static_cast<uint64_t>(owner.context),static_cast<uint64_t>(frame?frame->GetBrowser()->GetIdentifier():0),owner.snapshot_epoch,owner.snapshot_frame_id};
    snapshot_receiver_.Retire(actual);SnapshotFlush(frame);
  }
  owner.snapshot_epoch=0;owner.snapshot_context=nullptr;owner.snapshot_frame_id.clear();
}
void SnapshotContextCreated(CefRefPtr<CefBrowser>browser,CefRefPtr<CefFrame>frame,CefRefPtr<CefV8Context>context){
  if(!frame->IsMain())return;
  auto found=browsers_.find(browser->GetIdentifier());if(found==browsers_.end())return;auto&owner=found->second;
  owner.snapshot_context=context;owner.snapshot_route=frame;
  if(owner.snapshot_epoch){
    frame->SendProcessMessage(PID_BROWSER,suitecut::InitSnapshotControl::Admission(suitecut::InitSnapshotControl::kReady,owner.context,owner.snapshot_epoch,owner.snapshot_request,owner.snapshot_frame_id));return;
  }
  if(owner.snapshot_request||snapshot_next_request_==std::numeric_limits<uint64_t>::max())return;
  owner.snapshot_request=++snapshot_next_request_;owner.snapshot_frame_id=frame->GetIdentifier().ToString();
  frame->SendProcessMessage(PID_BROWSER,suitecut::InitSnapshotControl::Admission(suitecut::InitSnapshotControl::kRequest,owner.context,1,owner.snapshot_request,owner.snapshot_frame_id));
}
void SnapshotContextReleased(CefRefPtr<CefBrowser>browser,CefRefPtr<CefFrame>,CefRefPtr<CefV8Context>context){
  auto found=browsers_.find(browser->GetIdentifier());if(found==browsers_.end())return;auto&owner=found->second;
  // Document lifetime is not renderer/browser lifetime. Cache and mapping
  // receipts survive navigation; only this exact V8 reference is retired.
  if(owner.snapshot_context&&owner.snapshot_context->IsSame(context))owner.snapshot_context=nullptr;
}
bool SnapshotMessage(CefRefPtr<CefBrowser>browser,CefRefPtr<CefFrame>frame,CefProcessId sender,CefRefPtr<CefProcessMessage>message){
  if(sender!=PID_BROWSER||!message)return false;
  const auto name=message->GetName();
  if(name!=suitecut::InitSnapshotControl::kAdmission&&name!=std::string(suitecut::InitSnapshotWire::kMessage))return false;
  auto found=browsers_.find(browser->GetIdentifier());if(found==browsers_.end()||!frame||!frame->IsMain())return true;
  auto&owner=found->second;owner.snapshot_route=frame;
  if(name==suitecut::InitSnapshotControl::kAdmission){
    int context=0;uint64_t epoch=0,request=0;std::string channel;
    if(!suitecut::InitSnapshotControl::Admission(message,context,epoch,request,channel)||context!=owner.context||request!=owner.snapshot_request||channel!=owner.snapshot_frame_id||(owner.snapshot_epoch&&epoch<owner.snapshot_epoch))return true;
    if(owner.snapshot_epoch&&owner.snapshot_epoch!=epoch){
      const auto saved=owner.snapshot_context;const auto id=owner.snapshot_frame_id;SnapshotRetire(owner,frame);owner.snapshot_context=saved;owner.snapshot_frame_id=id;
    }
    suitecut::InitSnapshotOwner pin{static_cast<uint64_t>(context),static_cast<uint64_t>(browser->GetIdentifier()),epoch,owner.snapshot_frame_id};
    if(!snapshot_receiver_.Admit(pin))return true;
    owner.snapshot_epoch=epoch;frame->SendProcessMessage(PID_BROWSER,suitecut::InitSnapshotControl::Admission(suitecut::InitSnapshotControl::kReady,context,epoch,request,owner.snapshot_frame_id));return true;
  }
  if(!owner.snapshot_epoch)return true;
  const auto region=message->GetSharedMemoryRegion();
  if(!region||!region->IsValid()||!region->Memory()||region->Size()>suitecut::InitSnapshotWire::kHeader+suitecut::InitSnapshotWire::kFrame+suitecut::InitSnapshotWire::kPayload)return true;
  auto view=suitecut::InitSnapshotWire::Decode({static_cast<const uint8_t*>(region->Memory()),region->Size()});
  const suitecut::InitSnapshotOwner expected{static_cast<uint64_t>(owner.context),static_cast<uint64_t>(browser->GetIdentifier()),owner.snapshot_epoch,owner.snapshot_frame_id};
  if(!view||view->pin.owner!=expected)return true;
  // Browser checks the original command clock at the actual receipt. Renderer
  // clocks do not silently replace that budget. No author code runs here.
  const auto result=suitecut::InitSnapshotMessage::Install(snapshot_receiver_,message,view->pin,0,0);
  const bool installed=result.status==suitecut::InitSnapshotReceiver::Status::Installed||result.status==suitecut::InitSnapshotReceiver::Status::Duplicate;
  frame->SendProcessMessage(PID_BROWSER,suitecut::InitSnapshotControl::Receipt(suitecut::InitSnapshotControl::kAck,view->pin,installed));SnapshotFlush(frame);return true;
}
