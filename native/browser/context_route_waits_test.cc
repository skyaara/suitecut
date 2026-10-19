// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <iostream>
#include "context_routes.h"
using Routes=suitecut::NativeContextRoutes;
using Policy=suitecut::NativeContextRouteRegistration;
using Leases=suitecut::NativeRouteCallbackLeases;
struct Target {
  struct Sent{Routes::Mutation kind;Routes::Route route;Routes::Result done;};std::deque<Sent>sent;
  Routes::Apply apply(){return [this](auto kind,auto route,double,auto done){sent.push_back({kind,std::move(route),std::move(done)});};}
  void Ack(){assert(!sent.empty());auto done=std::move(sent.front().done);sent.pop_front();done(true,"");}
};
int main(){
 double now=1;auto&leases=suitecut::GlobalRouteCallbackLeases();
 {
  Routes routes([](const auto&,const auto&){assert(false);},[&]{return now;});assert(routes.Seed(1));Target page,broker;
  assert(routes.Attach(1,"page",page.apply(),[](bool ok,const auto&){assert(ok);}));assert(routes.Attach(1,"broker",broker.apply(),[](bool ok,const auto&){assert(ok);}));
  auto policy=Policy::Create("original","**/*",0);assert(routes.Install(1,1,policy,100,[](bool ok,const auto&){assert(ok);}));page.Ack();broker.Ack();
  const auto source=*leases.Scope(),child=*leases.Scope(),running=*leases.Issue(source,"actual-running",policy),queued=*leases.Issue(child,"actual-queued",policy);assert(leases.Start(running,source,"actual-running"));
  std::vector<bool>receipts;int delivered=0;assert(routes.RemoveAllObserved(2,1,99,true,100,[&](bool applied,const auto&ids){assert(ids==std::vector<std::string>{"original"});receipts.push_back(applied);},[&](bool ok,const auto&){assert(ok);++delivered;}));
  assert(receipts==std::vector<bool>{false}&&routes.physical()==2&&delivered==0);page.Ack();assert(delivered==0);broker.Ack();assert(receipts==std::vector<bool>({false,true})&&routes.pending()==1&&routes.physical()==0);
  // The old callback wait cannot hold the native policy mutation lock.
  auto future=Policy::Create("future","**/*",0);assert(routes.Install(3,1,future,100,[](bool ok,const auto&){assert(ok);}));page.Ack();broker.Ack();
  const auto later=*leases.Issue(source,"not-in-snapshot",future);assert(leases.Start(later,source,"not-in-snapshot"));
  leases.Close(child);assert(!leases.Start(queued,child,"actual-queued"));routes.Tick(now);assert(delivered==0);
  assert(leases.Finish(running,source,"actual-running"));routes.Tick(now);assert(delivered==1&&routes.pending()==0&&leases.running()==1);assert(leases.Finish(later,source,"not-in-snapshot"));
 }
 {
  Routes routes([](const auto&,const auto&){assert(false);},[&]{return now;});assert(routes.Seed(1));auto exhausted=Policy::Create("exhausted","**/*",1);
  assert(routes.Install(1,1,exhausted,100,[](bool ok,const auto&){assert(ok);}));bool last=false;assert(exhausted->Consume(last)&&last);const auto scope=*leases.Scope(),token=*leases.Issue(scope,"exhausted-callback",exhausted);assert(leases.Start(token,scope,"exhausted-callback"));
  assert(routes.Install(2,1,Policy::Create("current","**/*",0),100,[](bool ok,const auto&){assert(ok);})); // Prunes the exhausted registration from current policy.
  int done=0;assert(routes.RemoveAllObserved(3,1,7,true,100,[](bool,const auto&ids){assert(ids==std::vector<std::string>({"current","exhausted"}));},[&](bool ok,const auto&){assert(ok);++done;}));assert(done==0&&routes.pending()==1);
  assert(!routes.Install(3,1,Policy::Create("id-collision","**/*",0),100,[](bool,const auto&){assert(false);}));
  assert(leases.Finish(token,scope,"exhausted-callback"));routes.Tick(now);assert(done==1&&routes.pending()==0);
 }
 {
  Routes routes([](const auto&,const auto&){assert(false);},[&]{return now;});assert(routes.Seed(1));Target page;assert(routes.Attach(1,"page",page.apply(),[](bool ok,const auto&){assert(ok);}));auto policy=Policy::Create("deadline","**/*",0);assert(routes.Install(1,1,policy,100,[](bool ok,const auto&){assert(ok);}));page.Ack();const auto scope=*leases.Scope(),token=*leases.Issue(scope,"running",policy);assert(leases.Start(token,scope,"running"));
  int done=0,applied=0;assert(routes.RemoveAllObserved(2,1,1,true,10,[&](bool ack,const auto&){applied+=ack;},[&](bool ok,const auto&error){assert(!ok&&error.find("deadline")!=std::string::npos);++done;}));now=10;page.Ack();assert(done==1&&applied==0&&routes.pending()==0&&leases.running()==1);assert(leases.Finish(token,scope,"running"));now=1;
 }
 {
  Routes routes([](const auto&,const auto&){assert(false);},[&]{return now;});assert(routes.Seed(1));Target page;assert(routes.Attach(1,"page",page.apply(),[](bool ok,const auto&){assert(ok);}));auto policy=Policy::Create("closed","**/*",0);assert(routes.Install(1,1,policy,100,[](bool ok,const auto&){assert(ok);}));page.Ack();const auto scope=*leases.Scope(),token=*leases.Issue(scope,"running",policy);assert(leases.Start(token,scope,"running"));
  int done=0;assert(routes.RemoveAllObserved(2,1,1,true,100,[](bool,const auto&){},[&](bool ok,const auto&error){assert(!ok&&error.find("closed")!=std::string::npos);++done;}));page.Ack();routes.Closed(1);assert(done==1&&routes.pending()==0&&leases.running()==1);leases.Close(scope);assert(leases.running()==1);assert(leases.Finish(token,scope,"running"));
 }
 {
  Routes routes([](const auto&,const auto&){assert(false);},[&]{return now;});assert(routes.Seed(1));Target page;assert(routes.Attach(1,"page",page.apply(),[](bool ok,const auto&){assert(ok);}));auto policy=Policy::Create("cancelled","**/*",0);assert(routes.Install(1,1,policy,100,[](bool ok,const auto&){assert(ok);}));page.Ack();const auto scope=*leases.Scope(),token=*leases.Issue(scope,"running",policy);assert(leases.Start(token,scope,"running"));
  assert(routes.RemoveAllObserved(2,1,1,true,100,[](bool,const auto&){},[](bool,const auto&){assert(false);}));routes.Cancel(2);assert(routes.physical()==1&&routes.pending()==0);
  int done=0;assert(routes.RemoveAllObserved(2,1,2,true,100,[](bool,const auto&){},[&](bool ok,const auto&){assert(ok);++done;}));page.Ack();routes.Tick(now);assert(done==0&&routes.physical()==0);assert(leases.Finish(token,scope,"running"));routes.Tick(now);assert(done==1);
 }
 {
  auto routes=std::make_unique<Routes>([](const auto&,const auto&){assert(false);},[&]{return now;});assert(routes->Seed(1));auto policy=Policy::Create("reentrant","**/*",0);assert(routes->Install(1,1,policy,100,[](bool ok,const auto&){assert(ok);}));const auto scope=*leases.Scope(),token=*leases.Issue(scope,"running",policy);assert(leases.Start(token,scope,"running"));int done=0;
  assert(routes->RemoveAllObserved(2,1,1,true,100,[&](bool applied,const auto&){assert(!applied);routes.reset();},[&](bool ok,const auto&){assert(!ok);++done;}));assert(!routes&&done==1&&leases.running()==1);assert(leases.Finish(token,scope,"running"));
 }
 {
  Routes routes([](const auto&,const auto&){assert(false);},[&]{return now;});assert(routes.Seed(1));std::vector<Routes::Route>policies;
  for(int i=0;i<128;++i){auto p=Policy::Create(std::to_string(i)+std::string(125,'\1'),"**/*",0);assert(p);assert(routes.Install(i+1,1,p,100,[](bool ok,const auto&){assert(ok);}));policies.push_back(p);}
  assert(!routes.RemoveAllObserved(200,1,1,true,100,[](bool,const auto&){assert(false);},[](bool,const auto&){assert(false);}));assert(routes.pending()==0);for(const auto&p:policies)assert(p->available());
 }
 assert(leases.size()==0&&Policy::retained_count()==0);
 std::cout<<"Native context route removal waits: 7 actual-receipt/admission/owner/deadline groups passed\n";
}
