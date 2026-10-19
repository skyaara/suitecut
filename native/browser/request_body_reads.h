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
// Genuine Network.getRequestPostData receipts include exact binary/multipart
// bytes via the pinned protocol base64Encoded field. No SDK body reconstruction.
// Logical cancellation retains its native receipt until actual ACK/channel close.
class NativeRequestBodyReads {
 public:
  using Bytes=std::vector<uint8_t>;
  using Reply=std::function<void(bool,Bytes,bool,const std::string&)>;
  using Receipt=std::function<void()>;
  using Result=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,const std::string&,double,Result)>;
  NativeRequestBodyReads(Send send,std::function<double()>now,std::function<void()>wake)
      :send_(std::move(send)),now_(std::move(now)),wake_(std::move(wake)){}
  ~NativeRequestBodyReads(){Close();}
  void Read(const std::string&session,const std::string&request,double deadline,Reply reply,Receipt receipt={}){
    if(!reply){if(receipt)receipt();return;}
    if(closed_||session.size()>128||request.empty()||request.size()>256||!std::isfinite(deadline)||deadline<0||(deadline&&now_()>=deadline)){
      reply(false,{},false,closed_?"Native request-body owner is closed":"Invalid or expired native request-body owner");if(receipt)receipt();return;
    }
    if(reads_.size()>=64||global_>=128||serial_>=9007199254740991ULL){reply(false,{},false,"Native physical request-body reader capacity exceeded");if(receipt)receipt();return;}
    const auto id=++serial_;reads_.emplace(id,ReadState{deadline,std::move(reply),std::move(receipt)});++global_;
    const auto alive=alive_;auto send=send_;
    send(session,request,deadline,[this,alive,id](bool ok,const std::string&json){
      if(!*alive)return;
      const auto found=reads_.find(id);if(found==reads_.end())return;
      auto reply=std::move(found->second.reply);const auto deadline=found->second.deadline;
      // The receipt is independent of logical delivery and survives timeout.
      struct ActualReceipt {Receipt done;~ActualReceipt(){if(done)done();}} receipt{std::move(found->second.receipt)};
      reads_.erase(found);--global_;
      if(!reply)return;
      if(deadline&&now_()>=deadline){reply(false,{},false,"Native original request-body deadline exceeded");return;}
      if(!ok){reply(false,{},false,json.empty()?"Actual native request body is unavailable":json.substr(0,8192));return;}
      if(json.size()>16*1048576){reply(false,{},false,"Native request body metadata exceeds limit");return;}
      const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto body=parsed?parsed->GetDictionary():nullptr;
      if(!body||body->GetType("postData")!=VTYPE_STRING||body->GetType("base64Encoded")!=VTYPE_BOOL){reply(false,{},false,"Invalid actual request-body metadata");return;}
      const auto text=body->GetString("postData").ToString();Bytes bytes;
      if(body->GetBool("base64Encoded")){
        if(text.size()>2796204){reply(false,{},false,"Actual request body exceeds 2 MiB");return;}
        if(!text.empty()){const auto decoded=CefBase64Decode(text);if(!decoded||decoded->GetSize()>2097152){reply(false,{},false,"Invalid or excessive actual request-body bytes");return;}bytes.resize(decoded->GetSize());decoded->GetData(bytes.data(),bytes.size(),0);if(CefBase64Encode(bytes.data(),bytes.size())!=text){reply(false,{},false,"Noncanonical native request-body base64");return;}}
      }else{if(text.size()>2097152){reply(false,{},false,"Actual request body exceeds 2 MiB");return;}bytes.assign(text.begin(),text.end());}
      reply(true,std::move(bytes),!body->GetBool("base64Encoded"),"");
    });
    if(!*alive)return;
    if(!closed_&&!reads_.empty())wake_();
  }
  // Native consumers can disappear while the actual Chromium reader remains.
  // This only cancels delivery; the real callback still owns the physical slot.
  void CancelAll(){for(auto&[id,read]:reads_)read.reply={};}
  void Tick(double now){
    std::vector<Reply>expired;
    for(auto&[id,read]:reads_)if(read.reply&&read.deadline&&now>=read.deadline)expired.push_back(std::move(read.reply));
    for(auto&reply:expired)reply(false,{},false,"Native original request-body deadline exceeded");
  }
  void Close(){
    if(closed_)return;
    closed_=true;*alive_=false;auto reads=std::move(reads_);reads_.clear();global_-=reads.size();
    for(auto&[id,read]:reads){if(read.reply)read.reply(false,{},false,"Actual native request-body owner closed");if(read.receipt)read.receipt();}
  }
  static size_t Physical(){return global_;}
  size_t physical()const{return reads_.size();}
  bool NeedsTick()const{for(const auto&[id,read]:reads_)if(read.reply&&read.deadline)return true;return false;}
 private:
  struct ReadState{double deadline;Reply reply;Receipt receipt;};
  inline static size_t global_=0;uint64_t serial_=0;bool closed_=false;
  Send send_;std::function<double()>now_;std::function<void()>wake_;
  std::map<uint64_t,ReadState>reads_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
};
} // namespace suitecut
