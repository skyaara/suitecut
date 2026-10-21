// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include "include/cef_api_hash.h"
#include "dictionary_number.h"
#include "event_predicates.h"
#include "operation_registry.h"
#include "websocket_controls.h"
using Dict=CefRefPtr<CefDictionaryValue>;
static Dict Header(const char* type){auto value=CefDictionaryValue::Create();value->SetString("type",type);return value;}
static std::string Json(Dict value){auto wrapper=CefValue::Create();wrapper->SetDictionary(value);return CefWriteJSON(wrapper,JSON_WRITER_DEFAULT);}

// Execute the exact Source include. CEF dictionaries, socket journal and native
// operation/candidate ownership are real; only Writer admission and time are
// controlled. This does not represent an actual browser or Engine barrier.
struct SourceFixture {
  struct Packet {Dict header;std::vector<uint8_t> bytes;};
  struct Writer {
    bool block_predicate=false;
    std::vector<Packet> packets;
    bool TryEvent(Dict header,std::vector<uint8_t> bytes){
      if(block_predicate&&header->GetString("type")=="eventpredicate")return false;
      packets.push_back({header->Copy(false),std::move(bytes)});return true;
    }
  } writer_;
  SuiteCutOperationRegistry operations_;
  SuiteCutEventPredicateController event_predicates_;
  std::unique_ptr<suitecut::NativeWebSocketControls> sockets_;
  struct Result {int id;bool ok;std::string error;};
  std::vector<Result> results;
  int page_id_=2,wakes=0;
  double clock=1;
  double Now()const{return clock;}
  void WakeDeadlines(){++wakes;}
  void Complete(int id,bool ok,const std::string& error){
    results.push_back({id,ok,error});event_predicates_.Cancel(id);
    websocket_predicates_.erase(id);sockets_->Cancel(id);operations_.Finish(id);
  }
#include "controllers/network/websocket_predicates.inc"
  SourceFixture(){
    sockets_=std::make_unique<suitecut::NativeWebSocketControls>([this](Dict info,const std::vector<uint8_t>& bytes){
      auto header=Header("websocket");header->SetDictionary("socket",info->Copy(false));
      const bool accepted=writer_.TryEvent(header,bytes);if(accepted)WebSocketPredicatePublished(info);return accepted;
    },[this]{WakeDeadlines();},[](const std::string&){assert(false);});
    auto params=CefDictionaryValue::Create();params->SetString("requestId","genuine-component-request");params->SetString("url","ws://native.test/actual");
    sockets_->Event("Network.webSocketCreated",params,"",clock);
  }
  std::uint64_t Begin(int id,double timeout=100,const char* event="framereceived"){
    const auto operation=operations_.Begin(id,clock,timeout);assert(operation);
    auto config=CefDictionaryValue::Create();config->SetString("socketId","1");config->SetString("event",event);config->SetString("clientToken","actual-client");config->SetDouble("afterSequence",double(sockets_->sequence()));
    StartWebSocketPredicate(id,operation->generation,config);return operation->generation;
  }
  void Receive(const std::string& payload){
    auto params=CefDictionaryValue::Create();params->SetString("requestId","genuine-component-request");
    auto response=CefDictionaryValue::Create();response->SetInt("opcode",1);response->SetString("payloadData",payload);params->SetDictionary("response",response);
    sockets_->Event("Network.webSocketFrameReceived",params,"",clock);
  }
  void Close(){auto params=CefDictionaryValue::Create();params->SetString("requestId","genuine-component-request");sockets_->Event("Network.webSocketClosed",params,"",clock);}
  void Tick(double now){clock=now;sockets_->Tick(clock);PumpWebSocketPredicates();}
};

int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  using Controller=SuiteCutEventPredicateController;
  // Real event admission precedes its candidate. A busy Writer retains the
  // native scope and retry; only one successful publication is permitted.
  {SourceFixture source;const auto generation=source.Begin(7);const auto deadline=source.operations_.Get(7)->deadline_ms;
    source.writer_.block_predicate=true;source.Receive("first");
    assert(source.operations_.Size()==1&&source.event_predicates_.Size()==1&&source.wakes>0);
    assert(source.writer_.packets.size()==2&&source.event_predicates_.PendingBytes()>0);
    source.writer_.block_predicate=false;source.Tick(2);assert(source.writer_.packets.size()==3);
    const auto& packet=source.writer_.packets.back();assert(packet.header->GetString("type")=="eventpredicate"&&packet.header->GetInt("pageId")==2&&packet.header->GetString("event")=="framereceived");
    const auto parsed=CefParseJSON(std::string(packet.bytes.begin(),packet.bytes.end()),JSON_PARSER_RFC);assert(parsed&&parsed->GetDictionary()->GetString("payloadBase64")=="Zmlyc3Q=");
    source.Tick(3);assert(source.writer_.packets.size()==3);
    const auto offered=source.event_predicates_.Pending(7,generation);assert(offered);const auto token=offered->token;std::string matched;
    assert(source.event_predicates_.Answer(7,generation,token,"actual-client",false,matched)==Controller::Decision::Retry);
    source.PollWebSocketPredicate(7,generation);assert(source.operations_.Get(7)->deadline_ms==deadline);
    source.Receive("second");const auto next=source.event_predicates_.Pending(7,generation);assert(next&&next->token!=token);
    assert(source.event_predicates_.Answer(7,generation,next->token,"actual-client",true,matched)==Controller::Decision::Matched);
    source.Complete(7,true,"");assert(source.operations_.Size()==0&&source.websocket_predicates_.empty()&&source.event_predicates_.PendingBytes()==0);
  }
  // A real close behind an offered host candidate cancels the original native
  // operation. No later host ACK or tick can resurrect native ownership.
  {SourceFixture source;const auto generation=source.Begin(7);source.Receive("pending");const auto offered=source.event_predicates_.Pending(7,generation);assert(offered);const auto token=offered->token;
    source.Close();assert(source.results.size()==1&&!source.results.back().ok&&source.results.back().error=="WebSocket closed");assert(source.operations_.Size()==0&&source.websocket_predicates_.empty());
    assert(source.writer_.packets.back().header->GetDictionary("socket")->GetString("event")=="close");
    std::string matched;assert(source.event_predicates_.Answer(7,generation,token,"actual-client",true,matched)==Controller::Decision::Stale);
    source.Tick(10);assert(source.results.size()==1);
  }
  // Existing original deadline wins while a candidate is Writer-blocked.
  {SourceFixture source;source.Begin(7,5);source.writer_.block_predicate=true;source.Receive("pending");source.Tick(6);assert(source.results.size()==1&&!source.results.back().ok&&source.results.back().error.find("Timeout")!=std::string::npos);assert(source.event_predicates_.PendingBytes()==0);}
  // A close predicate receives the genuine close instead of the early-close
  // failure reserved for other socket events.
  {SourceFixture source;const auto generation=source.Begin(7,100,"close");source.Close();assert(source.results.empty());const auto offered=source.event_predicates_.Pending(7,generation);assert(offered&&offered->event=="close");}
}
