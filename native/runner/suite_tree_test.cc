// SPDX-License-Identifier: MIT
#include "suite_tree.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
template<class F>void Reject(F callback){bool failed=false;try{callback();}catch(const std::runtime_error&){failed=true;}assert(failed);}
int main(){
  using K=sr::SuiteHookKind;
  const std::vector<sr::SuiteDeclaration> suites={
    {3,2,1,{{K::BeforeAll,20},{K::BeforeEach,21},{K::AfterEach,22},{K::AfterEach,23},{K::AfterAll,24}}},
    {1,0,0,{{K::BeforeAll,1},{K::BeforeEach,2},{K::AfterEach,3},{K::AfterAll,4}}},
    {4,1,3,{{K::BeforeEach,31},{K::AfterEach,32}}},
    {2,1,1,{{K::BeforeAll,10},{K::BeforeEach,11},{K::AfterEach,12},{K::AfterAll,13}}}};
  const std::vector<sr::SuiteTestDeclaration> tests={{31,3,0},{41,4,0},{22,2,2},{12,1,2},{21,2,0},{11,1,0}};
  sr::NativeSuiteTree tree(suites,tests);
  assert((tree.TestOrder()==std::vector<uint64_t>{11,21,31,22,12,41}));
  auto routes=tree.Plan({31,41});assert(routes.size()==2);
  assert((routes[0].ancestors==std::vector<uint64_t>{1,2,3}));
  assert((routes[0].before_each==std::vector<uint64_t>{2,11,21}));
  assert((routes[0].after_each==std::vector<uint64_t>{22,23,12,3}));
  assert((routes[0].finish_suites==std::vector<uint64_t>{3,2}));
  assert((routes[1].finish_suites==std::vector<uint64_t>{4,1}));
  assert((tree.Hooks(2,K::BeforeAll)==std::vector<uint64_t>{10}));
  assert((tree.SuiteTests(2)==std::set<uint64_t>{21,22,31}));Reject([&]{tree.SuiteTests(999);});
  assert(tree.Plan({}).empty());Reject([&]{tree.Plan({999});});
  assert(!tree.ResolveModifiers({}, {}, {}).has_focus);
  const auto outer_focus=tree.ResolveModifiers({{2,{true,{}}}}, {}, {});
  assert(outer_focus.has_focus&&(outer_focus.focused==std::vector<uint64_t>{21,31,22}));
  const auto narrow_focus=tree.ResolveModifiers({{2,{true,{}}},{4,{true,{}}}}, {31}, {});
  assert((narrow_focus.focused==std::vector<uint64_t>{31,41}));
  const auto nested_focus=tree.ResolveModifiers({{2,{true,{}}},{3,{true,{}}}}, {}, {});
  assert((nested_focus.focused==std::vector<uint64_t>{31}));
  const auto reasons=tree.ResolveModifiers({{2,{false,std::string("outer")}}, {3,{false,std::string("")}}}, {}, {{21,"test"}});
  assert(!reasons.has_focus&&reasons.skipped.size()==3&&reasons.skipped.at(21)=="test"&&reasons.skipped.at(31).empty()&&reasons.skipped.at(22)=="outer");
  sr::NativeSuiteTree empty_focus({{1,0,0,{}},{2,1,1,{}}},{{1,1,0}});
  const auto empty=empty_focus.ResolveModifiers({{2,{true,{}}}}, {}, {});
  assert(empty.has_focus&&empty.focused.empty());
  Reject([&]{tree.ResolveModifiers({{999,{true,{}}}}, {}, {});});
  Reject([&]{tree.ResolveModifiers({}, {999}, {});});
  Reject([&]{tree.ResolveModifiers({}, {}, {{999,"invalid"}});});
  Reject([]{sr::NativeSuiteTree value({{0,0,0,{}}},{});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{}},{1,0,0,{}}},{});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{}},{2,999,0,{}}},{});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{}},{2,3,0,{}},{3,2,0,{}}},{});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{}}},{{1,1,0},{2,1,0}});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{}}},{{1,1,UINT32_MAX}});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{}}},{{0,1,0}});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{}}},{{1,2,0}});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{{K::BeforeAll,1},{K::AfterAll,1}}}},{});});
  Reject([]{sr::NativeSuiteTree value({{1,0,0,{{static_cast<K>(77),1}}}},{});});
  std::vector<sr::SuiteDeclaration> deep;for(uint64_t id=1;id<=65;++id)deep.push_back({id,id-1,0,{}});
  Reject([&]{sr::NativeSuiteTree value(deep,{});});
  std::vector<sr::SuiteTestDeclaration> maximum;for(uint64_t id=1;id<=8192;++id)maximum.push_back({id,1,static_cast<uint32_t>(id-1)});
  sr::NativeSuiteTree full({{1,0,0,{}}},maximum);assert(full.TestOrder().size()==8192);assert(full.Plan({1,8192}).size()==2);
  maximum.push_back({8193,1,8192});Reject([&]{sr::NativeSuiteTree value({{1,0,0,{}}},maximum);});
  sr::SuiteDeclaration expensive{1,0,0,{}};
  for(uint64_t id=1;id<=4096;++id)expensive.hooks.push_back({K::BeforeEach,id});
  maximum.resize(1024);std::set<uint64_t> selection;for(const auto& test:maximum)selection.insert(test.id);
  sr::NativeSuiteTree bounded({expensive},maximum);Reject([&]{bounded.Plan(selection);});
  std::cout<<"PASS native hierarchy order, ancestor hooks, selected branch lifetimes and invalid/bounded declarations\n";
}
