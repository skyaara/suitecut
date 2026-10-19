// SPDX-License-Identifier: MIT
#include "suite_discovery.h"
#include <cassert>
#include <fstream>
#include <iostream>

namespace sr=suitecut::runner;
template<class F>void Reject(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}assert(caught);}
int main(){
  char temporary[]="/tmp/suitecut-discovery-controls-XXXXXX";const auto* directory=::mkdtemp(temporary);assert(directory);
  struct Cleanup {std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{directory};
  const auto root=std::filesystem::canonical(directory);
  const auto write=[&](std::string name){const auto path=root/name;std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<"export default {};\n";};
  write("z.test.mjs");write("a.spec.ts");write("nested/b.test.jsx");write("nested/fixture.js");write("node_modules/hidden.spec.js");
  std::filesystem::create_symlink(root/"a.spec.ts",root/"linked.spec.ts");
  std::filesystem::create_directory_symlink(root/"nested",root/"linked-directory");
  sr::SuiteDiscoveryOptions options;options.directory=root;
  auto files=sr::SuiteDiscovery::Collect(options,[]{});
  assert(files.size()==3&&files[0].relative_path=="a.spec.ts"&&files[1].relative_path=="nested/b.test.jsx"&&files[2].relative_path=="z.test.mjs");
  for(size_t i=0;i<files.size();++i){assert(files[i].owner->Token().id==i+1);files[i].owner->Validate();}
  options.match=std::vector<sr::SuitePathPattern>{};assert(sr::SuiteDiscovery::Collect(options,[]{}).empty());
  options.match=std::vector<sr::SuitePathPattern>{{sr::SuitePathPattern::Kind::Glob,u"**/*.js",false}};
  auto javascript=sr::SuiteDiscovery::Collect(options,[]{});assert(javascript.size()==1&&javascript[0].relative_path=="nested/fixture.js");
  options.match=std::nullopt;options.ignore={{sr::SuitePathPattern::Kind::Glob,u"**/nested/**",false}};
  assert(sr::SuiteDiscovery::Collect(options,[]{}).size()==2);
  options.ignore.clear();options.file_filters={u"NESTED"};
  assert(sr::SuiteDiscovery::Collect(options,[]{}).size()==1);
  options.file_filters.clear();options.match=std::vector<sr::SuitePathPattern>{{sr::SuitePathPattern::Kind::Regex,u"a\\.spec\\.ts$",false}};
  assert(sr::SuiteDiscovery::Collect(options,[]{}).size()==1);
  sr::GrepPattern::Work work;
  sr::SuitePathMatcher brace({sr::SuitePathPattern::Kind::Glob,u"**/*.{spec,test}.{js,ts}",false});
  assert(brace.Matches(u"/actual/a.spec.ts",work)&&brace.Matches(u"/actual/nested/a.test.js",work)&&!brace.Matches(u"/actual/a.helper.ts",work));
  sr::SuitePathMatcher direct({sr::SuitePathPattern::Kind::Glob,u"nested/*.js",false});
  assert(direct.Matches(u"/actual/nested/fixture.js",work)&&!direct.Matches(u"/actual/nested/deep/fixture.js",work));
  sr::SuitePathMatcher newline({sr::SuitePathPattern::Kind::Glob,u"**/*.js",false});
  assert(newline.Matches(u"/actual/line\nbreak/file.js",work));
  Reject([]{sr::SuitePathMatcher invalid({sr::SuitePathPattern::Kind::Glob,u"**/@(a|b).js",false});});
  Reject([]{sr::SuitePathMatcher invalid({sr::SuitePathPattern::Kind::Glob,u"**/?(m)js",false});});
  Reject([]{sr::SuitePathMatcher invalid({sr::SuitePathPattern::Kind::Glob,u"**/{a,b.js",false});});
  for(const auto name:{"a.spec.js","b.test.tsx","c.spec.mts","d.test.cjs"})assert(sr::SuiteDiscovery::DefaultMatch(name));
  for(const auto name:{"spec.js","helper.ts","a.spec.json","a.spec.js.map"})assert(!sr::SuiteDiscovery::DefaultMatch(name));
  options.match=std::nullopt;unsigned checks=0;
  Reject([&]{(void)sr::SuiteDiscovery::Collect(options,[&]{if(++checks==5)throw std::runtime_error("actual outer budget expired");});});assert(checks==5);
  write("a.spec.ts");{std::ofstream changed(root/"a.spec.ts",std::ios::app);changed<<"actual mutation";}
  Reject([&]{files[0].owner->Validate();});
  options.directory=(root/"a.spec.ts").native();Reject([&]{(void)sr::SuiteDiscovery::Collect(options,[]{});});
  std::cout<<"Native discovery filesystem/matcher controls passed\n";
}
