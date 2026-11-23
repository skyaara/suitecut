// SPDX-License-Identifier: MIT
// Included inside Source after the single-frame reference capture methods.
// All captures remain staging-only until one final native CommitBatch.
// As in Playwright, a child frame that navigates, detaches, fails or does not
// answer in time leaves its iframe line empty instead of failing the snapshot.
// Only the root document must stay current.
struct SnapshotReferenceFrameBatch {
  struct Frame {
    std::shared_ptr<SnapshotReferenceCapture> work;
    std::string forest;
    size_t parent = 0;
    std::string owner_ref;
    bool dropped = false, merged = false;
  };
  struct Edge {
    size_t parent = 0, index = 0;
  };
  int owner = 0;
  uint64_t generation = 0;
  size_t next = 0, merge = 0;
  // The one child edge being captured: its parent, its frame once known, and a
  // sequence that retires every late callback of an abandoned edge.
  bool active_edge = false, merging = false;
  size_t active_parent = 0, active = 0;
  uint64_t edge_seq = 0;
  std::vector<Frame> frames;
  std::vector<Edge> edges;
  std::set<std::string> seen;
  std::vector<uint64_t> publication_tickets;
  bool publication_ready = false;
};
std::map<int, std::shared_ptr<SnapshotReferenceFrameBatch>> reference_frame_batches_;
bool ReferenceFramesCurrent(std::shared_ptr<SnapshotReferenceFrameBatch> batch) {
  if (!operations_.IsCurrent(batch->owner, batch->generation)) return false;
  const auto owned = reference_frame_batches_.find(batch->owner);
  if (owned == reference_frame_batches_.end() || owned->second != batch) return false;
  if (operations_.IsExpired(batch->owner, batch->generation, Now())) {
    Complete(batch->owner, false, "Native operation timed out");
    return false;
  }
  for (size_t i = 0; i < batch->frames.size(); ++i) {
    const auto& frame = batch->frames[i];
    if (frame.dropped) continue;
    const auto current = ReferenceDocument(frame.work->document.frame);
    const auto actual = frame_states_.find(frame.work->document.frame);
    if (!current || *current != frame.work->document ||
        (i && (actual == frame_states_.end() ||
               actual->second.parent != batch->frames[frame.parent].work->document.frame))) {
      // Merging and publication stay all-or-nothing; the caller captures again.
      if (!i || batch->merging) {
        Complete(batch->owner, false, "Native aggregate snapshot frame document changed");
        return false;
      }
      ReferenceFramesDrop(batch, i);
    }
  }
  return true;
}
// Drops a child frame with its descendants; their refs are never published.
void ReferenceFramesDrop(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch, size_t index) {
  ReferenceFramesDropOnly(batch, index);
  if (batch->active_edge && ((batch->active && batch->frames[batch->active].dropped) ||
                             batch->frames[batch->active_parent].dropped))
    ReferenceFramesAbandon(batch);
}
// The in-flight child edge produced nothing usable. Retire its callbacks and
// continue with the next edge from a fresh task.
void ReferenceFramesAbandon(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch) {
  if (!batch->active_edge) return;
  if (batch->active) ReferenceFramesDropOnly(batch, batch->active);
  batch->active_edge = false;
  batch->active = 0;
  ++batch->edge_seq;
  ax_captures_.erase(batch->owner);
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak = batch;
  CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), weak] {
                       if (const auto batch = weak.lock();
                           batch && self->ReferenceFramesCurrent(batch))
                         self->ReferenceFramesNext(batch);
                     }),
                     0);
}
void ReferenceFramesDropOnly(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,
                             size_t index) {
  for (size_t i = index; i < batch->frames.size(); ++i)
    if (i == index || batch->frames[batch->frames[i].parent].dropped)
      batch->frames[i].dropped = true;
}
// Source::Complete asks this first: a failure while a child edge is being
// captured, with the root document still current and time left, costs that
// child only.
bool ReferenceFramesChildFailed(int owner) {
  const auto found = reference_frame_batches_.find(owner);
  if (found == reference_frame_batches_.end()) return false;
  const auto batch = found->second;
  if (closing_page_ || renderer_failed_ || !batch->active_edge ||
      !operations_.IsCurrent(owner, batch->generation) ||
      operations_.IsExpired(owner, batch->generation, Now()))
    return false;
  const auto& root = batch->frames.front().work->document;
  const auto current = ReferenceDocument(root.frame);
  if (!current || *current != root) return false;
  ReferenceFramesAbandon(batch);
  return true;
}
bool ReferenceFrameLive(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch, size_t index) {
  return ReferenceFramesCurrent(batch) && !batch->frames[index].dropped;
}
bool ReferenceFrameOperationCurrent(int owner) {
  const auto found = reference_frame_batches_.find(owner);
  return found == reference_frame_batches_.end() || ReferenceFramesCurrent(found->second);
}
// Source calls only after any real network-publication hold has drained and
// immediately before Finish. A timed-out/changed held result never evicts old refs.
bool PublishReferenceFrameBatch(int owner) {
  const auto found = reference_frame_batches_.find(owner);
  if (found == reference_frame_batches_.end()) return true;
  const auto batch = found->second;
  if (!ReferenceFramesCurrent(batch)) return false;
  if (!batch->publication_ready) {
    Complete(owner, false, "Native aggregate snapshot publication was not prepared");
    return false;
  }
  // A child that changed after its merge stays in the text; its refs go stale.
  std::vector<uint64_t> tickets;
  for (size_t i = 0; i < batch->frames.size(); ++i)
    if (!batch->frames[i].dropped && batch->publication_tickets[i])
      tickets.push_back(batch->publication_tickets[i]);
  if (!SnapshotReferences().CommitBatch(tickets, Now())) {
    Complete(owner, false,
             operations_.IsExpired(owner, batch->generation, Now())
                 ? "Native operation timed out"
                 : "Native aggregate snapshot publication generation changed");
    return false;
  }
  return true;
}
void ReferenceFramesAttach(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,
                           const std::shared_ptr<SnapshotReferenceCapture>& work, size_t index) {
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak = batch;
  work->all_current = [this, weak, index] {
    const auto batch = weak.lock();
    return batch && ReferenceFrameLive(batch, index);
  };
  work->before_request = [this, weak] {
    const auto batch = weak.lock();
    return batch && ReferenceFramesCurrent(batch);
  };
}
void ReferenceFramesFinish(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch) {
  if (!ReferenceFramesCurrent(batch)) return;
  // Parents precede children, so one pass finds every frame whose refs the
  // root forest now holds: the root and each child merged under one.
  std::vector<std::vector<std::string>> groups;
  std::vector<uint64_t> tickets(batch->frames.size(), 0);
  std::vector<bool> included(batch->frames.size(), false);
  for (size_t i = 0; i < batch->frames.size(); ++i) {
    const auto& frame = batch->frames[i];
    included[i] = !i || (frame.merged && included[frame.parent]);
    if (!included[i]) continue;
    const auto group = SnapshotReferences().GroupFor(frame.work->ticket);
    if (!group) {
      Complete(batch->owner, false, "Native aggregate snapshot group unavailable");
      return;
    }
    groups.push_back(group->references);
    tickets[i] = group->ticket;
  }
  std::string error;
  if (!suitecut::SnapshotReferenceForests(batch->frames.front().forest, groups, error)) {
    Complete(batch->owner, false, error);
    return;
  }
  const auto root = batch->frames.front().work;
  const auto group = SnapshotReferences().GroupFor(root->ticket);
  auto params = CefDictionaryValue::Create();
  params->SetString("objectId", group->array);
  params->SetString("objectGroup", group->name);
  params->SetBool("returnByValue", true);
  const std::string boxes = root->boxes ? "true" : "false",
                    options = "{mode:'ai',boxes:" + boxes +
                              (root->depth < 0 ? std::string()
                                               : ",depth:" + std::to_string(root->depth)) +
                              "}";
  // An interactive snapshot is formatted here too, so only its text leaves the page.
  const std::string format =
      root->interactive
          ? "(" + std::string(kSuiteCutNativeAriaInteractive) + ")((" + kSuiteCutNativeAriaJSON +
                ")(this.forest," + options + "),{boxes:" + boxes + "})"
          : "(" + std::string(root->json ? kSuiteCutNativeAriaJSON : kSuiteCutNativeAriaFormatter) +
                ")(this.forest," + options + ")";
  params->SetString("functionDeclaration", "function(){return JSON.stringify(" + format + ");}");
  ReferenceRequest(
      root, "Runtime.callFunctionOn", params,
      [this, batch, tickets = std::move(tickets)](Dict result) {
        if (!ReferenceFramesCurrent(batch)) return;
        const auto value = result->GetDictionary("result");
        if (!value || value->GetType("value") != VTYPE_STRING) {
          Complete(batch->owner, false,
                   "Native aggregate snapshot serialized observation unavailable");
          return;
        }
        const auto snapshot = value->GetString("value").ToString();
        const auto root = batch->frames.front().work;
        // JSON.stringify of our own formatter; the host parses and checks it.
        const bool tree = root->json && !root->interactive;
        const char open = tree ? '[' : '"', close = tree ? ']' : '"';
        if (snapshot.size() < 2 || snapshot.front() != open || snapshot.back() != close) {
          Complete(batch->owner, false, "Native aggregate snapshot serialization is invalid");
          return;
        }
        auto response = CefDictionaryValue::Create();
        response->SetString(tree ? "snapshotJSON" : "yamlJSON", snapshot);
        const auto json = Json(response);
        if (!ReferenceFramesCurrent(batch)) return;
        batch->publication_tickets = tickets;
        batch->publication_ready = true;
        Complete(batch->owner, true, "", json);
      });
}
void ReferenceFramesMerge(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch) {
  if (!ReferenceFramesCurrent(batch)) return;
  if (!batch->merge) {
    ReferenceFramesFinish(batch);
    return;
  }
  const size_t index = batch->merge--;
  const auto child = batch->frames[index];
  if (child.dropped || child.forest.empty()) {
    ReferenceFramesMerge(batch);
    return;
  }
  const auto parent = batch->frames[child.parent].work;
  const auto group = SnapshotReferences().GroupFor(parent->ticket);
  if (!group) {
    Complete(batch->owner, false, "Native aggregate snapshot parent group unavailable");
    return;
  }
  auto params = CefDictionaryValue::Create();
  params->SetString("objectId", group->array);
  params->SetString("objectGroup", group->name);
  params->SetBool("returnByValue", true);
  params->SetString("functionDeclaration", kSuiteCutSnapshotReferenceMerge);
  auto args = CefListValue::Create();
  auto ref = CefDictionaryValue::Create();
  ref->SetString("value", child.owner_ref);
  args->SetDictionary(0, ref);
  auto forest = CefDictionaryValue::Create();
  forest->SetString("value", child.forest);
  args->SetDictionary(1, forest);
  params->SetList("arguments", args);
  ReferenceRequest(parent, "Runtime.callFunctionOn", params,
                   [this, batch, index, parent_index = child.parent](Dict result) {
                     const auto value = result->GetDictionary("result");
                     if (!value || value->GetType("value") != VTYPE_STRING) {
                       Complete(batch->owner, false, "Native aggregate snapshot merge unavailable");
                       return;
                     }
                     const auto forest = value->GetString("value").ToString();
                     batch->frames[parent_index].forest = forest;
                     batch->frames[index].merged = true;
                     ReferenceFramesMerge(batch);
                   });
}
void ReferenceFramesPrepared(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,
                             size_t index) {
  if (!ReferenceFrameLive(batch, index)) return;
  const auto work = batch->frames[index].work;
  const auto group = SnapshotReferences().GroupFor(work->ticket);
  if (!group) {
    Complete(batch->owner, false, "Native aggregate snapshot group unavailable");
    return;
  }
  const auto references = group->references;
  auto params = CefDictionaryValue::Create();
  params->SetString("objectId", group->array);
  params->SetString("objectGroup", group->name);
  params->SetBool("returnByValue", true);
  params->SetString("functionDeclaration", kSuiteCutSnapshotReferencePublish);
  auto args = CefListValue::Create();
  auto arg = CefDictionaryValue::Create();
  auto tokens = CefListValue::Create();
  for (size_t i = 0; i < references.size(); ++i) tokens->SetString(i, references[i]);
  arg->SetList("value", tokens);
  args->SetDictionary(0, arg);
  params->SetList("arguments", args);
  ReferenceRequest(
      work, "Runtime.callFunctionOn", params, [this, batch, index, references](Dict result) {
        if (!ReferenceFrameLive(batch, index)) return;
        const auto value = result->GetDictionary("result");
        if (!value || value->GetType("value") != VTYPE_STRING) {
          Complete(batch->owner, false, "Native aggregate snapshot prepared forest unavailable");
          return;
        }
        const auto forest = value->GetString("value").ToString();
        std::vector<size_t> frames;
        std::string error;
        if (!suitecut::SnapshotReferenceFrameIndices(forest, references, frames, error)) {
          Complete(batch->owner, false, error);
          return;
        }
        batch->frames[index].forest = forest;
        for (const auto node : frames) batch->edges.push_back({index, node});
        if (index) {
          batch->active_edge = false;
          batch->active = 0;
        }
        ReferenceFramesNext(batch);
      });
}
void ReferenceFramesCapture(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch, uint64_t seq,
                            size_t parent_index, size_t node_index, const std::string& child,
                            bool attached = false) {
  if (!ReferenceFramesCurrent(batch) || batch->edge_seq != seq || !batch->active_edge) return;
  const auto parent = batch->frames[parent_index].work;
  const auto frame = frame_states_.find(child);
  if (frame == frame_states_.end()) {
    // A real DOM owner can be observed before its target attachment is admitted.
    // Once actual attachment was observed, absence is genuine disappearance.
    if (attached) {
      Complete(batch->owner, false, "Native aggregate snapshot child frame detached");
      return;
    }
  } else if (frame->second.parent != parent->document.frame) {
    Complete(batch->owner, false, "Native aggregate snapshot child frame parent changed");
    return;
  }
  if (frame != frame_states_.end()) {
    attached = true;
    EnsureUtilityWorld(child);
  }
  const auto document = ReferenceDocument(child);
  if (!document) {
    const std::weak_ptr<SnapshotReferenceFrameBatch> weak = batch;
    CefPostDelayedTask(
        TID_UI,
        new Task(
            [self = CefRefPtr<Source>(this), weak, seq, parent_index, node_index, child, attached] {
              if (const auto batch = weak.lock())
                self->ReferenceFramesCapture(batch, seq, parent_index, node_index, child, attached);
            }),
        10);
    return;
  }
  if (!batch->seen.insert(child).second) {
    Complete(batch->owner, false, "Native aggregate snapshot frame cycle");
    return;
  }
  auto work = std::make_shared<SnapshotReferenceCapture>();
  work->owner = batch->owner;
  work->generation = batch->generation;
  work->document = *document;
  const auto root = batch->frames.front().work;
  work->json = root->json;
  work->boxes = root->boxes;
  work->depth = root->depth;
  const size_t index = batch->frames.size();
  ReferenceFramesAttach(batch, work, index);
  batch->frames.push_back(
      {work, "", parent_index,
       SnapshotReferences().GroupFor(parent->ticket)->references.at(node_index)});
  batch->active = index;
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak = batch;
  work->installed_done = [this, weak, index] {
    if (const auto batch = weak.lock()) ReferenceFramesPrepared(batch, index);
  };
  ReferenceFramesReadChild(batch, index);
}
void ReferenceFramesReadChild(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,
                              size_t index) {
  if (!ReferenceFrameLive(batch, index)) return;
  const auto work = batch->frames[index].work;
  const auto child = work->document.frame;
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak = batch;
  auto config = CefDictionaryValue::Create();
  config->SetString("frame", child);
  config->SetString("selector", ":root > body, :root > frameset");
  if (work->json) config->SetString("referenceFormat", "json");
  config->SetBool("accessibilityBoxes", work->boxes);
  CaptureDOMSemantic(
      batch->owner, config, [](const std::string&, const std::string&) {},
      [this, weak, index](const std::string& capture) {
        const auto batch = weak.lock();
        if (!batch || !ReferenceFrameLive(batch, index)) return;
        if (!ReferenceTemporaryGroups(batch->owner)) {
          Complete(batch->owner, false,
                   "Native aggregate snapshot temporary-group ownership unavailable");
          return;
        }
        const auto work = batch->frames[index].work;
        work->capture = capture;
        // A loading document can temporarily have no body. Reobserve only this
        // same pinned document under the unchanged original native deadline.
        ReadReferenceSnapshot(work, [this, weak, index] {
          const auto batch = weak.lock();
          if (!batch || !ReferenceFrameLive(batch, index)) return;
          CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), weak, index] {
                               if (const auto batch = weak.lock())
                                 self->ReferenceFramesReadChild(batch, index);
                             }),
                             10);
        });
      });
}
void ReferenceFramesDescribe(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch,
                             uint64_t seq, const SnapshotReferenceFrameBatch::Edge& edge) {
  if (!ReferenceFramesCurrent(batch) || batch->edge_seq != seq || !batch->active_edge) return;
  const auto work = batch->frames[edge.parent].work;
  const auto group = SnapshotReferences().GroupFor(work->ticket);
  if (!group || edge.index >= group->nodes.size()) {
    Complete(batch->owner, false, "Native aggregate snapshot iframe backend unavailable");
    return;
  }
  const int backend = group->nodes[edge.index].backend;
  auto params = CefDictionaryValue::Create();
  params->SetInt("backendNodeId", backend);
  params->SetInt("depth", 0);
  ReferenceRequest(
      work, "DOM.describeNode", params, [this, batch, seq, edge, backend](Dict result) {
        if (batch->edge_seq != seq || !batch->active_edge) return;
        const auto node = result->GetDictionary("node");
        if (!node || node->GetType("backendNodeId") != VTYPE_INT ||
            node->GetInt("backendNodeId") != backend || node->GetInt("nodeType") != 1 ||
            (node->GetString("nodeName") != "IFRAME" && node->GetString("nodeName") != "FRAME")) {
          Complete(batch->owner, false,
                   "Native aggregate snapshot iframe backend identity changed");
          return;
        }
        // An owner without a content frame (the page removed the iframe after the
        // capture) gets none later; Playwright shows such an iframe empty too.
        if (node->GetType("frameId") != VTYPE_STRING || node->GetString("frameId").empty()) {
          ReferenceFramesAbandon(batch);
          return;
        }
        ReferenceFramesCapture(batch, seq, edge.parent, edge.index,
                               node->GetString("frameId").ToString());
      });
}
void ReferenceFramesNext(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch) {
  if (!ReferenceFramesCurrent(batch)) return;
  while (batch->next < batch->edges.size() &&
         batch->frames[batch->edges[batch->next].parent].dropped)
    ++batch->next;
  if (batch->next == batch->edges.size()) {
    batch->merging = true;
    batch->merge = batch->frames.size() - 1;
    ReferenceFramesMerge(batch);
    return;
  }
  const auto edge = batch->edges[batch->next++];
  // A child that never gets a usable document, or whose renderer is busy,
  // gets a share of the time left; the rest of the page is still captured.
  const auto seq = ++batch->edge_seq;
  batch->active_edge = true;
  batch->active_parent = edge.parent;
  batch->active = 0;
  const auto operation = operations_.Get(batch->owner);
  const double left = operation && operation->deadline_ms ? operation->deadline_ms - Now() : 30000;
  ReferenceFramesWatch(batch, seq, Now() + std::max(1000.0, left / 3));
  ReferenceFramesDescribe(batch, seq, edge);
}
// Short ticks rather than one timer for the whole share: CEF holds a delayed
// task until it is due, and a pending task keeps a handover waiting long after
// the child answered.
void ReferenceFramesWatch(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch, uint64_t seq,
                          double deadline) {
  const std::weak_ptr<SnapshotReferenceFrameBatch> weak = batch;
  CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), weak, seq, deadline] {
                       if (const auto batch = weak.lock())
                         self->ReferenceFramesWatched(batch, seq, deadline);
                     }),
                     static_cast<int64_t>(std::clamp(deadline - Now(), 1.0, 50.0)));
}
void ReferenceFramesWatched(const std::shared_ptr<SnapshotReferenceFrameBatch>& batch, uint64_t seq,
                            double deadline) {
  if (batch->edge_seq != seq || !batch->active_edge || !ReferenceFramesCurrent(batch)) return;
  if (Now() >= deadline)
    ReferenceFramesAbandon(batch);
  else
    ReferenceFramesWatch(batch, seq, deadline);
}
void BeginReferenceFrameBatch(const std::shared_ptr<SnapshotReferenceCapture>& work) {
  if (!ReferenceCurrent(work)) return;
  auto batch = std::make_shared<SnapshotReferenceFrameBatch>();
  batch->owner = work->owner;
  batch->generation = work->generation;
  batch->frames.push_back({work, "", 0, ""});
  batch->seen.insert(work->document.frame);
  reference_frame_batches_[work->owner] = batch;
  ReferenceFramesAttach(batch, work, 0);
  ReferenceFramesPrepared(batch, 0);
}
