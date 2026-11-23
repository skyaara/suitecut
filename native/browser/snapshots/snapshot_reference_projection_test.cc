// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "snapshots/snapshot_reference_projection.h"
#include <cassert>
#include <iostream>
int main(){
  cef_api_hash(CEF_API_VERSION,0);std::string error;
  using Groups=std::vector<std::vector<std::string>>;
  const std::vector<std::string> own{"s17e0","s17e1"};
  // Lone surrogates and a quoted "ref" inside a name are data, not tokens.
  const std::string original=R"([{"role":"button","name":"Actual\ud800 \"ref\":\"s99e0\"","ref":"s17e0"},{"role":"group","children":[{"role":"link","name":"Actual\udfff","ref":"s17e1"}]}])";
  const auto before=original;assert(suitecut::SnapshotReferenceForests(original,Groups{own},error)&&original==before);
  assert(!suitecut::SnapshotReferenceForests(original,Groups{{"s17e0"}},error));
  assert(!suitecut::SnapshotReferenceForests(original,Groups{{"s17e0","s17e1","s17e2"}},error)&&error.find("omits")!=std::string::npos);
  assert(!suitecut::SnapshotReferenceForests(original,Groups{{"s17e0","s17e0"}},error));
  assert(!suitecut::SnapshotReferenceForests(original,Groups{},error));
  assert(!suitecut::SnapshotReferenceForests("{}",Groups{own},error));
  assert(!suitecut::SnapshotReferenceForests(R"([{"role":"button","ref":"s17e0)",Groups{{"s17e0"}},error));
  assert(suitecut::SnapshotReferenceForests(R"([{"role":"text","text":"Actual\ud800"}])",Groups{{}},error));
  // Child-frame forests merged under their iframe nodes.
  const std::string frames=R"([{"role":"iframe","ref":"s17e0","children":[{"role":"textbox","ref":"s18e0"}]},{"role":"button","ref":"s17e1"}])";
  assert(suitecut::SnapshotReferenceForests(frames,Groups{own,{"s18e0"}},error));
  assert(!suitecut::SnapshotReferenceForests(frames,Groups{own},error));
  std::vector<size_t> owners;
  assert(suitecut::SnapshotReferenceFrameIndices(R"([{"role":"iframe","ref":"s17e0"},{"role":"button","ref":"s17e1"}])",own,owners,error)&&owners==std::vector<size_t>{0});
  assert(suitecut::SnapshotReferenceFrameIndices(R"([{"role":"button","ref":"s17e0"},{"role":"link","ref":"s17e1"}])",own,owners,error)&&owners.empty());
  assert(!suitecut::SnapshotReferenceFrameIndices(R"([{"role":"iframe","ref":"s17e0"},{"role":"iframe","ref":"s17e0"}])",{"s17e0"},owners,error));
  // Large forests stay a linear scan.
  std::string big="[";std::vector<std::string> many;for(int i=0;i<200000;++i){many.push_back("s1e"+std::to_string(i));big+=std::string(i?",":"")+R"({"role":"button","name":"Add product )"+std::to_string(i)+R"(","ref":")"+many.back()+"\"}";}big+="]";
  assert(suitecut::SnapshotReferenceFrameIndices(big,many,owners,error)&&owners.empty());
  std::cout<<"Native snapshot reference token scan passed\n";
}
