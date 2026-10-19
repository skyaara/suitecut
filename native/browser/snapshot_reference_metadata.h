// SPDX-License-Identifier: MIT
#pragma once
#include <set>
#include <string>
#include <vector>
#include "include/cef_values.h"
#include "snapshot_reference_controls.h"

namespace suitecut {
// Consumes only Chromium's selected Runtime deep-node response schema. Author
// attributes, names, values and objects are never decoded as reference metadata.
inline bool SnapshotReferenceNodes(CefRefPtr<CefDictionaryValue> result,
                                   std::vector<SnapshotReferences::Node>& output,
                                   std::string& error) {
  output.clear();
  const auto fail = [&](const char* message) { error = message; return false; };
  if (!result || result->HasKey("exceptionDetails"))
    return fail("Native snapshot reference node metadata unavailable");
  const auto remote = result->GetDictionary("result");
  const auto deep = remote && remote->GetString("subtype") == "array"
    ? remote->GetDictionary("deepSerializedValue") : nullptr;
  const auto nodes = deep && deep->GetString("type") == "array" ? deep->GetList("value") : nullptr;
  if (!nodes || nodes->GetSize() > SnapshotReferences::kActiveNodes)
    return fail("Native snapshot reference node metadata exceeds capacity or has invalid shape");
  std::set<int> identities;
  std::vector<SnapshotReferences::Node> validated;
  validated.reserve(nodes->GetSize());
  for (size_t index = 0; index < nodes->GetSize(); ++index) {
    const auto value = nodes->GetDictionary(index);
    const auto node = value && value->GetString("type") == "node" ? value->GetDictionary("value") : nullptr;
    if (!node || node->GetType("nodeType") != VTYPE_INT || node->GetInt("nodeType") != 1 ||
        node->GetType("backendNodeId") != VTYPE_INT || node->GetInt("backendNodeId") <= 0)
      return fail("Native snapshot reference did not identify a genuine Element backend node");
    const int backend = node->GetInt("backendNodeId");
    if (!identities.insert(backend).second)
      return fail("Native snapshot reference node metadata contains a duplicate backend identity");
    validated.push_back({backend, 1});
  }
  output = std::move(validated);
  error.clear();
  return true;
}
}
