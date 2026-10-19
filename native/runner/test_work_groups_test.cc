// SPDX-License-Identifier: MIT
#include "test_work_groups.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void Reject(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}assert(rejected);}
static std::vector<SuiteDeclaration> Suites(){return {{1,0,0,{}},{2,1,0,{}},{3,1,1,{}},{4,2,1,{}}};}
static std::vector<SuiteTestDeclaration> Members(){return {{1,2,0},{2,4,0},{3,4,1},{4,2,2},{5,3,0},{6,3,1},{7,1,2}};}
static std::vector<WorkGroupTest> Tests(){return {{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0}};}
static TestWorkGroups Plan(std::map<uint64_t,WorkGroupMode> modes={},bool parallel=true,std::vector<uint64_t> ids={1,2,3,4,5,6,7}){
  return {Tests(),Suites(),Members(),std::move(modes),{true},{std::move(ids)},{parallel}};
}
int main(){unsigned controls=0;
 {const auto p=Plan();assert(p.Groups().size()==7);for(size_t i=0;i<7;++i)assert(p.Groups()[i].tests==std::vector<uint64_t>{i+1});++controls;}
 {const auto p=Plan({},false);assert(p.Groups().size()==1&&p.Groups()[0].tests==std::vector<uint64_t>({1,2,3,4,5,6,7}));++controls;}
 {const auto p=Plan({{2,WorkGroupMode::Serial},{4,WorkGroupMode::Serial}});assert(p.Groups().size()==4&&p.Groups()[0].mode==WorkGroupMode::Serial&&p.Groups()[0].suite==2&&p.Groups()[0].tests==std::vector<uint64_t>({1,2,3,4}));++controls;}
 {const auto p=Plan({{2,WorkGroupMode::Default}});assert(p.Groups().size()==4&&p.Groups()[0].mode==WorkGroupMode::Default&&p.Groups()[0].suite==2&&p.Groups()[0].tests==std::vector<uint64_t>({1,2,3,4}));++controls;}
 {const auto p=Plan({{2,WorkGroupMode::Default},{4,WorkGroupMode::Parallel}});assert(p.Groups().size()==6&&p.Groups()[0].tests==std::vector<uint64_t>({1,4})&&p.Groups()[1].tests==std::vector<uint64_t>{2}&&p.Groups()[2].tests==std::vector<uint64_t>{3});++controls;}
 {const auto p=Plan({{2,WorkGroupMode::Serial}},false);assert(p.Groups().size()==1&&p.Groups()[0].tests.size()==7);++controls;}
 {const auto p=Plan({{2,WorkGroupMode::Serial},{3,WorkGroupMode::Parallel}},false);assert(p.Groups().size()==3&&p.Groups()[0].tests==std::vector<uint64_t>({1,2,3,4,7}));++controls;}
 {const auto p=Plan({{2,WorkGroupMode::Serial}},true,{3,5,2});assert(p.Groups().size()==2&&p.Groups()[0].tests==std::vector<uint64_t>({2,3})&&p.Groups()[1].tests==std::vector<uint64_t>{5});++controls;}
 {auto tests=Tests();for(auto& t:tests)if(t.id>=5)t.project=1;const TestWorkGroups p(tests,Suites(),Members(),{},{true,true},{{1,2,3,4},{5,6,7}},{false,true});assert(p.Groups().size()==4&&p.ByProject()[0].size()==1&&p.ByProject()[1].size()==3);for(const auto i:p.ByProject()[1])assert(p.Groups()[i].project==1);++controls;}
 {const TestWorkGroups p({{5,0},{1,0},{2,0}},{},{},{},{true},{{1,5}},{false});assert(p.Groups().size()==1&&p.Groups()[0].tests==std::vector<uint64_t>({5,1}));++controls;}
 {const TestWorkGroups p({{1,0}},{},{},{},{false},{{}},{true});assert(p.Groups().empty()&&p.ByProject()[0].empty());++controls;}
 {const TestWorkGroups p({{1,0}},{},{},{},{true},{{}},{true});assert(p.Groups().size()==1&&p.Groups()[0].tests.empty());++controls;}
 Reject([]{(void)Plan({{2,WorkGroupMode::Serial},{4,WorkGroupMode::Parallel}});});++controls;
 Reject([]{(void)Plan({{2,WorkGroupMode::Serial},{4,WorkGroupMode::Parallel}},true,{5});});++controls;
 Reject([]{(void)Plan({{99,WorkGroupMode::Parallel}});});++controls;
 Reject([]{(void)Plan({{2,static_cast<WorkGroupMode>(99)}});});++controls;
 Reject([]{(void)Plan({},true,{1,1});});++controls;
 Reject([]{(void)Plan({},true,{99});});++controls;
 Reject([]{TestWorkGroups p({{1,1}},{},{},{},{true,true},{{1},{}},{true,true});});++controls;
 Reject([]{TestWorkGroups p({{1,0}},{},{},{},{false},{{1}},{true});});++controls;
 Reject([]{TestWorkGroups p({{0,0}},{},{},{},{true},{{}},{true});});++controls;
 Reject([]{TestWorkGroups p({{1,0},{1,0}},{},{},{},{true},{{1}},{true});});++controls;
 Reject([]{TestWorkGroups p({{1,0}},{},{},{{1,WorkGroupMode::Parallel}},{true},{{1}},{true});});++controls;
 Reject([]{auto m=Members();m.pop_back();TestWorkGroups p(Tests(),Suites(),m,{},{true},{{1}},{true});});++controls;
 Reject([]{std::vector<WorkGroupTest> tests;for(size_t i=0;i<=TestWorkGroups::kTests;++i)tests.push_back({i+1,0});TestWorkGroups p(tests,{},{},{},{true},{{}},{true});});++controls;
 assert(controls==25);std::cout<<"Native selected test work groups: "<<controls<<" controls passed\n";
}
