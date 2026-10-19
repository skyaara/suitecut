// SPDX-License-Identifier: MIT
#include "fixture_graph.h"
#include <cassert>
#include <iostream>
namespace suitecut::runner { struct FixtureOwnersTestAccess { static void Exhaust(FixtureOwners& owner){owner.nonce_=std::numeric_limits<uint64_t>::max();} }; }
using namespace suitecut::runner;
struct Harness {
 std::vector<FixtureOwners::Event> events;std::vector<bool> results;std::vector<std::string> errors;int ready=0,killed=0;
 FixtureOwners manager;
 explicit Harness(std::vector<Fixture> defs):manager(FixtureGraph(std::move(defs)),{
  [this](const auto& e){events.push_back(e);},[this](auto,auto){++ready;},
  [this](auto,auto,bool ok,const auto& error){results.push_back(ok);errors.push_back(error);},
  [this](const auto&){++killed;}}){}
 void Ready(){auto e=events.back();assert(e.kind==FixtureOwners::EventKind::Setup);manager.Ready(e.token,e.test,e.generation,0);}
 void Settle(bool ok=true){auto e=events.back();assert(e.kind!=FixtureOwners::EventKind::Setup);manager.Settled(e.token,e.test,e.generation,ok,ok?"":"actual teardown exception",1);}
};
int main(){
 { FixtureGraph g({{"automaticTest",Scope::Test,{},true},{"automaticWorker",Scope::Worker,{},true}});assert(g.Plan({})==std::vector<std::string>({"automaticWorker","automaticTest"})); }
 { FixtureGraph g({});bool rejected=false;try{g.Plan(std::vector<std::string>(257,"x"));}catch(const std::invalid_argument&){rejected=true;}assert(rejected); }
 auto invalid=[](std::vector<Fixture> defs){try{FixtureGraph graph(std::move(defs));(void)graph;return false;}catch(const std::invalid_argument&){return true;}};
 assert(invalid({{"x",Scope::Test,{"missing"}}}));assert(invalid({{"x",Scope::Test,{"y"}},{"y",Scope::Test,{"x"}}}));
 assert(invalid({{"x",Scope::Worker,{"y"}},{"y",Scope::Test,{}}}));assert(invalid({{"x",Scope::Worker,{}},{"x",Scope::Test,{}}}));
 {
 Harness h({{"worker",Scope::Worker,{}},{"automatic",Scope::Test,{"worker"},true},{"page",Scope::Test,{"worker"}}});
 assert(h.manager.Begin(1,10,{"page"},100,0));assert(h.events.back().fixture=="worker");h.Ready();assert(h.events.back().fixture=="automatic");h.Ready();assert(h.events.back().fixture=="page");h.Ready();assert(h.ready==1&&h.manager.State().physical==3);
 h.manager.Finish(true,"",1);assert(h.events.back().fixture=="page");assert(!h.manager.Begin(2,20,{},100,1));h.Settle();assert(h.events.back().fixture=="automatic");h.Settle();assert(h.results==std::vector<bool>{true});assert(h.manager.State().physical==1);
 assert(h.manager.Begin(2,20,{"page"},100,2));assert(h.events.back().fixture=="automatic");h.Ready();h.Ready();assert(h.ready==2);h.manager.Finish(true,"",3);h.Settle();h.Settle();h.manager.StopWorker(4);assert(h.events.back().fixture=="worker");h.Settle();assert(h.manager.State().physical==0);assert(!h.manager.Begin(3,30,{},100,5));
 }
 {
 Harness h({{"database",Scope::Test,{}}});assert(h.manager.Begin(1,2,{"database"},10,0));auto e=h.events.back();
 h.manager.Ready(e.token,999,e.generation,1);assert(h.ready==0); // exact owner/generation required
 h.manager.Tick(10);assert(h.events.back().kind==FixtureOwners::EventKind::CancelSetup);assert(h.manager.State().physical==1&&h.results.empty());
 h.manager.Tick(1009);assert(h.killed==0);h.manager.Tick(1010);assert(h.killed==1&&h.manager.State().physical==1);h.manager.Tick(2000);assert(h.killed==1);
 h.manager.WorkerClosed();assert(h.manager.State().physical==0&&h.results==std::vector<bool>{false});h.manager.Settled(e.token,e.test,e.generation,true,"",2001);assert(h.results.size()==1);
 }
 {
 Harness h({{"first",Scope::Test,{}},{"second",Scope::Test,{"first"}}});assert(h.manager.Begin(1,2,{"second"},100,0));h.Ready();h.Ready();h.manager.Finish(true,"",1);h.Settle(false);assert(h.events.back().fixture=="first");h.Settle();assert(h.results==std::vector<bool>{false});assert(h.errors[0].find("second teardown failed")!=std::string::npos);assert(!h.manager.Begin(2,3,{},100,2));
 }
 {
 Harness h({{"fixture",Scope::Test,{}}});for(uint64_t n=1;n<=10000;++n){assert(h.manager.Begin(n,n,{"fixture"},100,0));h.Ready();h.manager.Finish(true,"",1);h.Settle();assert(h.manager.State().physical==0);}assert(h.results.size()==10000);
 }
 {
 Harness h({{"fixture",Scope::Test,{}}});assert(!h.manager.Begin(1,1,{"fixture"},std::numeric_limits<double>::quiet_NaN(),0));assert(!h.manager.Begin(1,1,{"fixture"},100,std::numeric_limits<double>::quiet_NaN()));assert(!h.manager.Begin(1,1,{"fixture"},std::numeric_limits<double>::infinity(),0));assert(h.events.empty());
 assert(h.manager.Begin(1,1,{"fixture"},10,0));auto e=h.events.back();h.manager.Ready(e.token,e.test,e.generation,10);assert(h.ready==0);assert(h.events.back().kind==FixtureOwners::EventKind::CancelSetup);assert(h.manager.State().physical==1);h.Settle();assert(h.results==std::vector<bool>{false});assert(h.errors[0]=="Test original deadline expired");
 }
 {
 Harness h({{"fixture",Scope::Test,{}}});assert(h.manager.Begin(1,1,{"fixture"},10,0));h.Ready();assert(h.ready==1);h.manager.Finish(true,"",10);assert(h.manager.State().physical==1);h.Settle();assert(h.results==std::vector<bool>{false});assert(h.errors[0]=="Test original deadline expired");
 }
 {
 Harness h({{"fixture",Scope::Test,{}}});assert(h.manager.Begin(1,1,{"fixture"},10,0));const auto e=h.events.back();h.manager.Settled(e.token,e.test,e.generation,false,"genuine late setup exception",10);assert(h.results==std::vector<bool>{false});assert(h.errors[0]=="Test original deadline expired");assert(h.manager.State().physical==0);
 }
 {
 Harness h({{"fixture",Scope::Test,{}}});FixtureOwnersTestAccess::Exhaust(h.manager);assert(h.manager.Begin(1,1,{"fixture"},100,0));assert(h.events.empty()&&h.ready==0&&h.results==std::vector<bool>{false});assert(h.errors[0]=="Fixture callback token space exhausted");
 }
 std::cout<<"Fixture graph/native owner tests passed\n";
}
