// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
// Optional byte retention for genuinely streamed image resources. This never
// enables Network durable messages and never retains Document resources.
// Physical command receipts live independently of bounded cache entries.
class NativeResponseStreams {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Bytes=std::vector<uint8_t>;
  using Result=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,const std::string&,double,Result)>;
  NativeResponseStreams(Send send,std::function<double()> now,std::function<void()> wake)
      :send_(std::move(send)),now_(std::move(now)),wake_(std::move(wake)){}
  ~NativeResponseStreams(){Close();}
  void Event(const std::string&method,Dict actual,const std::string&session){
    if(closed_||!actual)return;
    const auto request=actual->GetString("requestId").ToString();
    if(request.empty()||request.size()>256||session.size()>128)return;
    if(method=="Network.requestWillBeSent"){
      Discard(session,request);
      // Chromium's streaming bit spans a physical redirect ID. Re-enabling on
      // a later hop could duplicate already-streamed bytes in bufferedData.
      // Retain no redirected image body until that behavior is independently
      // joined to genuine byte offsets; the primary body API stays untouched.
      if(!actual->HasKey("redirectResponse"))Start(session,request,actual->GetString("type"),now_()+30000);
    }else if(method=="Network.dataReceived")Data(session,request,actual);
    else if(method=="Network.loadingFinished")Finished(session,request,true);
    else if(method=="Network.loadingFailed")Finished(session,request,false);
  }
  bool Start(const std::string&session,const std::string&request,const std::string&type,double deadline){
    if(closed_||type!="Image"||session.size()>128||request.empty()||request.size()>256||!std::isfinite(deadline)||deadline<=now_())return false;
    Tick(now_());
    const auto key=session+"\n"+request;
    if(entries_.contains(key))return true;
    if(physical_.size()>=64||global_physical_>=128||entries_.size()>=128||global_entries_>=256||serial_>=9007199254740991ULL)return false;
    const auto token=++serial_;Entry entry;entry.token=token;entry.deadline=deadline;
    entries_.emplace(key,std::move(entry));++global_entries_;physical_.emplace(token,PhysicalState{key,session,deadline});++global_physical_;
    const auto alive=alive_;auto send=send_;
    send(session,request,deadline,[this,alive,token](bool ok,const std::string&json){
      if(!*alive)return;
      const auto found=physical_.find(token);if(found==physical_.end())return;
      const auto key=found->second.key;const auto deadline=found->second.deadline;
      physical_.erase(found);--global_physical_;
      auto entry=entries_.find(key);if(entry==entries_.end()||entry->second.token!=token)return;
      if(!ok||now_()>=deadline||json.size()>2796300){Erase(key);return;}
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto value=parsed?parsed->GetDictionary():nullptr;
      if(!value||value->GetType("bufferedData")!=VTYPE_STRING){Erase(key);return;}
      auto prefix=Decode(value->GetString("bufferedData").ToString());
      if(!prefix||prefix->size()>kBody-entry->second.suffix.size()||!Admit(prefix->capacity())){Erase(key);return;}
      entry->second.prefix=std::move(*prefix);entry->second.acknowledged=true;Charge(entry->second.prefix.capacity());
    });
    if(*alive&&!physical_.empty())wake_();
    return true;
  }
  void Data(const std::string&session,const std::string&request,Dict actual){
    if(closed_||!actual)return;
    const auto key=session+"\n"+request;auto found=entries_.find(key);if(found==entries_.end()||found->second.finished)return;
    if(now_()>=found->second.deadline){Erase(key);return;}
    const auto type=actual->GetType("dataLength");
    if(type!=VTYPE_INT&&type!=VTYPE_DOUBLE){Erase(key);return;}
    const double length=type==VTYPE_INT?actual->GetInt("dataLength"):actual->GetDouble("dataLength");
    if(!std::isfinite(length)||length<0||length>kBody||std::floor(length)!=length){Erase(key);return;}
    if(actual->GetType("data")!=VTYPE_STRING){if(length>0)Erase(key);return;}
    auto chunk=Decode(actual->GetString("data").ToString());if(!chunk||chunk->size()!=static_cast<size_t>(length)){Erase(key);return;}
    auto&entry=found->second;
    if(chunk->size()>kBody-entry.prefix.size()-entry.suffix.size()){Erase(key);return;}
    const auto required=entry.suffix.size()+chunk->size();
    if(required>entry.suffix.capacity()){
      const auto capacity=std::min(kBody,std::max(required,std::max(size_t(1024),entry.suffix.capacity()*2)));
      const auto added=capacity-entry.suffix.capacity();if(!Admit(added)){Erase(key);return;}
      Bytes grown;grown.reserve(capacity);
      // Account allocator-reported capacity before attaching the replacement;
      // rejection cannot refund an allocation that was never charged.
      const auto actual_added=grown.capacity()-entry.suffix.capacity();
      if(!Admit(actual_added)){Erase(key);return;}
      grown.insert(grown.end(),entry.suffix.begin(),entry.suffix.end());
      entry.suffix.swap(grown);Charge(actual_added);
    }
    entry.suffix.insert(entry.suffix.end(),chunk->begin(),chunk->end());
  }
  void Finished(const std::string&session,const std::string&request,bool success){
    const auto key=session+"\n"+request;const auto found=entries_.find(key);if(found==entries_.end())return;
    if(!success||now_()>=found->second.deadline){Erase(key);return;}found->second.finished=true;
  }
  std::optional<Bytes> Body(const std::string&session,const std::string&request)const{
    const auto found=entries_.find(session+"\n"+request);
    if(found==entries_.end()||!found->second.finished||!found->second.acknowledged||now_()>=found->second.deadline)return std::nullopt;
    const auto&entry=found->second;Bytes body;body.reserve(entry.prefix.size()+entry.suffix.size());
    body.insert(body.end(),entry.prefix.begin(),entry.prefix.end());body.insert(body.end(),entry.suffix.begin(),entry.suffix.end());return body;
  }
  std::pair<bool,std::string> Resolve(const std::string&session,const std::string&request,bool actual_ok,const std::string&actual_json)const{
    if(actual_ok){
      const auto parsed=CefParseJSON(actual_json,JSON_PARSER_RFC);const auto actual=parsed?parsed->GetDictionary():nullptr;
      // Preserve malformed metadata and nonempty genuine primary bodies for
      // the existing response reader to validate; this is no replacement API.
      if(!actual||actual->GetType("body")!=VTYPE_STRING||actual->GetType("base64Encoded")!=VTYPE_BOOL||!actual->GetString("body").empty())return {actual_ok,actual_json};
    }
    const auto bytes=Body(session,request);if(!bytes)return {actual_ok,actual_json};
    auto value=CefDictionaryValue::Create();value->SetBool("base64Encoded",true);
    value->SetString("body",bytes->empty()?std::string():CefBase64Encode(bytes->data(),bytes->size()).ToString());
    auto wrapper=CefValue::Create();wrapper->SetDictionary(value);return {true,CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString()};
  }
  // Called only from the existing exact-ID native session handoff.
  bool Move(const std::string&old_session,const std::string&new_session,const std::string&request){
    if(new_session.size()>128)return false;
    const auto old=old_session+"\n"+request,next=new_session+"\n"+request;
    if(old==next||!entries_.contains(old))return true;
    if(entries_.contains(next))return false;
    auto entry=entries_.extract(old);const auto token=entry.mapped().token;std::string key(next);entry.key().swap(key);entries_.insert(std::move(entry));
    const auto physical=physical_.find(token);if(physical!=physical_.end()){std::string key(next);physical->second.key.swap(key);}return true;
  }
  void Discard(const std::string&session,const std::string&request){Erase(session+"\n"+request);}
  void SessionClosed(const std::string&session){
    std::vector<std::string>keys;for(const auto&[key,entry]:entries_)if(key.starts_with(session+"\n"))keys.push_back(key);
    for(const auto&key:keys)Erase(key);
    // This is an actual target channel close, so outstanding physical replies
    // belonging to that exact session can no longer arrive.
    for(auto it=physical_.begin();it!=physical_.end();)if(it->second.session==session){
      const auto entry=entries_.find(it->second.key);if(entry!=entries_.end()&&entry->second.token==it->first)Erase(it->second.key);
      --global_physical_;it=physical_.erase(it);
    }else ++it;
  }
  void Tick(double now){std::vector<std::string>expired;for(const auto&[key,entry]:entries_)if(now>=entry.deadline)expired.push_back(key);for(const auto&key:expired)Erase(key);}
  void Close(){if(closed_)return;closed_=true;*alive_=false;global_physical_-=physical_.size();physical_.clear();global_entries_-=entries_.size();entries_.clear();global_bytes_-=bytes_;bytes_=0;}
  static size_t Physical(){return global_physical_;}
  static size_t RetainedBytes(){return global_bytes_;}
  static size_t Entries(){return global_entries_;}
  size_t physical()const{return physical_.size();}size_t entries()const{return entries_.size();}size_t bytes()const{return bytes_;}
  // Completed cache entries do not keep a 100 Hz owner timer alive. Their
  // fixed age is checked by Body; storage is swept on admission, existing
  // owner ticks, or actual Close, under the same strict retained byte bounds.
  bool NeedsTick()const{return !physical_.empty();}
 private:
  static constexpr size_t kBody=2097152,kBytes=16*1048576,kGlobalBytes=32*1048576;
  struct Entry{uint64_t token=0;double deadline=0;Bytes prefix,suffix;bool acknowledged=false,finished=false;};
  struct PhysicalState{std::string key,session;double deadline;};
  static std::optional<Bytes> Decode(const std::string&text){
    if(text.size()>2796204||text.size()%4)return std::nullopt;
    if(text.empty())return Bytes{};
    auto data=CefBase64Decode(text);if(!data||data->GetSize()>kBody)return std::nullopt;Bytes bytes(data->GetSize());data->GetData(bytes.data(),bytes.size(),0);
    if(CefBase64Encode(bytes.data(),bytes.size()).ToString()!=text)return std::nullopt;
    return bytes;
  }
  bool Admit(size_t bytes)const{return bytes<=kBytes-bytes_&&bytes<=kGlobalBytes-global_bytes_;}
  void Charge(size_t bytes){bytes_+=bytes;global_bytes_+=bytes;}
  void Erase(const std::string&key){const auto found=entries_.find(key);if(found==entries_.end())return;const auto bytes=found->second.prefix.capacity()+found->second.suffix.capacity();bytes_-=bytes;global_bytes_-=bytes;entries_.erase(found);--global_entries_;}
  Send send_;std::function<double()>now_;std::function<void()>wake_;bool closed_=false;uint64_t serial_=0;size_t bytes_=0;
  std::map<std::string,Entry>entries_;std::map<uint64_t,PhysicalState>physical_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
  inline static size_t global_physical_=0,global_entries_=0,global_bytes_=0;
};
} // namespace suitecut
