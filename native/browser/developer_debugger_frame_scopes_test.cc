// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include <set>
#include "developer_debugger_frame_scopes.h"
using Scopes=suitecut::FrameDebuggerScopes;
struct Harness{
 double now=1;Scopes::Host host{"actual-source",1};std::set<uint64_t>leases;
 std::vector<Scopes::Token>cleanup;size_t closes=0,wakes=0;bool current=true;
 std::function<void()>on_cleanup,on_close;
 std::shared_ptr<Scopes::Budget>budget;std::shared_ptr<Scopes>book;
 explicit Harness(std::shared_ptr<Scopes::Budget>shared=std::make_shared<Scopes::Budget>(),Scopes::Host actual={"actual-source",1}):host(std::move(actual)),budget(std::move(shared)){
  Scopes::Hooks hooks;hooks.now=[this]{return now;};hooks.current=[this](const auto&){return current;};hooks.lease_present=[this](uint64_t lease){return leases.contains(lease);};hooks.cleanup=[this](auto token,const auto&){cleanup.push_back(token);if(on_cleanup)on_cleanup();};hooks.close_channel=[this](const auto&,const auto&){++closes;if(on_close)on_close();};hooks.wake=[this]{++wakes;};book=Scopes::Create(host,std::move(hooks),budget);assert(book);
 }
 ~Harness(){if(book)book->HostClosed(host);}
 Scopes::Pin Pin(std::string frame="root",std::string session="",uint64_t attachment=1){return {{host,std::move(session)},attachment,1,std::move(frame),"actual-loader","actual-default-context",7,false};}
 Scopes::Token Reserve(uint64_t lease,Scopes::Pin pin,double deadline=100){std::string error;return book->Reserve(std::move(pin),lease,deadline,error);}
 Scopes::Token Open(uint64_t lease,Scopes::Pin pin){const auto token=Reserve(lease,std::move(pin));assert(token&&book->BeginAcquire(token));leases.insert(lease);assert(book->Ready(token,true));book->AcquireReturned(token);return token;}
};
int main(){size_t groups=0;
 {Harness h;auto pin=h.Pin();pin.root=true;const auto root=h.Open(1,pin);assert(!h.Reserve(2,h.Pin("child")));auto child=h.Pin("child");child.channel.session="actual-oopif-session";const auto oopif=h.Open(3,child);assert(oopif&&h.book->Retained()==2&&h.budget->retained()==2);h.book->Close(root);assert(!h.Reserve(4,h.Pin("child")));h.leases.erase(1);h.book->Poll(root);assert(h.Reserve(4,h.Pin("child")));++groups;}
 {auto budget=std::make_shared<Scopes::Budget>();Harness root(budget),duplicate(budget),other(budget,{"different-real-source",1});const auto a=root.Open(1,root.Pin());assert(!duplicate.Reserve(1,duplicate.Pin("child")));assert(other.Reserve(1,other.Pin()));duplicate.book->Close(a);assert(root.book->Current(a));++groups;}
 {Harness h;const auto token=h.Reserve(1,h.Pin());assert(h.book->BeginAcquire(token));h.leases.insert(1);h.book->Close(token);assert(h.book->Retained()==1&&!h.book->Ready(token,true));h.book->AcquireReturned(token);assert(h.book->Retained()==1&&!h.Reserve(2,h.Pin()));h.leases.erase(1);h.book->Poll(token);assert(!h.book->Retained());++groups;}
 {Harness h;auto token=h.Reserve(1,h.Pin());assert(h.book->BeginAcquire(token));h.on_cleanup=[&]{h.leases.erase(1);h.book->Poll(token);assert(h.book->Retained()==1);};assert(!h.book->Ready(token,false));h.book->AcquireReturned(token);assert(!h.book->Retained()&&h.budget->retained()==0);++groups;}
 {Harness h;auto token=h.Open(1,h.Pin());const auto receipt=h.book->Hold(token);assert(receipt);h.book->Close(token);h.leases.erase(1);h.book->Poll(token);assert(h.book->Retained()==1&&h.book->Physical()==1&&!h.Reserve(2,h.Pin()));assert(h.book->Retire(receipt)&&!h.book->Retire(receipt));assert(!h.book->Retained());++groups;}
 {Harness h;auto pin=h.Pin("child","actual-session");const auto token=h.Open(1,pin);const auto late=h.book->Hold(token);h.book->Close(token);h.book->ChannelClosed(pin.channel,2);assert(h.book->Retained()==1);h.book->ChannelClosed(pin.channel,1);assert(!h.book->Retained()&&!h.book->Physical());pin.attachment=2;assert(!h.Reserve(1,pin));const auto replacement=h.Open(2,pin);assert(replacement&&!h.book->Retire(late)&&h.book->Current(replacement));h.book->ChannelClosed(pin.channel,1);assert(h.book->Current(replacement));++groups;}
 {Harness h;auto a=h.Open(1,h.Pin("a","actual-a")),b=h.Open(2,h.Pin("b","actual-b"));auto ra=h.book->Hold(a),rb=h.book->Hold(b);h.book->ChannelClosed(h.Pin("a","actual-a").channel,1);assert(!h.book->Retire(ra)&&h.book->Physical()==1&&h.book->Current(b));h.book->HostClosed({h.host.source,2});assert(h.book->Current(b));assert(h.book->Retire(rb));++groups;}
 {Harness h;auto token=h.Open(1,h.Pin());auto r=h.book->Hold(token);h.current=false;h.book->Poll(token);assert(h.cleanup.size()==1&&!h.book->Current(token)&&h.book->Physical()==1);h.now=30002;h.book->Tick();assert(h.closes==1&&h.book->Retained()==1&&!h.book->NeedsTick());h.now=60000;h.book->Tick();assert(h.closes==1);h.book->Retire(r);h.leases.erase(1);h.book->Poll(token);assert(!h.book->Retained());++groups;}
 {Harness h;auto token=h.Reserve(1,h.Pin(),10);assert(h.book->BeginAcquire(token));h.leases.insert(1);h.book->AcquireReturned(token);h.now=10;assert(!h.book->Ready(token,true));assert(h.cleanup.size()==1&&h.book->Retained()==1);h.leases.erase(1);h.book->Poll(token);assert(!h.book->Retained());++groups;}
 {Harness h;h.now=500;assert(!h.Reserve(1,h.Pin(),500));const auto token=h.Reserve(2,h.Pin(),0);assert(token&&h.book->BeginAcquire(token));h.leases.insert(2);assert(h.book->Ready(token,true));h.book->AcquireReturned(token);h.now=100000;h.book->Tick();assert(h.book->Current(token)&&!h.book->NeedsTick());++groups;}
 {Harness h;auto pin=h.Pin();pin.context=0;assert(!h.Reserve(1,pin));pin=h.Pin();pin.unique_context.clear();assert(!h.Reserve(1,pin));pin=h.Pin();pin.channel.host.observer=2;assert(!h.Reserve(1,pin));pin=h.Pin();pin.root=true;pin.channel.session="not-root";assert(!h.Reserve(1,pin));assert(!h.Reserve(1,h.Pin(),std::numeric_limits<double>::quiet_NaN()));assert(h.book->Retained()==0);++groups;}
 {Harness h;std::vector<Scopes::Token>tokens;for(uint64_t i=1;i<=16;++i){auto token=h.Reserve(i,h.Pin(std::to_string(i),std::to_string(i)));assert(token);tokens.push_back(token);}assert(!h.Reserve(17,h.Pin("overflow","overflow"))&&h.budget->retained()==16&&h.budget->payload()<=65536);h.book->Close(tokens.back());assert(h.Reserve(17,h.Pin("overflow","overflow")));++groups;}
 {Harness h;std::vector<Scopes::Token>tokens;std::vector<Scopes::Receipt>receipts;for(uint64_t i=1;i<=4;++i){auto t=h.Open(i,h.Pin(std::to_string(i),std::to_string(i)));tokens.push_back(t);for(size_t c=0;c<32;++c){auto r=h.book->Hold(t);assert(r);receipts.push_back(r);}assert(!h.book->Hold(t));}auto extra=h.Open(5,h.Pin("extra","extra"));assert(!h.book->Hold(extra)&&h.budget->physical()==128);h.book->Retire(receipts.back());assert(h.book->Hold(extra));++groups;}
 {Harness h;auto token=h.Open(1,h.Pin());auto receipt=h.book->Hold(token);h.on_close=[&]{h.book->HostClosed(h.host);};h.book->Close(token);h.now=30002;h.book->Tick();assert(h.closes==1&&h.book->Retained()==0&&h.budget->physical()==0&&!h.book->Retire(receipt));++groups;}
 {Harness h;auto token=h.Open(1,h.Pin());h.current=false;assert(!h.book->Hold(token)&&h.cleanup.size()==1&&h.book->Retained()==1);++groups;}
 {Harness h;auto token=h.Open(1,h.Pin());h.on_cleanup=[&]{h.book->HostClosed(h.host);};h.book->Close(token);assert(h.book->Retained()==0&&h.budget->retained()==0);++groups;}
 std::cout<<"native frame debugger scope ledger "<<groups<<" groups passed\n";
}
