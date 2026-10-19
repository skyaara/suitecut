// SPDX-License-Identifier: MIT
#include "fixture_catalog.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
FixtureDeclaration D(uint64_t id,std::string name,std::vector<std::string> deps={},std::optional<Scope> scope=std::nullopt,std::optional<bool> automatic=std::nullopt){return {id,std::move(name),std::move(deps),scope,automatic};}
template<class F>void CatalogReject(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
std::vector<uint64_t> Planned(const FixtureCatalog& c,const std::vector<std::string>& names){std::vector<uint64_t> ids;for(const auto& key:FixtureGraph(c.Definitions()).Plan(c.Roots(names))){const auto found=std::find_if(c.Versions().begin(),c.Versions().end(),[&](const auto& v){return v.key==key;});assert(found!=c.Versions().end()&&found->active);ids.push_back(found->id);}return ids;}
int main(){unsigned groups=0;
 {FixtureCatalog c({{D(1,"foo")}});assert(c.Effective("foo").id==1&&c.Get(1).layer==1&&c.Get(1).previous==0&&Planned(c,{"foo"})==std::vector<uint64_t>{1});++groups;}
 {FixtureCatalog c({{D(1,"dep"),D(2,"foo"),D(3,"bar",{"foo"})},{D(4,"foo",{"foo","dep"})},{D(5,"foo",{"foo","dep"})}});assert(c.Get(4).previous==2&&c.Get(5).previous==4);assert(c.Get(3).bindings[0].declaration==5&&c.Get(4).bindings[0].declaration==2&&c.Get(5).bindings[0].declaration==4);assert(Planned(c,{"bar"})==std::vector<uint64_t>({2,1,4,5,3}));++groups;}
 {FixtureCatalog c({{D(1,"foo"),D(2,"bar"),D(3,"baz",{"foo","bar"})},{D(4,"foo",{"foo","bar"}),D(5,"bar",{"bar"})}});assert(c.Get(4).bindings[1].declaration==5&&c.Get(5).bindings[0].declaration==2);assert(Planned(c,{"baz"})==std::vector<uint64_t>({1,2,5,4,3}));++groups;}
 {FixtureCatalog c({{D(1,"foo",{},Scope::Worker,true)},{D(2,"foo",{"foo"})}});assert(c.Get(2).scope==Scope::Worker&&c.Get(2).automatic&&c.Definitions().size()==2);assert(Planned(c,{})==std::vector<uint64_t>({1,2}));++groups;}
 {FixtureCatalog c({{D(1,"foo",{},Scope::Worker,true)},{D(2,"foo",{},std::nullopt,false)}});assert(!c.Get(1).active&&!c.Get(2).automatic&&Planned(c,{}).empty());++groups;}
 {FixtureCatalog c({{D(1,"foo",{"missing"})},{D(2,"foo")}});assert(!c.Get(1).active&&c.Definitions().size()==1&&Planned(c,{"foo"})==std::vector<uint64_t>{2});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"foo",{"foo"})}});});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"foo",{"bar"}),D(2,"bar",{"foo"})}});});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"foo"),D(2,"bar",{"foo"})},{D(3,"foo",{"bar"})}});});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"foo",{},Scope::Worker)},{D(2,"foo",{},Scope::Test)}});});CatalogReject([]{FixtureCatalog c({{D(1,"foo",{},Scope::Test)},{D(2,"foo",{},Scope::Worker)}});});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"foo",{"bar"},Scope::Worker),D(2,"bar",{},Scope::Test)}});});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"foo",{"missing"})}});});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"foo"),D(2,"foo")}});});CatalogReject([]{FixtureCatalog c({{D(2,"foo")}});});CatalogReject([]{FixtureCatalog c({{D(1,"foo")},{D(1,"bar")}});});++groups;}
 {CatalogReject([]{FixtureCatalog c({{D(1,"bad name")}});});CatalogReject([]{FixtureCatalog c({{D(1,"foo",{"bad name"})}});});CatalogReject([]{FixtureCatalog c({{D(1,std::string(257,'x'))}});});++groups;}
 {CatalogReject([]{FixtureCatalog c(std::vector<FixtureCatalog::Layer>{});});CatalogReject([]{FixtureCatalog c(std::vector<FixtureCatalog::Layer>(33));});FixtureCatalog empty({{}});assert(empty.Visible().empty()&&empty.Definitions().empty());++groups;}
 {std::vector<FixtureDeclaration> defs;for(uint64_t i=1;i<=256;++i)defs.push_back(D(i,"v"+std::to_string(i)));FixtureCatalog full({defs});assert(full.Definitions().size()==256);defs.push_back(D(257,"overflow"));CatalogReject([&]{FixtureCatalog c({defs});});++groups;}
 {std::vector<FixtureDeclaration> defs;for(uint64_t i=1;i<=17;++i)defs.push_back(D(i,"v"+std::to_string(i),std::vector<std::string>(256,"base")));defs.push_back(D(18,"base"));CatalogReject([&]{FixtureCatalog c({defs});});++groups;}
 {FixtureCatalog c({{D(1,"base"),D(2,"foo",{"base","base"})}});assert(c.Get(2).bindings.size()==1&&Planned(c,{"foo","foo"})==std::vector<uint64_t>({1,2}));CatalogReject([&]{(void)c.Roots({"missing"});});CatalogReject([&]{(void)c.Get(0);});CatalogReject([&]{(void)c.Get(3);});++groups;}
 {FixtureCatalog c({{D(1,"worker",{},Scope::Worker),D(2,"test",{"worker"})},{D(3,"worker",{"worker"})}});const auto requested=c.Roots({"test"});std::vector<FixtureOwners::Event> events;bool body=false,finished=false;FixtureOwners owner(FixtureGraph(c.Definitions()),{[&](const auto& e){events.push_back(e);},[&](auto,auto){body=true;},[&](auto,auto,bool ok,const auto&){assert(ok);finished=true;},[](const auto&){assert(false);}});assert(owner.Begin(1,1,requested,100,0));size_t cursor=0;double now=1;while(!body){assert(cursor<events.size()&&events[cursor].kind==FixtureOwners::EventKind::Setup);const auto event=events[cursor++];owner.Ready(event.token,event.test,event.generation,now++);}assert(owner.State().physical==3&&owner.State().workerFixtures==2);owner.Finish(true,"",now++);while(!finished){assert(cursor<events.size());const auto e=events[cursor++];assert(e.kind==FixtureOwners::EventKind::ResumeTeardown);owner.Settled(e.token,e.test,e.generation,true,"",now++);}assert(owner.State().physical==2&&owner.State().workerFixtures==2);owner.StopWorker(now++);while(owner.State().physical){assert(cursor<events.size());const auto e=events[cursor++];assert(e.kind==FixtureOwners::EventKind::ResumeTeardown);owner.Settled(e.token,e.test,e.generation,true,"",now++);}assert(!owner.State().physical);++groups;}
 {FixtureCatalog c({{D(1,"first",{},Scope::Worker,true),D(2,"second",{},Scope::Worker,true)},{D(3,"first")}});assert(Planned(c,{})==std::vector<uint64_t>({3,2})&&!c.Get(1).active);++groups;}
 std::cout<<"PASS "<<groups<<" native fixture-layer/version/graph/owned-callback controls (controlled receipts, no OS/SDK fixture claim)\n";
}
