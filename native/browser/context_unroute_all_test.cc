// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <new>
#include "include/cef_api_hash.h"
#include "context_unroute_all.h"
// Real allocation failures exercise manager preparation, not a separate copy
// of its policy. Faulting ends at the actual admission boundary, before send.
static long allocation_fail=-1;
[[gnu::noinline]] void*operator new(std::size_t size){if(allocation_fail==0)throw std::bad_alloc();if(allocation_fail>0)--allocation_fail;if(auto*value=std::malloc(size?size:1))return value;throw std::bad_alloc();}
[[gnu::noinline]] void*operator new[](std::size_t size){return ::operator new(size);}
[[gnu::noinline]] void operator delete(void*value)noexcept{std::free(value);}
[[gnu::noinline]] void operator delete[](void*value)noexcept{std::free(value);}
[[gnu::noinline]] void operator delete(void*value,std::size_t)noexcept{std::free(value);}
[[gnu::noinline]] void operator delete[](void*value,std::size_t)noexcept{std::free(value);}
using Generic=suitecut::NativeContextRoutes;
using HAR=suitecut::NativeContextHARRoutes;
using Coordinator=suitecut::NativeContextUnrouteAll;
struct Fixture {
  double now=1;int failures=0,reply=0;bool success=false;std::string error;
  Generic generic{[this](const auto&,const auto&){++failures;},[this]{return now;}};
  HAR har{[this](const auto&,const auto&){++failures;},[this]{return now;}};
  Coordinator transaction{generic,har,[this]{return now;}};
  Generic::Route handler=suitecut::NativeContextRouteRegistration::Create("handler","**/*",0);
  std::shared_ptr<suitecut::NativeHARReplay>archive;
  suitecut::NativeHARRouteRegistration::Policy policy;
  std::deque<Generic::Result>generic_acks;std::deque<HAR::Result>har_acks;
  std::vector<std::string>calls;std::function<void()>at_send;
  Fixture(bool seed_har=true){
    assert(generic.Seed(1)&&generic.Seed(2));if(seed_har)assert(har.Seed(1));assert(har.Seed(2));
    archive=std::make_shared<suitecut::NativeHARReplay>();std::string reason;
    assert(archive->Load(CefParseJSON(R"({"log":{"entries":[]}})",JSON_PARSER_RFC)->GetDictionary(),reason));
    policy=suitecut::NativeHARRouteRegistration::Create(archive,false);assert(policy);
    assert(generic.Install(100,1,handler,0,[](bool ok,const auto&){assert(ok);}));
    if(seed_har)assert(har.Install(101,1,{"har","**/*",archive,false,{},policy},0,[](bool ok,const auto&){assert(ok);}));
  }
  void Attach(const std::string&owner="target"){
    assert(generic.Attach(1,owner,[this](auto mutation,auto,double,auto done){if(at_send)at_send();calls.push_back(mutation==Generic::Mutation::Install?"handler:install":"handler:remove");generic_acks.push_back(std::move(done));},[](bool,const auto&){}));
    assert(har.Attach(1,owner,[this](auto mutation,const auto&,double,auto done){if(at_send)at_send();calls.push_back(mutation==HAR::Mutation::Install?"har:install":"har:remove");har_acks.push_back(std::move(done));},[](bool,const auto&){}));
  }
  void GenericAck(bool ok=true){assert(!generic_acks.empty());auto ack=std::move(generic_acks.front());generic_acks.pop_front();ack(ok,ok?"":"actual handler removal rejected");}
  void HARAck(bool ok=true){assert(!har_acks.empty());auto ack=std::move(har_acks.front());har_acks.pop_front();ack(ok,ok?"":"actual HAR removal rejected");}
  bool Remove(int operation=1,uint64_t generation=1,bool wait=false,double deadline=100,Generic::RemovalReceipt receipt={}){
    return transaction.Remove(operation,1,generation,wait,deadline,std::move(receipt),[this](bool ok,const auto&reason){++reply;success=ok;error=reason;});
  }
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
  // Both books have committed before the first physical send or language
  // admission callback. No target, Page policy or unrelated context is added.
  {Fixture f;f.Attach();f.GenericAck();f.HARAck();
    int observed=0;f.at_send=[&]{assert(!f.handler->available());auto h=f.har.PrepareRemoveAll(900,1,0,[](bool,const auto&){});assert(!h);++observed;};
    assert(f.Remove(1,1,false,100,[&](bool applied,const auto&ids){assert(ids==std::vector<std::string>{"handler"});assert(!f.handler->available());if(!applied)assert(f.calls.size()==2);}));
    assert(observed==2&&f.reply==0);f.GenericAck();assert(!f.reply);f.HARAck();assert(f.reply==1&&f.success&&!f.transaction.pending());
  }
  // Removal waits behind the genuine earlier bootstrap installer ACK on each
  // owner channel. Logical deactivation never retires either held callback.
  {Fixture f;f.Attach();assert(f.generic.physical()==1&&f.har.physical()==1);assert(f.Remove());assert(f.calls.size()==2&&f.reply==0);f.GenericAck();assert(f.calls.back()=="handler:remove"&&f.generic.physical()==1);f.HARAck();assert(f.calls.back()=="har:remove"&&f.har.physical()==1);f.GenericAck();f.HARAck();assert(f.success&&f.reply==1);}
  // Second-family rejection leaves the first family's original policy live.
  {Fixture f(false);assert(!f.Remove()&&f.handler->available()&&f.generic.pending()==0&&!f.transaction.pending());}
  // Fail every actual allocation up to admission. No first-family removal,
  // retained observer, partial target queue or emitted receipt may survive.
  {bool completed=false;size_t faults=0;for(long point=0;point<1000&&!completed;++point){Fixture f;f.Attach();f.GenericAck();f.HARAck();const auto before=f.calls.size();int admitted=0;allocation_fail=point;
      const bool accepted=f.Remove(1,1,false,100,[&](bool applied,const auto&){if(!applied){allocation_fail=-1;++admitted;}});allocation_fail=-1;
      if(!accepted){++faults;assert(f.handler->available()&&admitted==0&&f.calls.size()==before&&f.reply==0&&!f.transaction.pending()&&!f.generic.pending()&&!f.har.pending());
        auto original=f.har.PrepareRemoveAll(900,1,0,[](bool,const auto&){});assert(original&&f.har.RemovalCurrent(*original));
      }else{completed=true;assert(admitted==1&&!f.handler->available());f.GenericAck();f.HARAck();assert(f.success);}
    }assert(completed&&faults>20);}
  // Already expired admission mutates neither family, even before Tick.
  {Fixture f;f.now=100;assert(!f.Remove()&&f.handler->available()&&!f.reply&&!f.transaction.pending());}
  // Wait observes exact real handler/read/Fetch receipts. Separate read Data
  // or route logical completion cannot release the native physical books.
  {Fixture f;auto&callbacks=suitecut::GlobalRouteCallbackLeases();const auto scope=*callbacks.Scope();const auto token=*callbacks.Issue(scope,"running",f.handler);assert(callbacks.Start(token,scope,"running"));
    auto&work=suitecut::GlobalHARWorkLeases();auto fetch=work.Issue(scope,"",f.policy,suitecut::NativeHARWorkLeases::Kind::ReplayFetch),read=work.Issue(scope,"",f.policy,suitecut::NativeHARWorkLeases::Kind::PostRead);assert(fetch&&read);
    assert(f.Remove(1,1,true)&&!f.reply&&f.transaction.pending()==1);assert(callbacks.Finish(token,scope,"running"));fetch->acknowledge(true,"");f.transaction.Tick();assert(!f.reply);read->acknowledge(true,"");f.transaction.Tick();assert(f.success&&f.reply==1);}
  // Default behavior waits for real policy-removal ACKs but leaves earlier
  // admitted native work owned independently after logical success.
  {Fixture f;auto&work=suitecut::GlobalHARWorkLeases();auto fetch=work.Issue(100,"",f.policy,suitecut::NativeHARWorkLeases::Kind::ReplayFetch);assert(fetch);assert(f.Remove()&&f.success&&!fetch->receipt->settled());fetch->acknowledge(true,"");}
  // An admitted synchronous consumer may cancel. Committed removal still
  // dispatches and retains both physical target callbacks until actual ACK.
  {Fixture f;f.Attach();f.GenericAck();f.HARAck();assert(f.Remove(1,7,false,100,[&](bool applied,const auto&){assert(!applied);f.transaction.Cancel(1,7);}));assert(f.generic.physical()==1&&f.har.physical()==1&&f.reply==0&&!f.transaction.pending());f.GenericAck();f.HARAck();assert(!f.generic.physical()&&!f.har.physical()&&f.reply==0);}
  // Clear retires only the combined observer. Both live managers must still
  // dispatch their committed tasks after the admission callback returns.
  {Fixture f;f.Attach();f.GenericAck();f.HARAck();assert(f.Remove(1,7,false,100,[&](bool applied,const auto&){assert(!applied);f.transaction.Clear();}));assert(f.reply==1&&!f.success&&!f.transaction.pending()&&f.generic.physical()==1&&f.har.physical()==1);f.GenericAck();f.HARAck();assert(!f.generic.physical()&&!f.har.physical()&&!f.generic.pending()&&!f.har.pending()&&f.reply==1);}
  // Actual observer destruction cannot leave an idle target's HAR binding
  // installed. Dispatch uses only independently guarded manager pointers.
  {Fixture f;f.Attach();f.GenericAck();f.HARAck();auto transaction=std::make_unique<Coordinator>(f.generic,f.har,[&]{return f.now;});int reply=0;
    assert(transaction->Remove(1,1,7,false,100,[&](bool applied,const auto&){assert(!applied);transaction.reset();},[&](bool ok,const auto&){assert(!ok);++reply;}));assert(!transaction&&reply==1&&f.generic.physical()==1&&f.har.physical()==1);f.GenericAck();f.HARAck();assert(!f.generic.physical()&&!f.har.physical()&&!f.generic.pending()&&!f.har.pending()&&reply==1);}
  // Policy removal ACK at the original deadline cannot publish late success.
  {Fixture f;f.Attach();f.GenericAck();f.HARAck();assert(f.Remove());f.GenericAck();f.now=100;f.HARAck();assert(f.reply==1&&!f.success&&f.error.find("deadline")!=std::string::npos);}
  // A genuine new target after commit inherits neither selected registration.
  {Fixture f;assert(f.Remove()&&f.success);f.Attach("future");assert(f.calls.empty());}
  // Manager preparation is invalidated by a real attachment generation change;
  // an old candidate cannot overwrite a newly admitted target's queue.
  {Fixture f;auto generic=f.generic.PrepareRemoveAll(1,1,100,[](bool,const auto&){});auto har=f.har.PrepareRemoveAll(1,1,100,[](bool,const auto&){});assert(generic&&har);f.Attach();assert(!f.generic.RemovalCurrent(*generic)&&!f.har.RemovalCurrent(*har)&&f.handler->available());f.GenericAck();f.HARAck();}
  // Per-native-manager claim prevents unrelated contexts/managers from
  // adopting another context's still-retained HAR read or replay policy.
  {Fixture f;HAR other([](const auto&,const auto&){assert(false);});assert(other.Seed(1));assert(!other.Install(1,1,{"unrelated","**/*",f.archive,false,{},f.policy},0,[](bool,const auto&){}));assert(f.har.OwnsPolicy(1,f.policy)&&!f.har.OwnsPolicy(2,f.policy)&&!other.OwnsPolicy(1,f.policy));}
  // Native context integers are not incarnations. A real old running callback
  // and HAR read survive logical context close, but a replacement seed with
  // the same integer must neither capture them nor reactivate their policies.
  {Fixture f;auto&callbacks=suitecut::GlobalRouteCallbackLeases();const auto scope=*callbacks.Scope();const auto token=*callbacks.Issue(scope,"old-context",f.handler);assert(callbacks.Start(token,scope,"old-context"));auto work=suitecut::GlobalHARWorkLeases().Issue(scope,"",f.policy,suitecut::NativeHARWorkLeases::Kind::PostRead);assert(work);
    f.generic.Closed(1);f.har.Closed(1);assert(f.generic.Seed(1)&&f.har.Seed(1));assert(!f.generic.OwnsPolicy(1,*f.handler)&&!f.har.OwnsPolicy(1,f.policy));assert(f.Remove(1,1,true)&&f.success&&!work->receipt->settled()&&callbacks.running()==1);assert(callbacks.Finish(token,scope,"old-context"));work->acknowledge(true,"");}
  // Even empty prepared candidates carry their real seed pin. Close+reseed
  // cannot revive an old empty reservation with coincidentally equal maps.
  {double now=1;Generic g([](const auto&,const auto&){assert(false);},[&]{return now;});HAR h([](const auto&,const auto&){assert(false);},[&]{return now;});assert(g.Seed(1)&&h.Seed(1));auto a=g.PrepareRemoveAll(1,1,100,[](bool,const auto&){});auto b=h.PrepareRemoveAll(1,1,100,[](bool,const auto&){});assert(a&&b);g.Closed(1);h.Closed(1);assert(g.Seed(1)&&h.Seed(1));assert(!g.RemovalCurrent(*a)&&!h.RemovalCurrent(*b));}
  std::cout<<"PASS16 actual native generic+HAR manager transaction/real held installers/allocation rollback/independent physical work/context incarnations/observer destruction; controlled, no browser feature claim\n";
}
