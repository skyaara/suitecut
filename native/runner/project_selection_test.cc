// SPDX-License-Identifier: MIT
#include "project_selection.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class Call>void Rejected(Call call){bool rejected=false;try{call();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
int main(){
  for(const auto& pattern:std::vector<std::string>{"",std::string(257,'a'),std::string("a\0b",3)})Rejected([&]{ProjectPattern value(pattern);});
  assert(ProjectPattern("SUite2").Matches("suite2"));assert(ProjectPattern("*oj*t-Na*e").Matches("project-name"));
  assert(ProjectPattern("**a**b***").Matches("AzzB"));assert(ProjectPattern("*").Matches(""));
  assert(!ProjectPattern("a").Matches("aa"));assert(!ProjectPattern("a?b").Matches("axb"));assert(ProjectPattern("a?b").Matches("a?b"));
  assert(ProjectPattern("[a].(b)+$").Matches("[A].(B)+$"));assert(ProjectPattern("π*").Matches("πx"));assert(!ProjectPattern("π*").Matches("Πx"));
  assert(ProjectPattern(std::string(256,'A')).Matches(std::string(256,'a')));assert(!ProjectPattern("*").Matches(std::string(257,'a')));
  // Independent small whole-language oracle for the bounded NFA's overlapping
  // star paths; deliberately uses a recursive recognizer, not its bitset logic.
  const auto oracle=[](auto&& self,std::string_view pattern,std::string_view name)->bool {
    if(pattern.empty())return name.empty();
    if(pattern[0]=='*'){for(size_t n=0;n<=name.size();++n)if(self(self,pattern.substr(1),name.substr(n)))return true;return false;}
    return !name.empty()&&pattern[0]==name[0]&&self(self,pattern.substr(1),name.substr(1));
  };
  for(size_t n=1;n<=5;++n){size_t total=1;for(size_t i=0;i<n;++i)total*=3;
    for(size_t code=0;code<total;++code){auto cursor=code;std::string pattern;for(size_t i=0;i<n;++i){pattern+="ab*"[cursor%3];cursor/=3;}ProjectPattern actual(pattern);
      for(size_t size=0;size<=5;++size)for(size_t bits=0;bits<(size_t{1}<<size);++bits){std::string name;for(size_t i=0;i<size;++i)name+=((bits>>i)&1)?'a':'b';assert(actual.Matches(name)==oracle(oracle,pattern,name));}
    }
  }
  const ProjectGraph graph({{"body",{"setup"}},{"setup",{},"cleanup"},{"cleanup"},{"manual",{},std::nullopt,false}});
  const std::vector<ProjectTestDeclaration> tests{{11,0},{12,0},{21,1},{22,1},{31,2},{41,3}};
  auto select=[&](TestSelection filter={},std::vector<std::string> patterns={},bool no_deps=false,std::vector<uint64_t> focus={},std::vector<size_t> suites={}){return SelectProjectTests(graph,tests,filter,patterns,no_deps,focus,suites);};
  assert((graph.Roots({"B*","SETUP","b*"})==std::vector<bool>{true,true,false,false}));
  assert((graph.Roots({"missing*","body"})==std::vector<bool>{true,false,false,false}));
  Rejected([&]{graph.Roots({"missing*"});});Rejected([&]{graph.Roots({"missing","body"});});
  {const auto value=select();assert((value.tests==std::vector<std::vector<uint64_t>>{{11,12},{21,22},{31},{}}));assert((value.roles==std::vector<std::string>{"primary","dependency","teardown","notSelected"}));}
  {TestSelection filter;filter.ids={12};const auto value=select(filter,{"body"});assert((value.tests==std::vector<std::vector<uint64_t>>{{12},{21,22},{31},{}}));}
  for(uint32_t shard=1;shard<=2;++shard){TestSelection filter;filter.shard_current=shard;filter.shard_total=2;const auto value=select(filter);assert((value.tests[0]==std::vector<uint64_t>{shard==1?11U:12U}));assert((value.tests[1]==std::vector<uint64_t>{21,22}));assert((value.tests[2]==std::vector<uint64_t>{31}));}
  {TestSelection filter;filter.shard_current=3;filter.shard_total=3;const auto value=select(filter);assert((value.plan.selected==std::vector<bool>{false,false,false,false}));}
  {TestSelection filter;filter.ids={22};const auto value=select(filter);assert((value.tests==std::vector<std::vector<uint64_t>>{{},{22},{31},{}}));assert(value.roles[1]=="primary");}
  {TestSelection filter;filter.ids={41};const auto value=select(filter,{"body"});assert((value.plan.selected==std::vector<bool>{false,false,false,false}));}
  {const auto value=select({}, {"body"},false,{21});assert((value.tests[0]==std::vector<uint64_t>{11,12}));assert((value.tests[1]==std::vector<uint64_t>{21,22}));}
  {const auto value=select({}, {},false,{12});assert((value.tests[0]==std::vector<uint64_t>{12}));assert((value.tests[1]==std::vector<uint64_t>{21,22}));}
  {const auto value=select({}, {"body"},false,{}, {1});assert((value.tests[0]==std::vector<uint64_t>{11,12}));}
  {const auto value=select({}, {"body"},false,{}, {0});assert((value.plan.selected==std::vector<bool>{false,false,false,false}));}
  {const auto value=select({}, {"body"},true);assert((value.plan.selected==std::vector<bool>{true,false,false,false}));assert(value.plan.no_dependencies);}
  {const auto value=select({}, {},true);assert((value.roles==std::vector<std::string>{"primary","primary","notSelected","notSelected"}));}
  {const auto value=select({}, {"cleanup"},true);assert((value.roles==std::vector<std::string>{"notSelected","notSelected","primary","notSelected"}));assert((value.tests[2]==std::vector<uint64_t>{31}));}
  {TestSelection filter;filter.ids={12};filter.shard_current=1;filter.shard_total=2;const auto value=select(filter,{"BODY"},true);assert((value.tests==std::vector<std::vector<uint64_t>>{{12},{},{},{}}));}
  {TestSelection filter;filter.ids={999};Rejected([&]{select(filter);});filter.ids={11,11};Rejected([&]{select(filter);});filter.ids={};filter.forbid_only=true;Rejected([&]{select(filter,{"body"},false,{21});});Rejected([&]{select(filter,{"body"},false,{}, {1});});}
  {auto bad=tests;bad[0].project=4;Rejected([&]{SelectProjectTests(graph,bad);});}
  {const ProjectGraph empty({{"body",{"setup"}},{"setup"}});const auto value=SelectProjectTests(empty,{{1,0}});assert((value.plan.selected==std::vector<bool>{true,true}));assert(value.tests[1].empty());}
  {const ProjectGraph empty({{"empty"}});assert(SelectProjectTests(empty,{}).plan.selected[0]);TestSelection shard;shard.shard_total=2;assert(!SelectProjectTests(empty,{},shard).plan.selected[0]);}
  {const std::vector<SuiteDeclaration> suites{{1,0,0,{}},{2,1,0,{}},{3,1,1,{}},{4,3,0,{}}};assert((ProjectSuiteFocus(2,suites,{{4,{true,std::nullopt}}})==std::vector<size_t>{1}));assert((ProjectSuiteFocus(2,suites,{{1,{true,std::nullopt}}})==std::vector<size_t>{0,1}));Rejected([&]{ProjectSuiteFocus(3,suites,{});});}
  {SerialProjectPlan plan(graph,{"body","setup"},true);auto admission=plan.Next();assert(admission&&admission->index==0);assert(plan.WorkerStarted(admission->generation,901));assert(plan.Complete(admission->generation,SerialProjectPlan::Outcome::Failed));assert(!plan.Next());assert(plan.WorkerExited(admission->generation,901));admission=plan.Next();assert(admission&&admission->index==1);}
  {SerialProjectPlan plan(graph,ProjectPlanSelection{{true,false,false,false},true});assert(plan.Next()->index==0);Rejected([&]{SerialProjectPlan bad(graph,ProjectPlanSelection{{true,false,false,false},false});});}
  std::cout<<"Native project selector/composition controls passed\n";
}
