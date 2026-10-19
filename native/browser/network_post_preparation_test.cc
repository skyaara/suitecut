// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_post_preparation.h"
#include "request_body_reads.h"
using Preparation=suitecut::NativeNetworkPostPreparation;
using Publication=suitecut::NativeNetworkBodyPublication;
using Reads=suitecut::NativeRequestBodyReads;
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));double now=1;std::vector<Reads::Result>acks;std::vector<Publication::Dict>events;std::vector<uint8_t>wire;uint64_t token=0;int data=0,receipts=0,applied=0;
  Reads reads([&](const std::string&session,const std::string&request,double deadline,Reads::Result ack){assert(session=="session"&&request=="post"&&deadline==100);acks.push_back(std::move(ack));},[&]{return now;},[]{});
  Publication publication([&](uint64_t id,size_t,size_t,std::span<const uint8_t>bytes,bool cancel){if(!cancel){token=id;wire.insert(wire.end(),bytes.begin(),bytes.end());}return true;},[&](Publication::Dict event){events.push_back(event->Copy(false));},[](const std::string&){assert(false);});
  Preparation prepare(publication,[&](const auto&session,const auto&request,double deadline,Preparation::Data done,Preparation::Receipt receipt){reads.Read(session,request,deadline,std::move(done),std::move(receipt));},[&](const std::string&id){assert(id=="logical");return Preparation::Reservation{[&](bool available,const auto&bytes,bool text){assert(available&&!text&&bytes==std::vector<uint8_t>({0,255,1}));++data;},[&]{++receipts;}};},[&](const std::string&id,Preparation::Dict fields,bool,const auto&,bool){assert(id=="logical"&&fields->HasKey("postDataBodyId"));++applied;});
  const auto gate=prepare.Prepare("logical","session","post",100);assert(gate&&prepare.Gate("logical")==gate);
  auto event=CefDictionaryValue::Create();auto request=CefDictionaryValue::Create();request->SetString("id","logical");request->SetBool("postDataUnavailable",true);event->SetDictionary("request",request);
  assert(publication.Queue(event,gate));const auto original=publication.QueuedCursor();assert(publication.Queue(event,gate));assert(events.empty());
  prepare.Start("logical");prepare.Start("logical");assert(acks.size()==1&&receipts==0&&reads.physical()==1);
  acks[0](true,"{\"postData\":\"AP8B\",\"base64Encoded\":true}");assert(data==1&&receipts==1&&applied==1&&prepare.pending()==0&&events.empty());
  publication.Tick(now);assert(token&&wire==std::vector<uint8_t>({0,255,1}));assert(publication.Received(token));assert(events.size()==2&&publication.PublishedCursor()>original);
  for(auto&value:events){auto actual=value->GetDictionary("request");assert(actual->GetString("postDataBodyId")==std::to_string(token)&&!actual->GetBool("postDataUnavailable"));}
  assert(publication.Taken(token));publication.Clear();
  // Clearing metadata does not settle a physically dispatched native read or
  // the original capture's receipt. Actual close settles it exactly once.
  int outcome=0,closed=0;Preparation clearing(publication,[&](const auto&session,const auto&request,double deadline,Preparation::Data done,Preparation::Receipt receipt){reads.Read(session,request,deadline,std::move(done),std::move(receipt));},[&](const std::string&){return Preparation::Reservation{[&](bool ok,const auto&,bool){assert(!ok);++outcome;},[&]{++closed;}};},[](const std::string&,Preparation::Dict,bool,const auto&,bool){assert(false);});
  assert(clearing.Prepare("another","session","post",100));clearing.Start("another");clearing.Clear();assert(reads.physical()==1&&closed==0);reads.Close();assert(reads.physical()==0&&closed==1&&outcome==1);acks.back()(true,"{}");assert(closed==1);
  // A physical Network ID can be reused by a genuine redirect hop. Its late
  // getRequestPostData reply must never become the previous hop's body bytes.
  int unavailable=0,physical=0;Preparation::Data late;Preparation::Receipt receipt;
  Preparation redirect(publication,[&](const auto&,const auto&,double,Preparation::Data done,Preparation::Receipt actual){late=std::move(done);receipt=std::move(actual);},[&](const std::string&){return Preparation::Reservation{[&](bool ok,const auto&bytes,bool){assert(!ok&&bytes.empty());++unavailable;},[&]{++physical;}};},[](const std::string&,Preparation::Dict,bool,const auto&,bool){assert(false);});
  const auto redirected=redirect.Prepare("old-hop","session","same-native-id",100);assert(redirected);redirect.Start("old-hop");assert(publication.Queue(event,redirected));redirect.Invalidate("old-hop");assert(redirect.pending()==0&&physical==0);late(true,{9,8,7},false,"");assert(unavailable==1&&physical==0);receipt();assert(physical==1);publication.Clear();
  // Archive ownership is distinct from logical metadata/data delivery. A
  // genuine read receipt (including one after Clear) alone releases its pin.
  {
    Preparation::Data actual_data;Preparation::Receipt actual_receipt;
    Preparation retained(publication,[&](const auto&,const auto&,double,auto data,auto receipt){actual_data=std::move(data);actual_receipt=std::move(receipt);},{},[](const auto&,auto,bool,const auto&,bool){});
    auto archive=std::make_shared<int>(7);std::weak_ptr<int>weak=archive;
    assert(retained.Prepare("physical-archive","session","request",100));assert(retained.Retain("physical-archive",archive));retained.Start("physical-archive");archive.reset();retained.Clear();assert(!weak.expired());
    actual_data(false,{},false,"closed");assert(!weak.expired());actual_receipt();assert(weak.expired());actual_receipt();
  }

  // Source owns its request-body preparation. A retained admission hook must
  // not keep that closed Source alive after the actual page channel retires.
  // Repeat enough distinct owners to expose a per-context leak under LSan.
  for(int iteration=0;iteration<10000;++iteration){
    struct Owner {std::unique_ptr<Preparation>prepare;};
    auto owner=std::make_shared<Owner>();std::weak_ptr<Owner>weak=owner;
    owner->prepare=std::make_unique<Preparation>(publication,
      [owner](const auto&,const auto&,double,auto,auto){assert(false);},
      [owner](const auto&){return Preparation::Reservation{};},
      [owner](const auto&,auto,bool,const auto&,bool){assert(false);});
    owner.reset();assert(!weak.expired());auto closing=weak.lock();
    closing->prepare->Clear();closing.reset();assert(weak.expired());
  }

}
