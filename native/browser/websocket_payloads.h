// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "include/cef_values.h"
namespace suitecut {
// Chunked input preserves the existing 64 KiB browser command limit. Budget
// follows actual callback capture lifetime through committed Inspector work.
class NativeWebSocketPayloads {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using Validate=std::function<bool(Dict)>;
  using Commit=std::function<void(Dict,double,Reply)>;
  explicit NativeWebSocketPayloads(std::function<double()>now):now_(std::move(now)){}
  bool Pending()const{return !stages_.empty();}
  void Tick(){std::erase_if(stages_,[this](const auto&item){return now_()>=item.second->deadline;});}
  void Closed(int context,int page=-1){std::erase_if(stages_,[context,page](const auto&item){return item.second->context==context&&(page<0||item.second->page==page);});}
  void Command(int context,int page,Dict input,double deadline,Validate validate,Commit commit,Reply reply){
    Tick();const auto action=input->GetString("action").ToString();
    if(action=="routePayloadBegin"){
      const auto operation=input->GetString("operation").ToString();const int bytes=input->GetInt("bytes"),opcode=input->GetInt("opcode");
      if(input->GetType("bytes")!=VTYPE_INT||bytes<1||bytes>2097152||(opcode!=1&&opcode!=2)||(operation!="replace"&&operation!="client"&&operation!="server")||!validate(input)||budget_->slots>=4||size_t(bytes)>8*1048576-budget_->bytes||serial_==UINT64_MAX){reply(false,nullptr,"Invalid native WebSocket payload admission or capacity");return;}
      auto stage=std::make_shared<Stage>();stage->budget=budget_;stage->declared=size_t(bytes);++budget_->slots;budget_->bytes+=stage->declared;
      stage->context=context;stage->page=page;stage->deadline=deadline?deadline:now_()+30000;stage->metadata=input->Copy(false);stage->bytes.reserve(stage->declared);
      const auto token=std::to_string(++serial_);stages_[token]=stage;auto result=CefDictionaryValue::Create();result->SetString("payloadToken",token);reply(true,result,"");return;
    }
    const auto token=input->GetString("payloadToken").ToString();const auto found=stages_.find(token);
    if(found==stages_.end()||found->second->context!=context||found->second->page!=page){reply(false,nullptr,"Native WebSocket staged payload is stale or belongs to another scope");return;}
    auto stage=found->second;
    for(const char*name:{"targetId","routingToken","requestId","registrationId","payloadScope"})if(input->GetString(name)!=stage->metadata->GetString(name)){reply(false,nullptr,"Native WebSocket staged payload target identity differs");return;}
    if(!validate(stage->metadata)){stages_.erase(found);reply(false,nullptr,"Native WebSocket staged payload channel is no longer owned");return;}
    if(action=="routePayloadAbort"){stages_.erase(found);reply(true,nullptr,"");return;}
    if(action=="routePayloadAppend"){
      const auto encoded=input->GetString("base64").ToString();auto binary=CefBase64Decode(encoded);
      if(input->GetType("base64")!=VTYPE_STRING||encoded.empty()||encoded.size()>32768||!binary||binary->GetSize()>24576||binary->GetSize()>stage->declared-stage->bytes.size()){reply(false,nullptr,"Invalid native WebSocket payload chunk or declared length exceeded");return;}
      std::vector<uint8_t>decoded(binary->GetSize());binary->GetData(decoded.data(),decoded.size(),0);
      if(CefBase64Encode(decoded.data(),decoded.size())!=encoded){reply(false,nullptr,"Native WebSocket payload chunk is not canonical base64");return;}
      stage->bytes.insert(stage->bytes.end(),decoded.begin(),decoded.end());reply(true,nullptr,"");return;
    }
    if(action!="routePayloadCommit"||stage->bytes.size()!=stage->declared){reply(false,nullptr,"Native WebSocket staged payload is incomplete or operation invalid");return;}
    stages_.erase(found);auto metadata=stage->metadata->Copy(false);metadata->SetString("payloadBase64",CefBase64Encode(stage->bytes.data(),stage->bytes.size()));
    // The retained real transport callback owns the entire staging budget until
    // ACK or actual target-channel death, including after logical cancellation.
    commit(metadata,stage->deadline,[stage,reply=std::move(reply)](bool ok,Dict result,const std::string&error){reply(ok,result,error);});
  }
 private:
  struct Budget {size_t slots=0,bytes=0;};
  struct Stage {std::shared_ptr<Budget>budget;size_t declared=0;int context=0,page=0;double deadline=0;Dict metadata;std::vector<uint8_t>bytes;~Stage(){if(budget){--budget->slots;budget->bytes-=declared;}}};
  std::function<double()>now_;std::shared_ptr<Budget>budget_=std::make_shared<Budget>();uint64_t serial_=0;std::map<std::string,std::shared_ptr<Stage>>stages_;
};
} // namespace suitecut
