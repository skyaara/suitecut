// SPDX-License-Identifier: MIT
#include "snapshot_paths.h"
#include <cassert>
#include <iostream>
namespace sr=suitecut::runner;
template<class F>void Reject(F&&f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
std::string Literal(const std::vector<sr::NativeSnapshotPart>&parts){std::string result;for(const auto&part:parts){assert(part.kind==sr::NativeSnapshotPart::Kind::Literal);result+=part.value;}return result;}
std::string Named(const std::vector<sr::NativeSnapshotPart>&parts){std::string result;for(const auto&part:parts)result+=part.kind==sr::NativeSnapshotPart::Kind::Literal?part.value:part.kind==sr::NativeSnapshotPart::Kind::Argument?part.value+"tree":part.value+".png";return std::filesystem::path(result).lexically_normal().string();}
int main(){
  const sr::NativeSnapshotFacts facts{"/actual/a/b/c/d.spec.ts","proj 2",{"suite","test should work"}};
  sr::NativeSnapshotSettings settings;settings.test_dir="../../..";
  const auto first=sr::NativeSnapshotPaths::Bind(facts,settings);
  assert(first.snapshot_directory=="/actual/a/b/c/d.spec.ts-snapshots");
  assert(first.snapshot.size()==4&&first.snapshot[1].kind==sr::NativeSnapshotPart::Kind::Argument&&first.snapshot.back().kind==sr::NativeSnapshotPart::Kind::Extension);
  assert(!first.common_kind_plan&&Named(first.snapshot)==Named(first.screenshot));
  assert(Named(first.snapshot)=="/actual/a/b/c/d.spec.ts-snapshots/tree-proj-2-"+sr::NativeSnapshotPaths::Platform()+".png");
  assert(Named(first.aria)=="/actual/a/b/c/d.spec.ts-snapshots/tree.png");
  auto unnamed_default=facts;unnamed_default.project_name.clear();
  assert(Named(sr::NativeSnapshotPaths::Bind(unnamed_default,settings).aria)==Named(first.aria));
  auto explicit_common=settings;explicit_common.path_template="{snapshotDir}/{testFilePath}-snapshots/{arg}{-projectName}{-snapshotSuffix}{ext}";
  const auto common=sr::NativeSnapshotPaths::Bind(facts,explicit_common);
  assert(common.common_kind_plan&&common.screenshot.empty()&&common.aria.empty());
  const auto shared_json=sr::NativeSnapshotPaths::Serialize(common);const auto shared=sr::merge_detail::JSONParser(shared_json).Parse();
  assert(sr::merge_detail::UInt(shared,"version")==2&&sr::merge_detail::Rows(shared,"plans").size()==1);
  const auto&shared_kinds=sr::merge_detail::Get(shared,"kinds");
  for(const auto kind:{"snapshot","screenshot","aria"})assert(sr::merge_detail::UInt(shared_kinds,kind)==1);
  auto inconsistent=common;inconsistent.aria.push_back({sr::NativeSnapshotPart::Kind::Literal,"different"});
  Reject([&]{sr::NativeSnapshotPaths::Serialize(inconsistent);});
  const auto mutable_plan=sr::NativeSnapshotPaths::Bind(facts,settings,true);
  assert(mutable_plan.mutable_suffix&&!mutable_plan.common_kind_plan);
  assert(mutable_plan.snapshot[mutable_plan.snapshot.size()-2].kind==sr::NativeSnapshotPart::Kind::SnapshotSuffix);
  for(const auto&part:mutable_plan.aria)assert(part.kind!=sr::NativeSnapshotPart::Kind::SnapshotSuffix);
  const auto mutable_wire=sr::NativeSnapshotPaths::Serialize(mutable_plan);const auto mutable_json=sr::merge_detail::JSONParser(mutable_wire).Parse();
  assert(sr::merge_detail::UInt(mutable_json,"version")==3);
  const auto&suffix=sr::merge_detail::Get(mutable_json,"suffix");
  assert(sr::merge_detail::UInt(suffix,"maxBytes")==1024&&sr::merge_detail::Text(sr::merge_detail::Get(suffix,"initial"))==sr::NativeSnapshotPaths::Platform());
  auto explicit_suffix=settings;explicit_suffix.path_template="{platform}/{snapshotSuffix}/{-snapshotSuffix}/{arg}{ext}";
  const auto suffix_ops=sr::NativeSnapshotPaths::Bind(facts,explicit_suffix,true);
  assert(suffix_ops.common_kind_plan&&suffix_ops.snapshot.front().value==sr::NativeSnapshotPaths::Platform()+"/");
  assert(suffix_ops.snapshot[1].kind==sr::NativeSnapshotPart::Kind::SnapshotSuffix&&suffix_ops.snapshot[3].value=="-");
  explicit_suffix.path_template="{snapshotSuffix}";explicit_suffix.screenshot_template=sr::NativeSnapshotPaths::Platform();explicit_suffix.aria_template="{snapshotSuffix}";
  assert(!sr::NativeSnapshotPaths::Bind(facts,explicit_suffix,true).common_kind_plan);
  const std::vector<std::pair<std::string,std::string>> tokens={
    {"{projectName}","proj-2"},{"{-projectName}","-proj-2"},{"foo{/projectName}","foo/proj-2"},
    {"{platform}",sr::NativeSnapshotPaths::Platform()},{"{testFileDir}","a/b/c"},
    {"{testFilePath}","a/b/c/d.spec.ts"},{"{testFileName}","d.spec.ts"},{"{testFileBaseName}","d.spec"},
    {"{testName}","suite-test-should-work"},{"{-snapshotSuffix}","-"+sr::NativeSnapshotPaths::Platform()}};
  for(const auto&[token,expected]:tokens){settings.path_template=token;assert(Literal(sr::NativeSnapshotPaths::Bind(facts,settings).snapshot)==expected);}
  settings.path_template="{/projectName}";auto unnamed=facts;unnamed.project_name.clear();assert(Literal(sr::NativeSnapshotPaths::Bind(unnamed,settings).snapshot).empty());
  settings.snapshot_dir="./a-snapshot-dir";settings.path_template="{snapshotDir}.png";
  assert(Literal(sr::NativeSnapshotPaths::Bind(facts,settings).snapshot)=="/actual/a/b/c/a-snapshot-dir.png");
  assert(sr::NativeSnapshotPaths::Bind(facts,settings).snapshot_directory=="/actual/a/b/c/a-snapshot-dir/a/b/c/d.spec.ts-snapshots");
  settings.path_template="{ext}{arg}";auto argument=sr::NativeSnapshotPaths::Bind(facts,settings);
  assert(argument.snapshot.size()==2&&argument.snapshot[0].kind==sr::NativeSnapshotPart::Kind::Extension&&argument.snapshot[1].kind==sr::NativeSnapshotPart::Kind::Argument);
  settings.path_template="{unknown}/{testName";assert(Literal(sr::NativeSnapshotPaths::Bind(facts,settings).snapshot)=="{unknown}/{testName");
  settings.path_template="general/{arg}{ext}";settings.screenshot_template="pixels/{arg}{ext}";settings.aria_template="semantics/{arg}{ext}";
  const auto kinds=sr::NativeSnapshotPaths::Bind(facts,settings);assert(kinds.snapshot[0].value=="general/"&&kinds.screenshot[0].value=="pixels/"&&kinds.aria[0].value=="semantics/");
  const auto json=sr::NativeSnapshotPaths::Serialize(kinds);assert(sr::merge_detail::JSONParser(json).Parse().fields.size()==6);
  assert(!kinds.common_kind_plan&&sr::merge_detail::UInt(sr::merge_detail::JSONParser(json).Parse(),"version")==1);
  sr::NativeSnapshotSettings precedence;precedence.path_template="generic/{arg}{ext}";precedence.screenshot_template="screens/{arg}{ext}";
  assert(Named(sr::NativeSnapshotPaths::Bind(facts,precedence).aria)=="generic/tree.png");
  precedence.aria_template="aria/{arg}{ext}";
  assert(Named(sr::NativeSnapshotPaths::Bind(facts,precedence).aria)=="aria/tree.png");
  precedence.path_template.reset();precedence.aria_template.reset();
  const auto screenshot_only=sr::NativeSnapshotPaths::Bind(facts,precedence);
  assert(Named(screenshot_only.screenshot)=="screens/tree.png"&&Named(screenshot_only.aria)==Named(first.aria));
  auto equivalent=settings;equivalent.path_template="{projectName}/{arg}{ext}";equivalent.screenshot_template="proj-2/{arg}{ext}";equivalent.aria_template="{-projectName}/{arg}{ext}";
  assert(!sr::NativeSnapshotPaths::Bind(facts,equivalent).common_kind_plan);
  equivalent.aria_template="proj-2/{arg}{ext}";const auto coalesced=sr::NativeSnapshotPaths::Bind(facts,equivalent);
  assert(coalesced.common_kind_plan&&coalesced.screenshot.empty()&&coalesced.aria.empty());
  equivalent.path_template="{arg}";equivalent.screenshot_template="{ext}";equivalent.aria_template="{arg}";
  assert(!sr::NativeSnapshotPaths::Bind(facts,equivalent).common_kind_plan);
  sr::NativeSnapshotDeclarations declarations;declarations.Declare(0,2,"snapshotPathTemplate","root/{arg}");declarations.Declare(1,2,"snapshotDir","custom");
  assert(declarations.Select(0,2).path_template=="root/{arg}"&&declarations.Select(0,2).snapshot_dir=="custom"&&!declarations.Select(1,2).snapshot_dir);
  Reject([&]{declarations.Declare(1,2,"snapshotDir","replacement");});Reject([&]{declarations.Declare(3,2,"snapshotDir","foreign");});Reject([&]{declarations.Select(std::nullopt,2);});
  Reject([&]{declarations.Declare(2,2,"snapshotDir",std::string("a\0b",3));});assert(!declarations.Select(1,2).snapshot_dir);
  auto invalid=facts;invalid.module="/foreign/test.ts";settings.test_dir="/actual";Reject([&]{sr::NativeSnapshotPaths::Bind(invalid,settings);});
  invalid=facts;invalid.module="relative.ts";Reject([&]{sr::NativeSnapshotPaths::Bind(invalid);});
  settings.path_template=std::string(32769,'x');Reject([&]{sr::NativeSnapshotPaths::Bind(facts,settings);});
  settings.path_template=std::string(256*5,'x');for(size_t i=0;i<257;++i)*settings.path_template+="{arg}";Reject([&]{sr::NativeSnapshotPaths::Bind(facts,settings);});
  assert(sr::NativeSnapshotPaths::FilesystemText(u"\U0001f642") == "\xf0\x9f\x99\x82");
  Reject([]{sr::NativeSnapshotPaths::FilesystemText(std::u16string(1,0xd800));});
  invalid=facts;invalid.title_path={std::string("\xed\xa0\x80",3)};sr::NativeSnapshotPaths::Bind(invalid);
  settings={};settings.path_template="literal/testName}/{notAToken_testName}";
  assert(Literal(sr::NativeSnapshotPaths::Bind(invalid,settings).snapshot)==*settings.path_template);
  settings={};settings.path_template="{testName}";Reject([&]{sr::NativeSnapshotPaths::Bind(invalid,settings);});
  std::cout<<"native snapshot path policy PASS\n";
}
