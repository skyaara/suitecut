// SPDX-License-Identifier: MIT
#include "snapshot_paths.h"
#include <cassert>
#include <charconv>
#include <iostream>
#include <memory>
#include <set>
namespace sr=suitecut::runner;
struct Metadata {
  sr::NativeSnapshotDeclarations snapshot_paths;
  struct Component {std::u16string title;};
  struct Project {std::string name;};
  std::vector<Project> projects;
  std::map<uint64_t,std::string>titles;
  std::map<uint64_t,size_t>project_tests;
  std::map<uint64_t,std::vector<Component>>grep_components;
};
struct Harness {
  struct Options {std::string suite="/actual/a/b/test.mjs";}options_;
  struct Active {struct Test {uint64_t id=1;}test;};
  std::optional<Active>active_=Active{};
  std::optional<Metadata>metadata_=Metadata{};
  std::vector<std::string>sent;size_t charge=0;bool reject_send=false;
  static void Size(const std::vector<std::string>&value,size_t size){if(value.size()!=size)throw std::invalid_argument("size");}
  static uint64_t Integer(const std::string&value,uint64_t max){uint64_t number=0;const auto result=std::from_chars(value.data(),value.data()+value.size(),number);if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size()||number>max)throw std::invalid_argument("integer");return number;}
  static std::string Hex(const std::string&value){constexpr char hex[]="0123456789abcdef";std::string result;for(const unsigned char c:value){result+=hex[c>>4];result+=hex[c&15];}return result.empty()?"-":result;}
  void Charge(size_t bytes){charge+=bytes;}
  void Send(const std::string&line){if(reject_send)throw std::runtime_error("actual controlled transport rejection");sent.push_back(line);}
