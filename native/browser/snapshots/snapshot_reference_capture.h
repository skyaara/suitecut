// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <string>
#include "snapshots/snapshot_reference_controls.h"
namespace suitecut {
struct SnapshotReferenceCapture {
  int owner=0;uint64_t generation=0,ticket=0;
  SnapshotReferences::Document document;
  std::string capture;size_t requests=0;int depth=-1;bool boxes=false,json=false,interactive=false;
  std::function<bool()> all_current,before_request;std::function<void()> installed_done;
};
}
