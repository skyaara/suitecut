// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "snapshot_reference_metadata.h"
#include "include/cef_parser.h"
#include <cassert>
#include <iostream>
namespace {
using Dict=CefRefPtr<CefDictionaryValue>;
Dict Reply(const char* json){const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);return parsed?parsed->GetDictionary():nullptr;}
}
int main(){
  cef_api_hash(CEF_API_VERSION,0);
  std::vector<suitecut::SnapshotReferences::Node> output;std::string error;
  auto actual=Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[{"type":"node","value":{"nodeType":1,"backendNodeId":41,"attributes":{"ref":"author-value"}}},{"type":"node","value":{"nodeType":1,"backendNodeId":42}}]}}})");
  assert(suitecut::SnapshotReferenceNodes(actual,output,error)&&output.size()==2&&output[0].backend==41&&output[1].backend==42);
  actual=nullptr;assert(output.size()==2&&output[0].backend==41); // Native values are independent of the actual CEF response owner.
  assert(suitecut::SnapshotReferenceNodes(Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[]}}})"),output,error)&&output.empty());
  assert(!suitecut::SnapshotReferenceNodes(Reply(R"({"result":{"subtype":"object","deepSerializedValue":{"type":"array","value":[]}}})"),output,error));
  assert(!suitecut::SnapshotReferenceNodes(Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[{"type":"object","value":{"nodeType":1,"backendNodeId":41}}]}}})"),output,error));
  assert(!suitecut::SnapshotReferenceNodes(Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[{"type":"node","value":{"nodeType":3,"backendNodeId":41}}]}}})"),output,error));
  assert(!suitecut::SnapshotReferenceNodes(Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[{"type":"node","value":{"nodeType":1,"backendNodeId":0}}]}}})"),output,error));
  assert(!suitecut::SnapshotReferenceNodes(Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[{"type":"node","value":{"nodeType":1,"backendNodeId":41}},{"type":"node","value":{"nodeType":1,"backendNodeId":41}}]}}})"),output,error));
  assert(!suitecut::SnapshotReferenceNodes(Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[{"type":"node","weakLocalObjectReference":1}]}}})"),output,error));
  assert(!suitecut::SnapshotReferenceNodes(Reply(R"({"exceptionDetails":{},"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[]}}})"),output,error));
  auto bound=Reply(R"({"result":{"subtype":"array","deepSerializedValue":{"type":"array","value":[]}}})");
  auto values=bound->GetDictionary("result")->GetDictionary("deepSerializedValue")->GetList("value");values->SetSize(4097);
  assert(!suitecut::SnapshotReferenceNodes(bound,output,error)&&output.empty());
  assert(!suitecut::SnapshotReferenceNodes(nullptr,output,error)&&output.empty());
  std::cout<<"PASS11 genuine CEF-owned selected-node-schema validation groups; no actual browser snapshot claim\n";
}