#include "snapshot_path_binding.inc"
  Harness(){metadata_->titles[1]="\"test\"";metadata_->grep_components[1]={{{u"suite"}},{{u"test"}}};}
  std::string LastJSON()const{const auto&line=sent.back();const auto offset=line.find(' ',line.find(' ')+1);return sr::DecodeHex(line.substr(offset+1),30000);}
};
template<class F>void Reject(F&&f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
  const auto named=[](const sr::merge_detail::JSON&plan,std::string_view kind){const auto&paths=sr::merge_detail::UInt(plan,"version")==3?sr::merge_detail::Get(plan,"paths"):plan;std::string result;for(const auto&part:sr::merge_detail::Rows(paths,kind)){const auto type=sr::merge_detail::Text(sr::merge_detail::Get(part,"kind")),value=sr::merge_detail::Text(sr::merge_detail::Get(part,"value"));result+=type=="arg"?value+"tree":type=="ext"?value+".png":type=="suffix"?value+sr::merge_detail::Text(sr::merge_detail::Get(sr::merge_detail::Get(plan,"suffix"),"initial")):value;}return std::filesystem::path(result).lexically_normal().string();};
  Harness defaults;defaults.metadata_->projects={{"real project"}};defaults.metadata_->project_tests[1]=0;defaults.SendSnapshotContext(1);
  const auto default_raw=defaults.LastJSON();const auto default_json=sr::merge_detail::JSONParser(default_raw).Parse();
  assert(sr::merge_detail::UInt(default_json,"version")==3&&sr::merge_detail::UInt(sr::merge_detail::Get(default_json,"suffix"),"maxBytes")==1024);
  assert(named(default_json,"aria")=="/actual/a/b/test.mjs-snapshots/tree.png");
  assert(named(default_json,"snapshot")=="/actual/a/b/test.mjs-snapshots/tree-real-project-"+sr::NativeSnapshotPaths::Platform()+".png");
  assert(named(default_json,"screenshot")==named(default_json,"snapshot"));
  Harness generic;generic.SnapshotDeclaration(*generic.metadata_,{"SNAPSHOT_CONFIG","0","snapshotPathTemplate",Harness::Hex("global/{arg}{ext}")});
  generic.SnapshotDeclaration(*generic.metadata_,{"SNAPSHOT_CONFIG","0","screenshotPathTemplate",Harness::Hex("pixels/{arg}{ext}")});generic.SendSnapshotContext(1);
  assert(named(sr::merge_detail::JSONParser(generic.LastJSON()).Parse(),"aria")=="global/tree.png");
  Harness specific;specific.SnapshotDeclaration(*specific.metadata_,{"SNAPSHOT_CONFIG","0","snapshotPathTemplate",Harness::Hex("global/{arg}{ext}")});
  specific.SnapshotDeclaration(*specific.metadata_,{"SNAPSHOT_CONFIG","0","ariaPathTemplate",Harness::Hex("semantic/{arg}{ext}")});specific.SendSnapshotContext(1);
  assert(named(sr::merge_detail::JSONParser(specific.LastJSON()).Parse(),"aria")=="semantic/tree.png");
  Harness h;assert(!h.SnapshotDeclaration(*h.metadata_,{"OTHER"}));
  assert(h.SnapshotDeclaration(*h.metadata_,{"SNAPSHOT_CONFIG","0","snapshotPathTemplate",Harness::Hex("{testName}/{arg}{ext}")}));
  h.SendSnapshotContext(1);assert(h.sent.size()==1&&h.LastJSON().find("suite-test/")!=std::string::npos);
  h.SendSnapshotContext(1);assert(h.sent.size()==1);
  h.active_->test.id=2;Reject([&]{h.SendSnapshotContext(1);});h.active_->test.id=1;
  h.snapshot_contexts_.clear();h.reject_send=true;Reject([&]{h.SendSnapshotContext(1);});assert(h.snapshot_contexts_.empty());
  h.reject_send=false;h.SendSnapshotContext(1);assert(h.sent.size()==2);
  Reject([&]{h.SnapshotDeclaration(*h.metadata_,{"SNAPSHOT_CONFIG","1","snapshotDir",Harness::Hex("foreign")});});
  Harness scalar;scalar.metadata_->grep_components[1][0].title=std::u16string(1,0xd800);scalar.SendSnapshotContext(1);assert(scalar.LastJSON().find("unsupported")==std::string::npos);
  scalar.snapshot_contexts_.clear();scalar.SnapshotDeclaration(*scalar.metadata_,{"SNAPSHOT_CONFIG","0","snapshotPathTemplate",Harness::Hex("{testName}")});scalar.SendSnapshotContext(1);assert(scalar.LastJSON().find("unsupported")!=std::string::npos);assert(scalar.LastJSON().find("/actual/a/b/test.mjs-snapshots")!=std::string::npos);
  Harness unknown;unknown.metadata_->grep_components[1][0].title=std::u16string(1,0xd800);
  unknown.SnapshotDeclaration(*unknown.metadata_,{"SNAPSHOT_CONFIG","0","snapshotPathTemplate",Harness::Hex("literal/testName}/{notAToken_testName}/{arg}")});
  unknown.SendSnapshotContext(1);assert(unknown.LastJSON().find("unsupported")==std::string::npos&&unknown.LastJSON().find("notAToken_testName")!=std::string::npos);
  Harness project;project.metadata_->projects={{"real project"},{"other"}};project.metadata_->project_tests[1]=0;
  project.SnapshotDeclaration(*project.metadata_,{"SNAPSHOT_CONFIG","1","snapshotPathTemplate",Harness::Hex("{projectName}/{arg}")});project.SendSnapshotContext(1);assert(project.LastJSON().find("real-project/")!=std::string::npos);
  Harness missing;missing.metadata_->grep_components.clear();Reject([&]{missing.SendSnapshotContext(1);});assert(missing.sent.empty());
  Harness unavailable;unavailable.metadata_->snapshot_paths.Declare(0,0,"testDir","/different");unavailable.SendSnapshotContext(1);assert(unavailable.LastJSON().find("unsupported")!=std::string::npos);
  std::cout<<"native snapshot actual included binding PASS\n";
}
