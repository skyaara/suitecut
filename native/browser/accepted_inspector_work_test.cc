// SPDX-License-Identifier: MIT
#include "accepted_inspector_work.h"
#include <cassert>
#include <iostream>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
using Work=suitecut::AcceptedInspectorWork;
struct Owner {
#include "controllers/work/accepted_inspector_owner.inc"
};
int main(){
 size_t controls=0;auto work=std::make_shared<Work>();
 const auto page=*work->Open(101,Work::Kind::Page),broker=*work->Open(102,Work::Kind::Broker),utility=*work->Open(103,Work::Kind::Utility);
 assert(page.nonce!=broker.nonce&&broker.nonce!=utility.nonce&&work->Inspect().channels==3);++controls;
 const auto a=*work->Reserve(page,1,"session");assert(!work->ReceiptLaneReady()&&work->Inspect().queued==1&&work->Inspect().accepted==0);++controls;
 assert(work->ActualReply(page,1,"session")==Work::Reply::NotDispatched&&work->Pending(page,1));++controls;
 assert(work->Dispatch(a,"session")&&work->Inspect().submitted==1&&work->Inspect().accepted==0);++controls;
 assert(!work->NeverDispatched(a)&&!work->ReceiptLaneReady()&&work->Pending(page,1));++controls;
 assert(work->ActualReply(page,1,"foreign")==Work::Reply::Foreign&&work->Pending(page,1));++controls;
 assert(work->SendReturned(a,true)&&work->Inspect().accepted==1&&work->Inspect().submitted==1&&!work->SendReturned(a,true));++controls;
 assert(work->ActualReply(page,1,"session")==Work::Reply::Observed&&work->ReceiptLaneReady()&&work->ActualReply(page,1,"session")==Work::Reply::Unknown);++controls;
 const auto b=*work->Reserve(page,2,""),c=*work->Reserve(broker,2,"");assert(work->Dispatch(b,"")&&work->Dispatch(c,"")&&work->SendReturned(b,true)&&work->SendReturned(c,true));assert(work->ActualReply(page,2,"")==Work::Reply::Observed&&work->Pending(broker,2)&&work->Inspect().submitted==1);++controls;
 assert(work->ActualChannelClosed(broker)&&work->Inspect().submitted==0&&work->ReceiptLaneReady()&&!work->ActualChannelClosed(broker));++controls;
 const auto replacement=*work->Open(102,Work::Kind::Broker);const auto d=*work->Reserve(replacement,2,"");assert(work->Dispatch(d,"")&&work->SendReturned(d,true));assert(work->ActualReply(broker,2,"")==Work::Reply::Foreign&&!work->SendReturned(c,true)&&work->Pending(replacement,2));++controls;
 assert(work->ActualReply(replacement,2,"")==Work::Reply::Observed&&work->ReceiptLaneReady());++controls;
 const auto e=*work->Reserve(utility,7,"");assert(work->NeverDispatched(e)&&!work->NeverDispatched(e)&&work->Inspect().not_dispatched==1&&work->ReceiptLaneReady());++controls;
 const auto f=*work->Reserve(utility,8,"");assert(work->Dispatch(f,"")&&work->SendReturned(f,false)&&work->Inspect().refused==1&&work->Inspect().accepted==4&&work->ReceiptLaneReady());++controls;
 Owner owner;owner.NativeInspectorOpened(work,104,Work::Kind::Page);owner.NativeInspectorReserve(1,"late");assert(owner.NativeInspectorCall(1,"late",[&]{owner.NativeInspectorReply(1,"late");assert(work->Inspect().submitted==1&&!work->ReceiptLaneReady());return true;}));assert(work->ReceiptLaneReady()&&work->Inspect().accepted==5);++controls;
 owner.NativeInspectorReserve(2,"");assert(owner.NativeInspectorCall(2,"",[&]{owner.NativeInspectorClosed(104);assert(work->Inspect().submitted==1&&!work->ReceiptLaneReady());return true;}));assert(work->ReceiptLaneReady()&&work->Inspect().accepted==6);++controls;
 auto failure=std::make_shared<Work>();Owner throwing;throwing.NativeInspectorOpened(failure,105,Work::Kind::Page);throwing.NativeInspectorReserve(1,"");bool original=false;try{throwing.NativeInspectorCall(1,"",[]{throw std::runtime_error("original native send allocation failure");return true;});}catch(const std::runtime_error& error){original=std::string(error.what())=="original native send allocation failure";}assert(original&&failure->Inspect().submitted==1&&failure->Inspect().unwound==1&&failure->Inspect().accepted==0&&failure->Inspect().poisoned);++controls;
 throwing.NativeInspectorReply(1,"");assert(failure->Inspect().submitted==0&&!failure->ReceiptLaneReady());++controls;
 auto foreign=std::make_shared<Work>();Owner wrong;wrong.NativeInspectorOpened(foreign,106,Work::Kind::Page);wrong.NativeInspectorReserve(1,"");wrong.NativeInspectorCall(1,"",[]{return true;});wrong.NativeInspectorClosed(107);assert(foreign->Inspect().submitted==1&&foreign->Inspect().poisoned);wrong.NativeInspectorClosed(106);assert(foreign->Inspect().submitted==0&&!foreign->ReceiptLaneReady());++controls;
 auto bounds=std::make_shared<Work>();const auto bounded=*bounds->Open(108,Work::Kind::Page);assert(bounds->Reserve(bounded,1,std::string(128,'x'))&&!bounds->Reserve(bounded,2,std::string(129,'x'))&&bounds->Inspect().poisoned&&bounds->Inspect().queued==1);++controls;
 auto full=std::make_shared<Work>();const auto filled=*full->Open(109,Work::Kind::Page);for(size_t i=0;i<Work::kPending;++i)assert(full->Reserve(filled,static_cast<int>(i+1),""));assert(!full->Reserve(filled,5000,"")&&full->Inspect().queued==Work::kPending&&full->Inspect().poisoned);assert(full->ActualChannelClosed(filled)&&!full->Inspect().queued&&!full->ReceiptLaneReady());++controls;
 auto channels=std::make_shared<Work>();for(size_t i=0;i<Work::kChannels;++i)assert(channels->Open(static_cast<int>(i+1),Work::Kind::Page));assert(!channels->Open(500,Work::Kind::Page)&&channels->Inspect().poisoned);++controls;
 auto repeated=std::make_shared<Work>();const auto original_channel=*repeated->Open(110,Work::Kind::Page);assert(!repeated->Open(110,Work::Kind::Utility)&&repeated->Inspect().channels==1&&repeated->ActualChannelClosed(original_channel)&&!repeated->ReceiptLaneReady());++controls;
 auto rejected=std::make_shared<Work>();Owner refusing;refusing.NativeInspectorOpened(rejected,111,Work::Kind::Utility);refusing.NativeInspectorReserve(1,"");assert(!refusing.NativeInspectorCall(1,"",[]{return false;})&&rejected->Inspect().accepted==0&&rejected->Inspect().refused==1&&rejected->ReceiptLaneReady());++controls;
 auto mismatch=std::make_shared<Work>();const auto expected=*mismatch->Open(112,Work::Kind::Page);const auto selected=*mismatch->Reserve(expected,1,"expected");assert(!mismatch->Dispatch(selected,"other")&&mismatch->Inspect().queued==1&&mismatch->Inspect().poisoned&&mismatch->NeverDispatched(selected)&&!mismatch->ReceiptLaneReady());++controls;
 auto closing=std::make_shared<Work>();Owner close_then_throw;close_then_throw.NativeInspectorOpened(closing,113,Work::Kind::Page);close_then_throw.NativeInspectorReserve(1,"");try{close_then_throw.NativeInspectorCall(1,"",[&]()->bool{close_then_throw.NativeInspectorClosed(113);assert(closing->Inspect().submitted==1);throw std::runtime_error("actual closed channel during send stack unwind");});assert(false);}catch(const std::runtime_error&){}assert(closing->Inspect().submitted==0&&closing->Inspect().unwound==1&&!closing->ReceiptLaneReady());++controls;
 auto callback_work=std::make_shared<Work>();Owner callback_owner;callback_owner.NativeInspectorOpened(callback_work,114,Work::Kind::Page);callback_owner.NativeInspectorReserve(1,"");callback_owner.NativeInspectorCall(1,"",[]{return true;});
 {const auto frame=callback_owner.NativeInspectorCallback();callback_owner.NativeInspectorReply(1,"");assert(callback_work->Inspect().submitted==0&&callback_work->Inspect().callbacks==1&&!callback_work->ReceiptLaneReady());}assert(callback_work->ReceiptLaneReady()&&callback_work->Inspect().callbacks==0);++controls;
 {const auto frame=callback_owner.NativeInspectorCallback();callback_owner.NativeInspectorClosed(114);assert(callback_work->Inspect().channels==0&&callback_work->Inspect().callbacks==1&&!callback_work->ReceiptLaneReady());}assert(callback_work->ReceiptLaneReady());++controls;
 auto callback_throw=std::make_shared<Work>();Owner callback_error;callback_error.NativeInspectorOpened(callback_throw,115,Work::Kind::Page);try{const auto frame=callback_error.NativeInspectorCallback();throw std::runtime_error("actual native callback stack exception");}catch(const std::runtime_error&){}assert(callback_throw->Inspect().callbacks==0&&callback_throw->Inspect().poisoned&&!callback_throw->ReceiptLaneReady());++controls;
 // Native-issued handler scope is independent of fixture attribution.
 auto scoped=std::make_shared<Work>();const auto scoped_channel=*scoped->Open(201,Work::Kind::Page);
 suitecut::NativeWorkOrigin first;first.locator_handler_invocation=41;auto second=first;second.locator_handler_invocation=42;
 assert(!scoped->LocatorHandlerReady(0)&&scoped->LocatorHandlerReady(41)&&scoped->ReceiptLaneReady());++controls;
 const auto scoped_first=*scoped->Reserve(scoped_channel,1,"",first);
 const auto scoped_other=*scoped->Reserve(scoped_channel,2,"",second);
 const auto untagged=*scoped->Reserve(scoped_channel,3,"");
 assert(!scoped->LocatorHandlerReady(41)&&!scoped->LocatorHandlerReady(42)&&scoped->LocatorHandlerReady(43)&&!scoped->ReceiptLaneReady());++controls;
 assert(scoped->Dispatch(scoped_first,"")&&scoped->ReplyOrigin(scoped_channel,1,"")==first);
 assert(scoped->ActualReply(scoped_channel,1,"")==Work::Reply::Observed&&!scoped->LocatorHandlerReady(41));
 assert(scoped->SendReturned(scoped_first,true)&&scoped->LocatorHandlerReady(41)&&!scoped->LocatorHandlerReady(42)&&!scoped->ReceiptLaneReady());++controls;
 const auto callback=scoped->BeginCallback(scoped_channel);assert(callback&&!scoped->LocatorHandlerReady(41)&&!scoped->LocatorHandlerReady(43));
 assert(scoped->CallbackReturned(callback)&&scoped->LocatorHandlerReady(41));++controls;
 assert(scoped->NeverDispatched(scoped_other)&&scoped->LocatorHandlerReady(42)&&scoped->NeverDispatched(untagged)&&scoped->ReceiptLaneReady());++controls;
 const auto refusal=*scoped->Reserve(scoped_channel,4,"",first);assert(scoped->Dispatch(refusal,"")&&!scoped->LocatorHandlerReady(41));
 assert(scoped->SendReturned(refusal,false)&&scoped->LocatorHandlerReady(41));++controls;
 const auto close_during_send=*scoped->Reserve(scoped_channel,5,"",first);assert(scoped->Dispatch(close_during_send,""));
 const auto close_callback=scoped->BeginCallback(scoped_channel);assert(close_callback&&scoped->ActualChannelClosed(scoped_channel)&&!scoped->LocatorHandlerReady(41));
 assert(scoped->CallbackReturned(close_callback)&&!scoped->LocatorHandlerReady(41));
 assert(scoped->SendReturned(close_during_send,true)&&scoped->LocatorHandlerReady(41));++controls;
 auto scoped_owner=std::make_shared<Work>();Owner native_scoped;native_scoped.NativeInspectorOpened(scoped_owner,202,Work::Kind::Page);
 {const suitecut::NativeWorkOriginFrame frame(first);native_scoped.NativeInspectorReserve(1,"");}
 assert(!scoped_owner->LocatorHandlerReady(41));
 assert(native_scoped.NativeInspectorCall(1,"",[]{return true;}));
 assert(scoped_owner->ReplyOrigin(*native_scoped.inspector_channel_,1,"")==first);
 native_scoped.NativeInspectorReply(1,"");assert(scoped_owner->LocatorHandlerReady(41));++controls;
 scoped->Unsupported();assert(!scoped->LocatorHandlerReady(41)&&!scoped->LocatorHandlerReady(43)&&!scoped->ReceiptLaneReady());++controls;
 assert(controls==38);std::cout<<"Accepted Inspector work: "<<controls<<" controlled policy/owner-reentrancy cases passed; no actual CEF/backend/Engine-idle proof\n";
}
