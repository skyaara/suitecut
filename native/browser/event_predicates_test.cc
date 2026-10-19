// SPDX-License-Identifier: MIT
#include "event_predicates.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <random>

int main() {
  using Controller=SuiteCutEventPredicateController;
  Controller controller;
  assert(!controller.Start(0,1,"client","console",0));
  assert(!controller.Start(1,0,"client","console",0));
  assert(!controller.Start(1,1,"","console",0));
  assert(!controller.Start(1,Controller::kMaxWireInteger+1,"client","console",0));
  assert(controller.Start(1,1,"client","console",0));
  assert(controller.Offer(1,1,1,"framenavigated","{}") == Controller::Status::Waiting);
  assert(controller.Cursor(1,1)==1);
  assert(controller.Offer(1,1,2,"console","{\"text\":\"first\"}") == Controller::Status::CandidateReady);
  const auto first=controller.Pending(1,1);assert(first);
  const auto token=first->token;
  std::string matched;
  assert(controller.Answer(1,2,token,"client",true,matched)==Controller::Decision::Stale);
  assert(controller.Answer(1,1,token+1,"client",true,matched)==Controller::Decision::Stale);
  assert(controller.Answer(1,1,token,"other",true,matched)==Controller::Decision::Stale);
  assert(controller.Answer(1,1,token,"client",false,matched)==Controller::Decision::Retry);
  assert(!controller.Pending(1,1)&&controller.Cursor(1,1)==2&&controller.PendingBytes()==0);
  assert(controller.Offer(1,1,3,"console","{\"text\":\"accepted\"}")==Controller::Status::CandidateReady);
  const auto accepted=controller.Pending(1,1);assert(accepted);
  assert(controller.Answer(1,1,accepted->token,"client",true,matched)==Controller::Decision::Matched);
  assert(matched=="{\"text\":\"accepted\"}"&&controller.Size()==0&&controller.PendingBytes()==0);
  for(int id=1;id<=64;++id)assert(controller.Start(id,2,"client","console",0));
  assert(!controller.Start(65,2,"client","console",0));
  const std::string large(Controller::kMaxPayload,'x');
  for(int id=1;id<=4;++id)assert(controller.Offer(id,2,1,"console",large)==Controller::Status::CandidateReady);
  assert(controller.PendingBytes()==Controller::kPendingBytes);
  assert(controller.Offer(5,2,1,"console",large)==Controller::Status::PayloadCapacity);
  controller.Cancel(1);assert(controller.PendingBytes()==3*Controller::kMaxPayload);
  assert(controller.Offer(5,2,1,"console",large)==Controller::Status::CandidateReady);
  controller.Clear();assert(controller.PendingBytes()==0&&controller.Size()==0);
  // WebSocket base64 snapshots may exceed the generic1MiB limit, but share
  // the same aggregate4MiB budget and exact cancel/late-ACK ownership.
  assert(!controller.Start(1,3,"socket","framereceived",0,0));
  assert(!controller.Start(1,3,"socket","framereceived",0,Controller::kWebSocketPayload+1));
  assert(controller.Start(1,3,"socket","framereceived",0,Controller::kWebSocketPayload));
  assert(controller.Start(2,3,"generic","console",0));
  const std::string expanded(Controller::kWebSocketPayload,'b');
  assert(controller.Offer(2,3,1,"console",expanded)==Controller::Status::PayloadCapacity);
  assert(controller.Offer(1,3,1,"framereceived",expanded)==Controller::Status::CandidateReady);
  assert(controller.Offer(2,3,1,"console",large)==Controller::Status::CandidateReady);
  assert(controller.PendingBytes()==Controller::kPendingBytes);
  const auto ws=controller.Pending(1,3);assert(ws);const auto ws_token=ws->token;
  controller.Cancel(1);assert(controller.PendingBytes()==Controller::kMaxPayload);
  assert(controller.Answer(1,3,ws_token,"socket",true,matched)==Controller::Decision::Stale);
  controller.Clear();assert(controller.PendingBytes()==0);
  std::mt19937_64 random(0x1a7e);
  for(std::uint64_t i=0;i<1000000;++i){
    const int id=static_cast<int>(random()%64)+1;const auto generation=i+3;
    assert(controller.Start(id,generation,"fuzz","console",0));
    assert(controller.Offer(id,generation,1,"console","{}") == Controller::Status::CandidateReady);
    const auto pending=controller.Pending(id,generation);assert(pending);const auto nonce=pending->token;
    assert(controller.Answer(id,generation-1,nonce,"fuzz",true,matched)==Controller::Decision::Stale);
    if(random()%3==0)controller.Cancel(id);
    else if(random()%2==0)assert(controller.Answer(id,generation,nonce,"fuzz",true,matched)==Controller::Decision::Matched);
    else {assert(controller.Answer(id,generation,nonce,"fuzz",false,matched)==Controller::Decision::Retry);controller.Cancel(id);}
    assert(controller.Answer(id,generation,nonce,"fuzz",true,matched)==Controller::Decision::Stale);
    assert(controller.Size()==0&&controller.PendingBytes()==0);
  }
}
