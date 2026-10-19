// SPDX-License-Identifier: MIT
#include "multi_file_catalog.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
template<class F>void Reject(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}assert(caught);}
sr::CollectedSuiteFile File(uint64_t id,size_t count,std::optional<sr::ProjectDeclaration> project=std::nullopt){
  sr::CollectedSuiteFile file;file.file={id,1,id};file.path="/controlled/file"+std::to_string(id)+".spec.mjs";file.catalog.protocol="actual controlled metadata "+std::to_string(id);
  if(project){file.catalog.projects.push_back(*project);file.catalog.project_metadata.push_back("{}");}
  for(size_t i=0;i<count;++i){file.catalog.tests.push_back({i+1,1000,0,false,"\"actual local declaration\"",{}});if(project)file.catalog.project_tests.emplace(i+1,0);}
  return file;
}
int main(){
  sr::MultiFileCatalog catalog({File(1,2),File(2,2)});
  assert(catalog.Tests().size()==4&&catalog.Tests()[2].selection_id==3&&catalog.Tests()[2].local_id==1&&catalog.Tests()[2].file==1);
  auto selected=catalog.Select();const auto anonymous=catalog.AnonymousProject();
  assert(catalog.LocalTests(selected,anonymous,0)==std::vector<uint64_t>({1,2}));
  sr::TestSelection exact;exact.ids={3};selected=catalog.Select(exact);
  assert(catalog.LocalTests(selected,anonymous,0).empty()&&catalog.LocalTests(selected,anonymous,1)==std::vector<uint64_t>({1}));
  sr::TestSelection shard;shard.shard_current=2;shard.shard_total=2;selected=catalog.Select(shard);
  assert(catalog.LocalTests(selected,anonymous,0).empty()&&catalog.LocalTests(selected,anonymous,1)==std::vector<uint64_t>({1,2}));
  auto focused=File(2,2);focused.catalog.focused={2};sr::MultiFileCatalog focus({File(1,2),focused});
  selected=focus.Select();assert(focus.LocalTests(selected,focus.AnonymousProject(),0).empty()&&focus.LocalTests(selected,focus.AnonymousProject(),1)==std::vector<uint64_t>({2}));
  sr::TestSelection forbid;forbid.forbid_only=true;Reject([&]{(void)focus.Select(forbid);});
  exact.ids={99};Reject([&]{(void)catalog.Select(exact);});
  auto support=File(1,3,sr::ProjectDeclaration{"setup"});support.config_eligible=std::set<uint64_t>{1,3};support.cli_eligible=std::set<uint64_t>{};
  auto primary=File(2,2,sr::ProjectDeclaration{"browser",{"setup"},"teardown"});primary.cli_eligible=std::set<uint64_t>{2};
  auto teardown=File(3,1,sr::ProjectDeclaration{"teardown"});
  sr::MultiFileCatalog projects({support,primary,teardown});selected=projects.Select({}, {"browser"});
  const auto setup=projects.Graph().Index("setup"),browser=projects.Graph().Index("browser"),end=projects.Graph().Index("teardown");
  assert(projects.LocalTests(selected,setup,0)==std::vector<uint64_t>({1,3}));
  assert(projects.LocalTests(selected,browser,1)==std::vector<uint64_t>({2}));
  assert(projects.LocalTests(selected,end,2)==std::vector<uint64_t>({1}));
  assert(selected.roles[setup]=="dependency"&&selected.roles[browser]=="primary"&&selected.roles[end]=="teardown");
  selected=projects.Select({}, {"browser"},true);assert(!selected.projects.selected[setup]&&!selected.projects.selected[end]);
  auto repeated=File(4,1,sr::ProjectDeclaration{"browser",{"setup"},"teardown"});
  sr::MultiFileCatalog combined({support,primary,teardown,repeated});selected=combined.Select({}, {"browser"});
  assert(combined.LocalTests(selected,combined.Graph().Index("browser"),3)==std::vector<uint64_t>({1}));
  auto wrong=repeated;wrong.catalog.projects[0].dependencies.clear();Reject([&]{sr::MultiFileCatalog invalid({support,primary,teardown,wrong});});
  auto forged=File(1,1);forged.cli_eligible=std::set<uint64_t>{9};Reject([&]{sr::MultiFileCatalog invalid({forged});});
  Reject([&]{sr::MultiFileCatalog invalid({File(1,1),File(1,1)});});
  Reject([&]{sr::MultiFileCatalog invalid({File(1,1,sr::ProjectDeclaration{"missing",{"absent"}})});});
  Reject([&]{(void)catalog.Select({}, {"suitecut-native-anonymous"});});
  std::cout<<"Native global file catalog/selection controls passed\n";
}
