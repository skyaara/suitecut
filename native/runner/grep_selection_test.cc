// SPDX-License-Identifier: MIT
#include "grep_title.h"
#include "project_selection.h"
#include <iostream>
using namespace suitecut::runner;
namespace{unsigned checks=0;void Check(bool condition){++checks;if(!condition)throw std::runtime_error("grep selection control failed "+std::to_string(checks));}
template<class Callback>void Reject(Callback callback){bool failed=false;try{callback();}catch(const std::invalid_argument&){failed=true;}Check(failed);}}
int main(){
  const std::vector<GrepTitleComponent> parts{{u"outer suite",2,{u"@inherited",u"@same"}},{u"test AA",2,{u"@leaf",u"@same"}}};
  Check(ComposeGrepTitle(u"Project One",u"nested/alpha.spec.mjs",parts)==u"Project One nested/alpha.spec.mjs outer suite @inherited @same test AA @leaf @same");
  Check(ComposeGrepTitle(u"",u"a.mjs",{{u"",0,{}},{u"test",0,{}}})==u"a.mjs  test");
  Check(GrepUnits("\"\\ud800\"")==std::u16string(1,0xd800));Check(GrepUnits(GrepJSON(u"a\n\U0001f600"))==u"a\n\U0001f600");
  Check(GrepUTF8("a\n\xf0\x9f\x98\x80")==u"a\n\U0001f600");Reject([]{GrepUTF8("\xff");});
  ValidateGrepComponents({{1,parts}},{1});Reject([&]{ValidateGrepComponents({{1,parts}},{1,2});});
  auto incomplete=parts;incomplete[0].tags.pop_back();Reject([&]{ValidateGrepComponents({{1,incomplete}},{1});});
  const std::vector<uint64_t> ids{1,2,3,4};TestSelection selection;selection.eligible=std::set<uint64_t>{2,4};
  Check(SelectTestIndices(ids,selection)==std::vector<size_t>({1,3}));selection.shard_total=2;
  Check(SelectTestIndices(ids,selection)==std::vector<size_t>({1}));selection.shard_current=2;Check(SelectTestIndices(ids,selection)==std::vector<size_t>({3}));
  selection.shard_current=1;selection.shard_total=1;selection.eligible->clear();Check(SelectTestIndices(ids,selection).empty());
  selection.ids={99};Reject([&]{SelectTestIndices(ids,selection);});selection.ids.clear();selection.eligible->insert(99);Reject([&]{SelectTestIndices(ids,selection);});
  ProjectGraph graph({{"body",{"setup"},"teardown"},{"setup"},{"teardown"}});
  const std::vector<ProjectTestDeclaration> declarations{{1,0},{2,0},{3,1},{4,1},{5,2}};
  TestSelection filter;filter.eligible=std::set<uint64_t>{2};
  const auto with_support=SelectProjectTests(graph,declarations,filter,{"body"});
  Check(with_support.tests==std::vector<std::vector<uint64_t>>({{2},{3,4},{5}}));
  Check(with_support.roles==std::vector<std::string>({"primary","dependency","teardown"}));
  const auto without_support=SelectProjectTests(graph,declarations,filter,{"body"},true);
  Check(without_support.tests==std::vector<std::vector<uint64_t>>({{2},{},{}}));
  filter.eligible->clear();const auto none=SelectProjectTests(graph,declarations,filter,{"body"});
  Check(none.plan.selected==std::vector<bool>({false,false,false}));
  filter.eligible=std::set<uint64_t>{2};filter.shard_total=2;filter.shard_current=2;
  Check(SelectProjectTests(graph,declarations,filter,{"body"}).plan.selected==std::vector<bool>({false,false,false}));
  filter.shard_current=1;Check(SelectProjectTests(graph,declarations,filter,{"body"}).tests==with_support.tests);
  filter.forbid_only=true;Reject([&]{SelectProjectTests(graph,declarations,filter,{"body"},false,{3});});
  filter.forbid_only=false;filter.shard_current=filter.shard_total=1;
  filter.catalog_eligible=std::set<uint64_t>{1,2,3,5};
  Check(SelectProjectTests(graph,declarations,filter,{"body"}).tests==std::vector<std::vector<uint64_t>>({{2},{3},{5}}));
  filter.catalog_eligible=std::set<uint64_t>{3,5};
  Check(SelectProjectTests(graph,declarations,filter,{"body"}).plan.selected==std::vector<bool>({false,false,false}));
  filter.catalog_eligible->insert(99);Reject([&]{SelectProjectTests(graph,declarations,filter,{"body"});});
  Check(ComposeGrepTitle(u"P",u"file.mjs",parts,{u"@global",u"@same"})==u"P file.mjs @global @same outer suite @inherited @same test AA @leaf @same");
  std::cout<<"Native grep title/selection "<<checks<<" controls PASS\n";
}
