// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <iostream>
#include "include/cef_api_hash.h"
#include "coverage_dispatch.h"
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));using Dispatch=suitecut::CoverageDispatch;using Dict=Dispatch::Dict;double now=100;struct Call{std::string session,method;Dict params;double deadline;Dispatch::Reply reply;};std::deque<Call> actual;size_t positives=0,negatives=0;
 Dispatch::Hooks hooks;hooks.now=[&]{return now;};hooks.wake=[]{};hooks.send=[&](const std::string& session,const std::string& method,Dict params,double deadline,auto reply){actual.push_back({session,method,params?params->Copy(false):nullptr,deadline,std::move(reply)});return true;};auto dispatch=std::make_shared<Dispatch>(hooks);
 auto reply=[&](bool ok,Dict,const std::string&){if(ok)++positives;else ++negatives;};auto params=CefDictionaryValue::Create();params->SetString("scriptId","actual-native-script");
 for(size_t i=0;i<Dispatch::kBackend+Dispatch::kQueued;++i){assert(dispatch->Send("actual-session","Debugger.getScriptSource",params,110,reply));}assert(actual.size()==16&&dispatch->Physical()==16&&dispatch->Pending()==272);assert(!dispatch->Send("actual-session","Debugger.getScriptSource",params,110,reply));params->SetString("scriptId","mutated author object");assert(actual.front().params->GetString("scriptId")=="actual-native-script");
 // Cancellation retires only genuinely undispatched commands. It cannot turn
 // real backend calls into physical0, nor dispatch more canceled queued work.
 dispatch->CancelQueued("startJS","actual native operation canceled");assert(negatives==256&&dispatch->Pending()==16&&dispatch->Physical()==16&&actual.size()==16);
 now=111;while(!actual.empty()){auto call=std::move(actual.front());actual.pop_front();assert(call.deadline==110);call.reply(true,CefDictionaryValue::Create(),"");}assert(positives==16&&dispatch->Pending()==0&&dispatch->Physical()==0&&dispatch->Bytes()==0);
 // A late actual enabled-domain ACK remains true for the owning controller to
 // record/disable that actual domain. This dispatcher invents no logical ACKs.
 assert(dispatch->Send("actual-session","DOM.enable",nullptr,120,reply));now=121;auto late=std::move(actual.front());actual.pop_front();late.reply(true,CefDictionaryValue::Create(),"");assert(positives==17);
 now=100;for(size_t i=0;i<17;++i)assert(dispatch->Send("actual-session","Profiler.enable",nullptr,110,reply));assert(actual.size()==16&&dispatch->Pending()==17);now=111;dispatch->Tick(now);assert(actual.size()==16&&dispatch->Pending()==16&&negatives==257);while(!actual.empty()){auto call=std::move(actual.front());actual.pop_front();call.reply(true,CefDictionaryValue::Create(),"");}assert(dispatch->Pending()==0&&dispatch->Bytes()==0);
 // Repeated queued admission/cancellation cannot leave an unbounded tombstone
 // queue or accidentally reject the next genuinely available reservation.
 now=100;for(size_t i=0;i<16;++i)assert(dispatch->Send("held-session","Debugger.enable",nullptr,500,reply));for(int cycle=0;cycle<100;++cycle){for(size_t i=0;i<256;++i)assert(dispatch->Send("queued-session","Debugger.getScriptSource",nullptr,500,reply));dispatch->CancelQueued("startJS","canceled queued work");assert(dispatch->Pending()==16&&actual.size()==16);}
 dispatch->Detached("held-session");assert(dispatch->Pending()==0&&dispatch->Physical()==0&&dispatch->Bytes()==0);while(!actual.empty()){auto call=std::move(actual.front());actual.pop_front();call.reply(true,CefDictionaryValue::Create(),"");}assert(dispatch->Pending()==0);
 // Transaction cancellation cannot cancel another actual target's queued
 // domain teardown. That accepted cleanup must reach its actual backend ACK.
 now=100;for(int i=0;i<16;++i){assert(dispatch->Send("held-before-cleanup","Debugger.getScriptSource",nullptr,500,reply));}assert(dispatch->Send("cleanup-session","Profiler.disable",nullptr,500,reply));dispatch->CancelQueued("startJS","logical transaction canceled");assert(dispatch->Pending()==17&&actual.size()==16);bool cleanup_seen=false;while(!actual.empty()){auto call=std::move(actual.front());actual.pop_front();cleanup_seen=cleanup_seen||call.method=="Profiler.disable";call.reply(true,CefDictionaryValue::Create(),"");}assert(cleanup_seen&&dispatch->Pending()==0&&dispatch->Physical()==0);
 // Closing is an actual provider-supplied channel-retirement boundary.
 for(int i=0;i<17;++i){assert(dispatch->Send("closing-session","CSS.enable",nullptr,500,reply));}dispatch->Closed();assert(dispatch->Pending()==0&&dispatch->Physical()==0&&dispatch->Bytes()==0);while(!actual.empty()){auto call=std::move(actual.front());actual.pop_front();call.reply(true,CefDictionaryValue::Create(),"");}assert(!dispatch->Send("closed-session","CSS.enable",nullptr,500,reply));
 // Actual synchronous replies are valid; native queue pumping stays bounded.
 hooks.send=[](const auto&,const auto&,Dict,double,auto done){done(true,CefDictionaryValue::Create(),"");return true;};auto sync=std::make_shared<Dispatch>(hooks);assert(sync->Send("actual-sync-session","Profiler.enable",nullptr,500,reply));assert(sync->Physical()==0&&sync->Pending()==0&&sync->Bytes()==0);
 std::cout<<"Native coverage queue bounds, ownership copies, original deadlines, cancellation, real retirement and synchronous ACKs passed\n";
}
