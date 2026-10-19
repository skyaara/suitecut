// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
// A real Source owns this channel. A logical deadline cannot erase a pending
// physical CDP callback; genuine Source close retires its canceled channel.
class NativeResponseBodyReads {
 public:
  using Bytes=std::vector<uint8_t>;
  using Reply=std::function<void(bool,Bytes,const std::string&)>;
  using Result=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,const std::string&,double,Result)>;
  NativeResponseBodyReads(Send send,std::function<double()>now,std::function<void()>wake)
      :send_(std::move(send)),now_(std::move(now)),wake_(std::move(wake)){}
  ~NativeResponseBodyReads(){Close();}
  void Read(const std::string&session,const std::string&request,double deadline,Reply reply){
    if(!reply)return;
    if(closed_||session.size()>128||request.empty()||request.size()>256||!std::isfinite(deadline)||deadline<0||(deadline&&now_()>=deadline)){
      reply(false,{},closed_?"Native response-body owner is closed":"Invalid or expired native response-body owner");return;
    }
    if(reads_.size()>=64||global_>=128||serial_>=9007199254740991ULL){reply(false,{},"Native physical response-body reader capacity exceeded");return;}
    const auto id=++serial_;reads_.emplace(id,ReadState{deadline,std::move(reply)});++global_;
    send_(session,request,deadline,[this,alive=alive_,id](bool ok,const std::string&json){
      if(!*alive)return;
      const auto found=reads_.find(id);if(found==reads_.end())return;
      auto reply=std::move(found->second.reply);const auto deadline=found->second.deadline;reads_.erase(found);--global_;
      if(!reply)return;
      if(deadline&&now_()>=deadline){reply(false,{},"Native original response-body deadline exceeded");return;}
      if(!ok){reply(false,{},json.empty()?"Actual native response body is unavailable":json.substr(0,8192));return;}
      if(json.size()>16*1048576){reply(false,{},"Native response body metadata exceeds limit");return;}
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto body=parsed?parsed->GetDictionary():nullptr;
      if(!body||body->GetType("body")!=VTYPE_STRING||body->GetType("base64Encoded")!=VTYPE_BOOL){reply(false,{},"Invalid actual response-body metadata");return;}
      const auto text=body->GetString("body").ToString();Bytes bytes;
      if(body->GetBool("base64Encoded")){
        if(text.size()>2796204){reply(false,{},"Actual response body exceeds 2 MiB");return;}
        if(!text.empty()){const auto decoded=CefBase64Decode(text);if(!decoded||decoded->GetSize()>2097152){reply(false,{},"Invalid or excessive actual response-body bytes");return;}bytes.resize(decoded->GetSize());decoded->GetData(bytes.data(),bytes.size(),0);}
      }else{if(text.size()>2097152){reply(false,{},"Actual response body exceeds 2 MiB");return;}bytes.assign(text.begin(),text.end());}
      reply(true,std::move(bytes),"");
    });
    if(!closed_&&!reads_.empty())wake_();
  }
  void Tick(double now){
    std::vector<Reply>expired;
    for(auto&[id,read]:reads_)if(read.reply&&read.deadline&&now>=read.deadline)expired.push_back(std::move(read.reply));
    for(auto&reply:expired)reply(false,{},"Native original response-body deadline exceeded");
  }
  void Close(){
    if(closed_)return;
    closed_=true;*alive_=false;auto reads=std::move(reads_);reads_.clear();global_-=reads.size();
    for(auto&[id,read]:reads)if(read.reply)read.reply(false,{},"Actual native response-body owner closed");
  }
  size_t physical()const{return reads_.size();}
  bool NeedsTick()const{for(const auto&[id,read]:reads_)if(read.reply&&read.deadline)return true;return false;}
 private:
  struct ReadState{double deadline;Reply reply;};
  inline static size_t global_=0;uint64_t serial_=0;bool closed_=false;
  Send send_;std::function<double()>now_;std::function<void()>wake_;
  std::map<uint64_t,ReadState>reads_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
};
} // namespace suitecut
