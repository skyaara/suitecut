// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
// Genuine CDP socket identities and payload bytes. This observer never changes
// browser sockets; routing requires a separate native transport owner.
class NativeWebSocketControls {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Bytes=std::vector<uint8_t>;
  using Emit=std::function<bool(Dict,const Bytes&)>;
  using Observe=std::function<void(Dict,const Bytes&,double)>;
  void SetObserver(Observe observe){observe_=std::move(observe);}
  using Reply=std::function<void(bool,Dict,const Bytes&,const std::string&)>;
  NativeWebSocketControls(Emit emit,std::function<void()> wake,std::function<void(const std::string&)> failure)
      :emit_(std::move(emit)),wake_(std::move(wake)),failure_(std::move(failure)){}
  ~NativeWebSocketControls(){Clear();}
  void Event(const std::string& method,Dict params,const std::string& session,double now) {
    if(disposed_||!params||!method.starts_with("Network.webSocket"))return;
    const auto request=params->GetString("requestId").ToString();
    if(request.empty()||request.size()>128||session.size()>128){Fail("Invalid native WebSocket identity");return;}
    const auto key=session+"\n"+request;
    if(method=="Network.webSocketCreated") {
      if(sockets_.contains(key))return;
      const auto url=params->GetString("url").ToString();
      if(sockets_.size()>=128||global_sockets_>=512||url.size()>8192||!(url.starts_with("ws://")||url.starts_with("wss://"))){Fail("Native WebSocket identity capacity exceeded");return;}
      Socket socket{std::to_string(++serial_),session,request,url};sockets_.emplace(key,std::move(socket));++global_sockets_;
      Publish(Base(sockets_.at(key),"created"),{},now);return;
    }
    auto found=sockets_.find(key);if(found==sockets_.end())return;
    auto& socket=found->second;
    if(method=="Network.webSocketFrameSent"||method=="Network.webSocketFrameReceived") {
      auto frame=params->GetDictionary("response");if(!frame){Fail("Invalid native WebSocket frame");return;}
      const int opcode=frame->GetInt("opcode");
      // Browser-control frames are observed separately from authored messages.
      if(opcode!=1&&opcode!=2)return;
      if(frame->GetType("payloadData")!=VTYPE_STRING){Fail("Invalid native WebSocket payload");return;}
      const auto rendered=frame->GetString("payloadData").ToString();Bytes bytes;
      if(opcode==1){if(rendered.size()>2097152){Fail("Native WebSocket frame exceeds 2 MiB");return;}bytes.assign(rendered.begin(),rendered.end());}
      else {
        if(rendered.size()>2796204){Fail("Native WebSocket frame exceeds 2 MiB");return;}
        if(!rendered.empty()){
          auto decoded=CefBase64Decode(rendered);
          if(!decoded||decoded->GetSize()>2097152){Fail("Invalid native WebSocket binary frame");return;}
          bytes.resize(decoded->GetSize());decoded->GetData(bytes.data(),bytes.size(),0);
        }
      }
      auto event=Base(socket,method=="Network.webSocketFrameSent"?"framesent":"framereceived");event->SetInt("opcode",opcode);Publish(event,std::move(bytes),now);return;
    }
    if(method=="Network.webSocketFrameError") {
      auto event=Base(socket,"socketerror");auto error=params->GetString("errorMessage").ToString();
      if(error.size()>8192){Fail("Native WebSocket error exceeds limit");return;}
      event->SetString("error",error);Publish(event,{},now);return;
    }
    if(method=="Network.webSocketClosed") {auto event=Base(socket,"close");sockets_.erase(found);--global_sockets_;Publish(event,{},now);}
  }
  bool Wait(int owner,const std::string& socket,const std::string& event,double after,double deadline,Reply reply) {
    if(disposed_||owner<1||(socket.empty()&&event!="created")||socket.size()>20||!Allowed(event)||!std::isfinite(after)||after<0||std::floor(after)!=after||after>double(sequence_)||!std::isfinite(deadline)||deadline<0||!reply||waiters_.size()>=128||waiters_.contains(owner))return false;
    if(!socket.empty()&&!Issued(socket))return false;
    if(!journal_.empty()&&after<journal_.front()->event->GetDouble("sequence")-1){reply(false,nullptr,{},"Native WebSocket journal capacity exceeded");return true;}
    Waiter waiter{socket,event,after,deadline,std::move(reply)};
    for(const auto& record:journal_)if(record->delivered){
      if(Matches(waiter,record->event)){waiter.reply(true,record->event,record->bytes,"");return true;}
      if(Closed(waiter,record->event)){waiter.reply(false,nullptr,{},"WebSocket closed");return true;}
    }
    // Issued IDs never repeat. Absence from the live table proves actual close;
    // a queued close still owns Writer ordering and must be delivered first.
    if(event!="close"&&!socket.empty()&&!Live(socket)&&!PendingClose(socket)){
      waiter.reply(false,nullptr,{},"WebSocket closed");return true;
    }
    waiters_.emplace(owner,std::move(waiter));return true;
  }
  void Cancel(int owner){waiters_.erase(owner);}
  void SessionDetached(const std::string& session,double now){
    std::vector<std::string> closed;for(const auto& [key,socket]:sockets_)if(socket.session==session)closed.push_back(key);
    for(const auto& key:closed){auto event=Base(sockets_.at(key),"close");sockets_.erase(key);--global_sockets_;Publish(event,{},now);}
  }
  void Tick(double now) {
    if(disposed_)return;
    std::vector<Reply> expired;
    for(auto it=waiters_.begin();it!=waiters_.end();)if(it->second.deadline>0&&now>=it->second.deadline){expired.push_back(std::move(it->second.reply));it=waiters_.erase(it);}else ++it;
    for(auto& reply:expired)reply(false,nullptr,{},"Native WebSocket event deadline exceeded");
    size_t burst=0;
    while(!pending_.empty()&&burst++<16){
      const auto record=pending_.front();
      if(now>=record->deadline){Fail("Native WebSocket output deadline exceeded");return;}
      if(!emit_(record->event,record->bytes))break;
      pending_bytes_-=record->cost;pending_.pop_front();record->delivered=true;
      // The real event is now ahead of these replies in the owner Writer FIFO.
      // Backpressure retains wait ownership and its original native deadline.
      std::vector<std::pair<Reply,bool>> done;
      for(auto it=waiters_.begin();it!=waiters_.end();){
        const bool matches=Matches(it->second,record->event);
        if(matches||Closed(it->second,record->event)){done.emplace_back(std::move(it->second.reply),matches);it=waiters_.erase(it);}else ++it;
      }
      for(auto& [reply,matches]:done)if(matches)reply(true,record->event,record->bytes,"");else reply(false,nullptr,{},"WebSocket closed");
    }
    if(!pending_.empty())wake_();
  }
  void Clear(){disposed_=true;global_sockets_-=sockets_.size();sockets_.clear();pending_.clear();journal_.clear();waiters_.clear();pending_bytes_=journal_bytes_=0;}
  uint64_t sequence()const{return sequence_;}
  size_t sockets()const{return sockets_.size();}
  size_t pending()const{return pending_.size();}
  size_t retained_bytes()const{return pending_bytes_+journal_bytes_;}
 private:
  struct Socket{std::string id,session,request,url;};
  struct Record{
    Dict event;Bytes bytes;size_t cost=0;double deadline=0;bool delivered=false;
    ~Record(){global_retained_bytes_-=cost;--global_records_;}
  };
  struct Waiter{std::string socket,event;double after,deadline;Reply reply;};
  static bool Allowed(const std::string& event){return event=="created"||event=="framesent"||event=="framereceived"||event=="socketerror"||event=="close";}
  static bool Matches(const Waiter& wait,Dict event){return (wait.socket.empty()||event->GetString("socketId")==wait.socket)&&event->GetString("event")==wait.event&&event->GetDouble("sequence")>wait.after;}
  static bool Closed(const Waiter& wait,Dict event){return !wait.socket.empty()&&wait.event!="close"&&event->GetString("event")=="close"&&event->GetString("socketId")==wait.socket;}
  bool Issued(const std::string& socket)const{uint64_t id=0;const auto result=std::from_chars(socket.data(),socket.data()+socket.size(),id);return result.ec==std::errc{}&&result.ptr==socket.data()+socket.size()&&id&&id<=serial_&&std::to_string(id)==socket;}
  bool Live(const std::string& socket)const{return std::any_of(sockets_.begin(),sockets_.end(),[&](const auto& entry){return entry.second.id==socket;});}
  bool PendingClose(const std::string& socket)const{return std::any_of(pending_.begin(),pending_.end(),[&](const auto& record){return record->event->GetString("event")=="close"&&record->event->GetString("socketId")==socket;});}
  Dict Base(const Socket& socket,const std::string& kind){auto event=CefDictionaryValue::Create();event->SetString("socketId",socket.id);event->SetString("sessionId",socket.session);event->SetString("requestId",socket.request);event->SetString("url",socket.url);event->SetString("event",kind);return event;}
  void Fail(const std::string& error){Clear();failure_(error);}
  void Publish(Dict event,Bytes bytes,double now){
    if(disposed_)return;
    if(sequence_>=9007199254740991ULL){Fail("Native WebSocket sequence exhausted");return;}
    event->SetDouble("sequence",double(++sequence_));auto wrapper=CefValue::Create();wrapper->SetDictionary(event->Copy(false));const auto cost=bytes.size()+CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).length();
    if(pending_.size()>=128||cost>16777216-pending_bytes_){Fail("Native WebSocket output ownership capacity exceeded");return;}
    while(!journal_.empty()&&(cost>33554432-global_retained_bytes_||global_records_>=1024)){journal_bytes_-=journal_.front()->cost;journal_.pop_front();}
    if(cost>33554432-global_retained_bytes_||global_records_>=1024){Fail("Native WebSocket global retained-byte capacity exceeded");return;}
    auto record=std::make_shared<Record>();record->event=event;record->bytes=std::move(bytes);record->cost=cost;record->deadline=now+30000;global_retained_bytes_+=cost;++global_records_;
    while(!journal_.empty()&&(journal_.size()>=512||cost>4194304-journal_bytes_)){journal_bytes_-=journal_.front()->cost;journal_.pop_front();}
    if(cost<=4194304){journal_.push_back(record);journal_bytes_+=cost;}
    pending_.push_back(record);pending_bytes_+=cost;
    if(observe_)observe_(record->event->Copy(false),record->bytes,now);
    Tick(now);
  }
  inline static size_t global_sockets_=0,global_retained_bytes_=0,global_records_=0;
  Observe observe_;Emit emit_;std::function<void()> wake_;std::function<void(const std::string&)> failure_;
  bool disposed_=false;uint64_t serial_=0,sequence_=0;size_t pending_bytes_=0,journal_bytes_=0;
  std::map<std::string,Socket>sockets_;std::deque<std::shared_ptr<Record>>pending_,journal_;std::map<int,Waiter>waiters_;
};
}
