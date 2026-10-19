// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "snapshot_reference_projection.h"
#include <cassert>
#include <iostream>
int main(){
  cef_api_hash(CEF_API_VERSION,0);std::string error;const std::string prefix="rActualSource_17_";
  const std::string original=R"([{"role":"button","name":"Actual\ud800","ref":"rActualSource_17_0"},{"role":"group","children":[{"role":"link","name":"Actual\udfff","ref":"rActualSource_17_1"}]}])";
  const auto before=original;assert(suitecut::SnapshotReferenceForest(original,prefix,2,error)&&original==before);
  assert(!suitecut::SnapshotReferenceForest(original,prefix,3,error));
  assert(!suitecut::SnapshotReferenceForest(original,"rDifferentSource_17_",2,error));
  assert(!suitecut::SnapshotReferenceForest(R"([{"role":"button","ref":"rActualSource_17_00"}])",prefix,1,error));
  assert(!suitecut::SnapshotReferenceForest(R"([{"role":"button","ref":"rActualSource_17_1"}])",prefix,1,error));
  assert(suitecut::SnapshotReferenceForest(R"([{"role":"text","text":"Actual\ud800"}])",prefix,0,error));
  assert(!suitecut::SnapshotReferenceForest(R"([{"ref":"rActualSource_17_0"}])",prefix,1,error));
  assert(!suitecut::SnapshotReferenceForest(R"([{"role":"button","children":"author"}])",prefix,0,error));
  assert(!suitecut::SnapshotReferenceForest("[17]",prefix,0,error));
  assert(!suitecut::SnapshotReferenceForest("{}",prefix,0,error));
  std::string nested=R"({"role":"button"})";for(int i=0;i<66;++i)nested="{\"role\":\"group\",\"children\":["+nested+"]}";
  assert(!suitecut::SnapshotReferenceForest("["+nested+"]",prefix,0,error));
  assert(!suitecut::SnapshotReferenceForest(std::string(65537,' '),prefix,0,error));
  const std::map<std::string,size_t> aggregate{{prefix,1},{"rActualChild_18_",1}};
  const std::string frames=R"([{"role":"iframe","name":"Parent\ud800","ref":"rActualSource_17_0","children":[{"role":"textbox","text":"Child\udfff","ref":"rActualChild_18_0"}]}])";
  const auto exact=frames;assert(suitecut::SnapshotReferenceForests(frames,aggregate,error)&&frames==exact);
  assert(!suitecut::SnapshotReferenceForests(frames,{{prefix,1}},error));
  assert(!suitecut::SnapshotReferenceForests(frames,{{prefix,1},{"rActualChild_18_",2}},error));
  assert(!suitecut::SnapshotReferenceForests(frames,{{prefix,4096},{"rActualChild_18_",1}},error));
  std::vector<size_t> owners;assert(suitecut::SnapshotReferenceFrameIndices(R"([{"role":"iframe","ref":"rActualSource_17_0"}])",prefix,1,owners,error)&&owners==std::vector<size_t>{0});
  assert(!suitecut::SnapshotReferenceFrameIndices(R"([{"role":"iframe","ref":"rActualSource_17_0"},{"role":"iframe","ref":"rActualSource_17_0"}])",prefix,1,owners,error));
  const std::vector<std::vector<std::string>> stable{{"rActualSource_17_1","rActualSource_17_0"},{"rActualChild_18_0"}};
  assert(suitecut::SnapshotReferenceForests(R"([{"role":"iframe","ref":"rActualSource_17_0","children":[{"role":"textbox","ref":"rActualChild_18_0"}]},{"role":"button","ref":"rActualSource_17_1"}])",stable,error));
  assert(suitecut::SnapshotReferenceFrameIndices(R"([{"role":"iframe","ref":"rActualSource_17_0"},{"role":"button","ref":"rActualSource_17_1"}])",stable[0],owners,error)&&owners==std::vector<size_t>{1});
  assert(!suitecut::SnapshotReferenceForests(original,std::vector<std::vector<std::string>>{{"rActualSource_17_0"}},error));
  assert(!suitecut::SnapshotReferenceForests(original,std::vector<std::vector<std::string>>{{"rActualSource_17_0","rActualSource_17_0"}},error));
  assert(!suitecut::SnapshotReferenceForests(original,std::vector<std::vector<std::string>>{{"rActualSource_17_0","rActualSource_99_0"}},error));
  assert(!suitecut::SnapshotReferenceFrameIndices(R"([{"role":"iframe","ref":"rActualSource_17_0"},{"role":"iframe","ref":"rActualSource_17_0"}])",std::vector<std::string>{"rActualSource_17_0"},owners,error));
  std::cout<<"PASS18 real CEF native reference forest/token/Unicode validation groups; no compiled browser-reference claim\n";
}
