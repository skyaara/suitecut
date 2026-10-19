// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include "har_capture.h"

namespace suitecut {
// Independent native consumers share real Source ownership, not recording
// lifetimes. A trace stop cannot discard a public HTTP archive or its bytes.
class NativeNetworkArchives {
 public:
  using Dict=NativeHARCapture::Dict;
  using BodyFetch=NativeHARCapture::BodyFetch;
  using OwnerKind=NativeHARCapture::OwnerKind;
  using Publication=NativeHARCapture::Publication;
  using Reserve=NativeHARCapture::Reserve;
  NativeNetworkArchives(std::function<double()> now,std::function<void(const std::string&)> failure)
      :recording_([](int,Dict){},[](int,const std::string&,const NativeHARCapture::Bytes&){},now,failure),
       tracing_([](int,Dict){},[](int,const std::string&,const NativeHARCapture::Bytes&){},std::move(now),std::move(failure)){}
  void SetReservation(Reserve reserve){tracing_.SetReservation(std::move(reserve));}
  bool PageCreated(int context,const std::string&owner,const std::string&id,double wall){return !closed_&&recording_.PageCreated(context,owner,id,wall)&&tracing_.PageCreated(context,owner,id,wall);}
  bool PageTitle(int context,const std::string&owner,const std::string&title){return !closed_&&recording_.PageTitle(context,owner,title)&&tracing_.PageTitle(context,owner,title);}
  bool PageLifecycle(int context,const std::string&owner,const std::string&phase,double timestamp,double wall,const std::string&loader){return !closed_&&recording_.PageLifecycle(context,owner,phase,timestamp,wall,loader)&&tracing_.PageLifecycle(context,owner,phase,timestamp,wall,loader);}
  bool Start(int context,const std::string&filter="**"){return !closed_&&recording_.Start(context,filter);}
  void SealRecording(int context){recording_.Seal(context);}
  void Stop(int context){recording_.Stop(context);}
  Dict Export(int context)const{return recording_.Export(context);}
  bool Ready(int context)const{return recording_.Ready(context);}
  bool BeginTrace(int context,uint64_t session){
    if(closed_||!session||session>9007199254740991ULL)return false;
    const auto found=traces_.find(context);if(found!=traces_.end())return found->second.session==session&&!found->second.sealed;
    if(!tracing_.Start(context))return false;
    traces_.emplace(context,Trace{session,false});return true;
  }
  void SealTrace(int context,uint64_t session){const auto found=traces_.find(context);if(found!=traces_.end()&&found->second.session==session){found->second.sealed=true;tracing_.Seal(context);}}
  void EndTrace(int context,uint64_t session){
    const auto found=traces_.find(context);if(found==traces_.end()||found->second.session!=session)return;
    traces_.erase(found);tracing_.Stop(context);
  }
  // The journal owns published session history across chunks. Reset only this
  // consumer's completed chunk records; native generation guards and physical
  // callback counts survive Stop/Start inside NativeHARCapture.
  bool ResumeTrace(int context,uint64_t session){
    const auto found=traces_.find(context);
    if(found==traces_.end())return BeginTrace(context,session);
    if(found->second.session!=session||!found->second.sealed||tracing_.pending(context))return false;
    EndTrace(context,session);return BeginTrace(context,session);
  }
  bool Active(int context)const{return !closed_&&(recording_.Active(context)||traces_.contains(context));}
  bool Attach(int context,const std::string&owner,BodyFetch fetch,OwnerKind kind=OwnerKind::Page){
    if(closed_||owners_.contains(owner)||owners_.size()>=192||!fetch)return false;
    if(!recording_.Attach(context,owner,fetch,kind))return false;
    if(!tracing_.Attach(context,owner,std::move(fetch),kind)){recording_.Detach(owner);return false;}
    owners_.insert(owner);return true;
  }
  void Detach(const std::string&owner){owners_.erase(owner);recording_.Detach(owner);tracing_.Detach(owner);}
  void PrimaryObservation(int context,const std::string&owner,bool ready){recording_.PrimaryObservation(context,owner,ready);tracing_.PrimaryObservation(context,owner,ready);}
  void ResponseBytes(int context,const std::string&owner,const std::string&method,Dict data,const std::string&session){recording_.ResponseBytes(context,owner,method,data,session);tracing_.ResponseBytes(context,owner,method,data,session);}
  bool OriginalRequest(int context,const std::string&owner,const std::string&session,Dict raw){
    const bool recorded=recording_.OriginalRequest(context,owner,session,raw);
    const bool traced=!TraceAccepting(context)||tracing_.OriginalRequest(context,owner,session,raw);return recorded&&traced;
  }
  bool Observe(int context,const std::string&owner,Dict event,double wall){
    const bool recorded=recording_.Observe(context,owner,event,wall);
    const bool traced=!traces_.contains(context)||tracing_.Observe(context,owner,event,wall);return recorded&&traced;
  }
  NativeHARCapture::PostPublication ReservePost(int context,const std::string&owner,const std::string&id){
    if(closed_)return {};
    auto recording=recording_.ReservePost(context,owner,id),tracing=tracing_.ReservePost(context,owner,id);
    if(!recording.receipt&&!tracing.receipt)return {};
    NativeHARCapture::PostPublication publication;
    publication.data=[first=std::move(recording.data),second=std::move(tracing.data)](bool available,const NativeHARCapture::Bytes&bytes,bool text){if(first)first(available,bytes,text);if(second)second(available,bytes,text);};
    publication.receipt=[first=std::move(recording.receipt),second=std::move(tracing.receipt)]{if(first)first();if(second)second();};return publication;
  }
  // A sealed trace waits only body reservations already issued in that exact
  // trace session. Live SSE or later archive-only requests cannot prolong it.
  size_t pending(int context)const{return tracing_.pending(context);}
  size_t pending(int context,uint64_t session)const{const auto found=traces_.find(context);return found!=traces_.end()&&found->second.session==session?tracing_.pending(context):0;}
  std::set<uint64_t> RecordingBodies(const std::string&owner)const{return recording_.PendingBodies(owner);}
  bool RecordingBodies(const std::set<uint64_t>&scope)const{return recording_.PendingBodies(scope);}
  size_t recording_pending(int context)const{return recording_.pending(context);}
  size_t physical(int context)const{return recording_.physical(context)+tracing_.physical(context);}
  size_t page_metadata_bytes()const{return recording_.page_metadata_bytes()+tracing_.page_metadata_bytes();}
  size_t retained_bytes()const{return recording_.retained_bytes()+tracing_.retained_bytes();}
  bool NeedsTick()const{return recording_.NeedsTick()||tracing_.NeedsTick();}
  void Tick(double now){recording_.Tick(now);tracing_.Tick(now);}
  void Closed(int context){traces_.erase(context);recording_.Stop(context);tracing_.Stop(context);}
  void Clear(){closed_=true;traces_.clear();owners_.clear();recording_.Clear();tracing_.Clear();}
 private:
  struct Trace{uint64_t session;bool sealed;};
  bool TraceAccepting(int context)const{const auto found=traces_.find(context);return found!=traces_.end()&&!found->second.sealed;}
  NativeHARCapture recording_,tracing_;std::map<int,Trace>traces_;std::set<std::string>owners_;bool closed_=false;
};
} // namespace suitecut
