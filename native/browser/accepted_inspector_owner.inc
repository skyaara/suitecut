// SPDX-License-Identifier: MIT
// Included privately in each actual page, broker and utility CEF client.
std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work_;
std::optional<suitecut::AcceptedInspectorWork::Channel> inspector_channel_;
void NativeInspectorOpened(std::shared_ptr<suitecut::AcceptedInspectorWork> ledger,int actual_browser,suitecut::AcceptedInspectorWork::Kind kind){
  if(inspector_channel_){if(inspector_work_)inspector_work_->Unsupported();return;}
  inspector_work_=std::move(ledger);if(inspector_work_)inspector_channel_=inspector_work_->Open(actual_browser,kind);
}
void NativeInspectorReserve(int command,std::string_view session){if(inspector_work_&&inspector_channel_)(void)inspector_work_->Reserve(*inspector_channel_,command,session);}
void NativeInspectorNeverDispatched(int command){
  if(!inspector_work_||!inspector_channel_)return;
  if(const auto ticket=inspector_work_->Pending(*inspector_channel_,command))if(!inspector_work_->NeverDispatched(*ticket))inspector_work_->Unsupported();
}
template<class Dispatch>bool NativeInspectorCall(int command,std::string_view session,Dispatch&& actual){
  // Snapshot the native lease before a reentrant CEF send can close/reset its
  // owner fields. The caller also retains its actual CefBrowser locally.
  const auto ledger=inspector_work_;const auto channel=inspector_channel_;
  const auto ticket=ledger&&channel?ledger->Pending(*channel,command):std::nullopt;
  if(ticket&&!ledger->Dispatch(*ticket,session))ledger->Unsupported();
  bool accepted=false;
  try{accepted=actual();}catch(...){if(ticket&&!ledger->SendUnwound(*ticket))ledger->Unsupported();throw;}
  if(ticket&&!ledger->SendReturned(*ticket,accepted))ledger->Unsupported();
  return accepted;
}
struct NativeInspectorCallbackFrame {
  std::shared_ptr<suitecut::AcceptedInspectorWork> ledger;uint64_t receipt=0;int unwinding=0;
  NativeInspectorCallbackFrame(std::shared_ptr<suitecut::AcceptedInspectorWork> actual,const std::optional<suitecut::AcceptedInspectorWork::Channel>& channel):ledger(std::move(actual)),unwinding(std::uncaught_exceptions()){
    if(ledger){if(channel)receipt=ledger->BeginCallback(*channel);else ledger->Unsupported();}
  }
  NativeInspectorCallbackFrame(const NativeInspectorCallbackFrame&)=delete;NativeInspectorCallbackFrame& operator=(const NativeInspectorCallbackFrame&)=delete;
  ~NativeInspectorCallbackFrame(){if(ledger){if(std::uncaught_exceptions()>unwinding)ledger->Unsupported();if(receipt&&!ledger->CallbackReturned(receipt))ledger->Unsupported();}}
};
NativeInspectorCallbackFrame NativeInspectorCallback(){return NativeInspectorCallbackFrame(inspector_work_,inspector_channel_);}
suitecut::NativeWorkOrigin NativeInspectorReplyOrigin(int command,std::string_view session)const noexcept{
  return inspector_work_&&inspector_channel_?inspector_work_->ReplyOrigin(*inspector_channel_,command,session):suitecut::NativeWorkOrigin{};
}
void NativeInspectorReply(int command,std::string_view session){if(inspector_work_&&inspector_channel_)(void)inspector_work_->ActualReply(*inspector_channel_,command,session);}
void NativeInspectorClosed(int actual_browser){
  if(!inspector_work_||!inspector_channel_)return;
  if(inspector_channel_->browser!=actual_browser||!inspector_work_->ActualChannelClosed(*inspector_channel_)){inspector_work_->Unsupported();return;}
  inspector_channel_.reset();
}
