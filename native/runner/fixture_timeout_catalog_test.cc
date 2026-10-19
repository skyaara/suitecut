// SPDX-License-Identifier: MIT
#include "fixture_timeout_catalog.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
static FixtureDeclaration D(uint64_t id, std::string name, std::vector<std::string> deps = {}) {
  return {id, std::move(name), std::move(deps), std::nullopt, std::nullopt};
}
template<class Fn> static void Reject(Fn fn) { bool rejected=false; try {fn();} catch(const std::exception&) {rejected=true;} assert(rejected); }
int main() {
  unsigned n=0;
  {FixtureCatalog graph({{D(1,"foo")}});FixtureTimeoutCatalog c(graph,{});assert(!c.Get(1).timeout && !c.Get(1).origin);++n;}
  {FixtureCatalog graph({{D(1,"foo")}});FixtureTimeoutCatalog c(graph,{{1,0}});assert(c.Get(1).timeout==0 && c.Get(1).origin==1);++n;}
  {FixtureCatalog graph({{D(1,"foo")},{D(2,"foo",{"foo"})}});FixtureTimeoutCatalog c(graph,{{1,1000}});assert(c.Get(2).timeout==1000 && c.Get(2).origin==1 && c.Get(2).declaration==2);assert(c.Get(1).timeout==1000);++n;}
  {FixtureCatalog graph({{D(1,"foo")},{D(2,"foo")},{D(3,"foo")}});FixtureTimeoutCatalog c(graph,{{1,1000},{2,0}});assert(c.Get(3).timeout==0 && c.Get(3).origin==2 && c.Get(3).declaration==3);++n;}
  {FixtureCatalog graph({{D(1,"foo"),D(2,"bar")},{D(3,"foo")}});FixtureTimeoutCatalog c(graph,{{1,1000},{2,500},{3,100}});assert(c.Resolve(graph.Effective("foo").key).timeout==100 && c.Get(2).timeout==500);assert(c.Get(1).timeout==1000);++n;}
  {FixtureCatalog graph({{D(1,"foo")}});Reject([&]{FixtureTimeoutCatalog c(graph,{{1,100},{1,200}});});++n;}
  {FixtureCatalog graph({{D(1,"foo")}});Reject([&]{FixtureTimeoutCatalog c(graph,{{0,100}});});Reject([&]{FixtureTimeoutCatalog c(graph,{{2,100}});});++n;}
  {FixtureCatalog graph({{D(1,"foo")}});Reject([&]{FixtureTimeoutCatalog c(graph,{{1,120001}});});FixtureTimeoutCatalog c(graph,{{1,120000}});assert(c.Get(1).timeout==120000);++n;}
  {FixtureCatalog graph({{D(1,"foo")}});FixtureTimeoutCatalog c(graph,{});Reject([&]{(void)c.Resolve("foo");});Reject([&]{(void)c.Resolve("nativeFixture2");});Reject([&]{(void)c.Get(0);});++n;}
  {FixtureCatalog graph({{D(1,"foo",{"missing"})},{D(2,"foo")}});FixtureTimeoutCatalog c(graph,{{1,500}});assert(!graph.Get(1).active && c.Get(2).timeout==500 && c.Get(2).origin==1);++n;}
  assert(n==10);std::cout<<"PASS "<<n<<" controlled native fixture timeout catalog/version provenance cases; no host/process claim\n";
}
