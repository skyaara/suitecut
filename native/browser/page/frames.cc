// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::FrameContextChanged(FrameState& frame) -> void {
  NativeHighlightsFrameChanged(frame.id);
  if (snapshot_references_) {
    snapshot_references_->InvalidateFrame(frame.id, Now());
    WakeDeadlines();
  }
  if (frame.context_generation == std::numeric_limits<uint64_t>::max()) {
    Error("Native execution context generation exhausted");
    return;
  }
  ++frame.context_generation;
}

auto Source::FrameJson(const FrameState& frame) -> Dict {
  auto info = CefDictionaryValue::Create();
  info->SetString("id", frame.id);
  info->SetString("parentId", frame.parent);
  suitecut::InboundSetString(info, "name", frame.name);
  info->SetString("url", frame.url);
  info->SetDouble("navigationSequence", static_cast<double>(frame.navigation));
  return info;
}

auto Source::JournalEvent(uint64_t sequence, const std::string& event, const std::string& json)
    -> void {
  while (!event_history_.empty() &&
         (event_history_.size() >= 128 || event_history_bytes_ + json.size() > 4 * 1048576)) {
    event_history_bytes_ -= event_history_.front().json.size();
    event_history_.pop_front();
  }
  event_history_.push_back({sequence, event, json});
  event_history_bytes_ += json.size();
  PumpEventPredicates();
}

auto Source::FrameEvent(const char* event, const FrameState& frame) -> void {
  if (settings_bootstrap_) return;
  if (std::string(event) == "framenavigated" && frame.id == main_frame_id_) ConsoleNavigation();
  if (!ready_) return;
  const auto sequence = ++event_sequence_;
  auto header = Header("event");
  header->SetString("event", event);
  header->SetDictionary("frame", FrameJson(frame));
  header->SetDouble("sequence", static_cast<double>(sequence));
  JournalEvent(sequence, event, Json(FrameJson(frame)));
  SendPacket(header);
  std::vector<int> waiters;
  for (const auto& [id, name] : event_waiters_)
    if (name == event) waiters.push_back(id);
  for (const int id : waiters) Complete(id, true, "", Json(FrameJson(frame)));
}

auto Source::StoreFrame(Dict info, const char* event, const std::string& session, bool restored)
    -> void {
  const auto id = info->GetString("id").ToString();
  auto parent = info->GetString("parentId").ToString();
  if (!session.empty() && parent.empty()) {
    const auto existing = frame_states_.find(id);
    if (existing != frame_states_.end()) parent = existing->second.parent;
  }
  std::string name;
  if (!suitecut::ReadInspectorFrameName(info, name)) {
    Error("Native frame name metadata encoding or unit limit exceeded");
    return;
  }
  // Chromium reports a committed frame's fragment separately from its URL.
  const auto url = info->GetString("url").ToString() + info->GetString("urlFragment").ToString();
  if (id.empty() || id.size() > 128 || parent.size() > 128 || url.size() > 8192 ||
      (!frame_states_.contains(id) && frame_states_.size() >= 1024)) {
    Error("Native frame metadata limit exceeded");
    return;
  }
  if (std::strcmp(event, "framenavigated") == 0) {
    NativeFrameDebuggerFrameInvalidated(id, session);
    CompleteCountsInFrame(id);
    if (bindings_ && !restored) bindings_->FrameRemoved(id);
    // Chromium can discard an old document's complete child tree without
    // emitting an individual frameDetached event for each descendant.
    std::vector<std::string> children;
    for (const auto& [child, state] : frame_states_)
      if (state.parent == id) children.push_back(child);
    for (const auto& child : children) DetachFrame(child);
  }
  auto& frame = frame_states_[id];
  frame.id = id;
  frame.parent = parent;
  frame.name = name;
  frame.url = url;
  frame.session = session;
  engine_.contexts()->PageOriginObserved(page_id_, frame.url);
  if (info->GetType("securityOrigin") == VTYPE_STRING)
    frame.origin = info->GetString("securityOrigin").ToString();
  const auto loader = info->GetString("loaderId").ToString();
  if (!loader.empty() && loader != frame.loader) {
    frame.loader = loader;
    frame.milestones.clear();
  }
  if (std::strcmp(event, "framenavigated") == 0) {
    FrameContextChanged(frame);
    if (!restored) {
      frame.context = 0;
      frame.utility_context = 0;
      frame.context_identity.clear();
      frame.utility_identity.clear();
    }
    frame.milestones.insert("commit");
    ++frame.navigation;
    InvalidateFrameHandles(id);
    upload_store_.DropFrame(id);
  }
  if (parent.empty() && session.empty()) {
    main_frame_id_ = id;
    NativeFrameDebuggerMainObserved();
  }
  if (restored && frame.context > 0 && !frame.context_identity.empty())
    NativeFrameDebuggerDefaultObserved(frame);
  if (!*event && id == main_frame_id_ && frame.url == last_cef_load_url_) {
    frame.milestones.insert("commit");
    frame.milestones.insert("DOMContentLoaded");
    frame.milestones.insert("load");
  }
  EnsureUtilityWorld(id);
  if (*event) FrameEvent(event, frame);
  if (restored) {
    if (bindings_ && frame.context)
      bindings_->ContextAvailable(FrameJson(frame), session, frame.context);
    ObserveRestoredDocument(id);
  }
  CheckNavigations();
}

auto Source::ImportFrameTree(Dict tree, const std::string& session, int observed_tree) -> void {
  auto info = tree->GetDictionary("frame");
  if (!info) return;
  StoreFrame(info, "", session);
  NativeFrameDebuggerInitialAssociated(observed_tree, info->GetString("id").ToString(), session);
  auto children = tree->GetList("childFrames");
  if (children && frame_states_.size() < 1024)
    for (size_t i = 0; i < children->GetSize(); ++i) {
      auto child = children->GetDictionary(i);
      if (child) ImportFrameTree(child, session, observed_tree);
    }
}

auto Source::MaybeReady() -> void {
  if (ready_ || !native_websocket_policy_root_ready_ || !native_worker_discovery_ready_ ||
      !private_response_ready_ ||
      (private_response_ && private_response_->parent_close_requested()) || !geolocation_ready_ ||
      !initial_empty_ready_ || settings_bootstrap_ || (settings_page_ && !settings_applied_) ||
      !authentication_ready_ || !network_policy_ready_ || !initial_loaded_ ||
      main_frame_id_.empty())
    return;
  const auto frame = frame_states_.find(main_frame_id_);
  if (frame == frame_states_.end()) return;
  if (!NativeWidgetMouseReady()) return;
  if (engine_.http_user_agent_.empty()) {
    if (http_user_agent_pending_) return;
    http_user_agent_pending_ = true;
    const int id = AllocateDevToolsId();
    if (!id || network_calls_.size() >= 1024) {
      NetworkFatal("Native HTTP user-agent discovery capacity exceeded");
      return;
    }
    network_calls_.emplace(
        id, NetworkCall{"",
                        [this](bool ok, const std::string& json) {
                          if (closing_page_) return;
                          const auto value = ok ? CefParseJSON(json, JSON_PARSER_RFC) : nullptr;
                          const auto result = value ? value->GetDictionary() : nullptr;
                          const auto agent =
                              result ? result->GetString("userAgent").ToString() : "";
                          if (agent.empty() || agent.size() > 8192) {
                            NetworkFatal("Native browser user-agent discovery failed");
                            return;
                          }
                          engine_.http_user_agent_ = agent;
                          MaybeReady();
                        },
                        Now() + 30000});
    WakeDeadlines();
    if (!SubmitDevTools(id, "Browser.getVersion", CefDictionaryValue::Create(), "")) {
      network_calls_.erase(id);
      NetworkFatal("Native browser user-agent discovery dispatch failed");
    }
    return;
  }
  if (!PrepareInitialHistory()) return;
  if (!engine_.StartupReady(page_id_)) return;
  native_creation_handler_receipt_.reset();
  ready_ = true;
  auto header = Header("ready");
  header->SetDouble("timestampMs", Now());
  header->SetString("cefVersion", CEF_VERSION);
  header->SetInt("captureWidth", capture_width_);
  header->SetInt("captureHeight", capture_height_);
  auto viewport = CefDictionaryValue::Create();
  viewport->SetInt("width", width_);
  viewport->SetInt("height", height_);
  if (context_settings_ && context_settings_->no_fixed_viewport)
    header->SetNull("viewport");
  else
    header->SetDictionary("viewport", viewport);
  auto capabilities = suitecut::NativeBackendCapabilities();
  capabilities->SetBool("originalWidgetInputReceipts",
                        native_widget_mouse_ && native_widget_mouse_->available());
  header->SetDictionary("capabilities", capabilities);
  if (const auto video = engine_.VideoMetadata(page_id_)) header->SetDictionary("video", video);
  header->SetDictionary("mainFrame", FrameJson(frame->second));
  SendPacket(header);
  FlushDownloadEvents();
  if (page_id_ == 1) reader_ = std::thread([this] { ReadCommands(); });
  EmitFrame();
}

auto Source::DetachFrame(const std::string& root) -> void {
  std::set<std::string> detached{root};
  for (size_t pass = 0; pass < frame_states_.size(); ++pass) {
    const auto before = detached.size();
    for (const auto& [id, frame] : frame_states_)
      if (detached.contains(frame.parent)) detached.insert(id);
    if (detached.size() == before) break;
  }
  for (const auto& frame : detached) {
    const auto known = frame_states_.find(frame);
    if (known != frame_states_.end())
      NativeFrameDebuggerFrameInvalidated(frame, known->second.session);
    NativePristineFrameDetached(frame);
  }
  if (element_states_)
    for (const auto& frame : detached) element_states_->FrameRemoved(frame);
  if (snapshot_references_) {
    for (const auto& frame : detached) snapshot_references_->InvalidateFrame(frame, Now());
    WakeDeadlines();
  }
  if (network_)
    for (const auto& frame : detached) network_->FrameRemoved(frame);
  std::vector<int> canceled;
  for (const auto& [id, frame] : operation_frames_)
    if (detached.contains(frame)) canceled.push_back(id);
  for (const int id : canceled) {
    const auto input = input_work_.find(id);
    // A synchronous click handler may remove its own frame. Once the final
    // release has actually entered Chromium, retain the original operation
    // until its input ACK and renderer barrier; detachment is not that ACK.
    if (input != input_work_.end() && input->second->final_release_submitted)
      operation_frames_.erase(id);
    else if (count_queries_.contains(id))
      CompleteCountZero(id);
    else if (visibility_snapshots_.Contains(id))
      CompleteVisibilityEmpty(id);
    else if (lazy_queries_.contains(id) && !author_query_evaluations_.contains(id))
      RetryContext(id);
    else
      Complete(id, false, "Native frame detached");
  }
  for (auto it = utility_worlds_.begin(); it != utility_worlds_.end();) {
    if (detached.contains(it->second.frame))
      it = utility_worlds_.erase(it);
    else
      ++it;
  }
  for (const auto& id : detached) {
    if (bindings_) bindings_->FrameRemoved(id);
    InvalidateFrameHandles(id);
    upload_store_.DropFrame(id);
    const auto found = frame_states_.find(id);
    if (found != frame_states_.end()) {
      NetworkActivity(found->second.parent);
      FrameEvent("framedetached", found->second);
      frame_states_.erase(found);
    }
  }
}

auto Source::InvalidateFrameHandles(const std::string& frame) -> void {
  NativeHighlightsFrameChanged(frame);
  ConsoleFrameRemoved(frame);
  for (auto it = handles_.begin(); it != handles_.end();) {
    if (it->second.frame_id != frame) {
      ++it;
      continue;
    }
    auto params = CefDictionaryValue::Create();
    params->SetString("objectGroup", it->second.group);
    if (browser_) SendDevTools("Runtime.releaseObjectGroup", params, it->second.session);
    it = handles_.erase(it);
  }
}

auto Source::FrameId(Dict message) -> std::string {
  return message->GetType("frame") == VTYPE_STRING ? message->GetString("frame").ToString()
                                                   : main_frame_id_;
}

auto Source::EnsureUtilityWorld(const std::string& id) -> void {
  const auto frame = frame_states_.find(id);
  if (frame == frame_states_.end() || !frame->second.context || frame->second.utility_context)
    return;
  for (const auto& [call, world] : utility_worlds_)
    if (world.frame == id && world.session == frame->second.session &&
        world.loader == frame->second.loader)
      return;
  if (utility_worlds_.size() >= 1024) {
    Error("Native DOM execution world limit exceeded");
    return;
  }
  const int call = AllocateDevToolsId();
  if (!call) {
    Error("Native DOM execution world command unavailable");
    return;
  }
  auto params = CefDictionaryValue::Create();
  params->SetString("frameId", id);
  params->SetString("worldName", "__suitecut_dom__");
  utility_worlds_[call] = {id, frame->second.session, frame->second.loader};
  if (!SubmitDevTools(call, "Page.createIsolatedWorld", params, frame->second.session)) {
    utility_worlds_.erase(call);
    Error("Native DOM execution world dispatch failed");
  }
}

auto Source::WithFrame(int owner, const std::string& frame, std::function<void(int)> action,
                       bool utility) -> void {
  const auto operation = operations_.Get(owner);
  if (!operation || !browser_) return;
  if (!ReferenceFrameOperationCurrent(owner)) return;
  if (element_bounds_operations_.contains(owner) &&
      operations_.IsExpired(owner, operation->generation, Now())) {
    Complete(owner, false, "boundingBox: Native operation timed out");
    return;
  }
  if (const auto bounds = element_bounds_work_.find(owner); bounds != element_bounds_work_.end())
    if (const auto work = bounds->second; work && !ElementBoundsCurrent(work)) return;
  if (locator_predicate_operations_.contains(owner) &&
      operations_.IsExpired(owner, operation->generation, Now())) {
    Complete(owner, false, "waitForFunction: Native operation timed out");
    return;
  }
  if (const auto predicate = locator_predicate_work_.find(owner);
      predicate != locator_predicate_work_.end())
    if (!LocatorPredicateCurrent(predicate->second)) return;
  if (aria_json_operations_.contains(owner) &&
      operations_.IsExpired(owner, operation->generation, Now())) {
    Complete(owner, false, "ariaSnapshotJSON: Native operation timed out");
    return;
  }
  if (const auto snapshot = aria_json_work_.find(owner); snapshot != aria_json_work_.end())
    if (!AriaJSONCurrent(snapshot->second)) return;
  const auto found = frame_states_.find(frame);
  if (found == frame_states_.end()) {
    if (count_queries_.contains(owner))
      CompleteCountZero(owner);
    else if (visibility_snapshots_.Contains(owner))
      CompleteVisibilityEmpty(owner);
    else if (lazy_queries_.contains(owner))
      RetryContext(owner);
    else
      Complete(owner, false, "Native frame detached or unavailable");
    return;
  }
  operation_frames_[owner] = frame;
  operation_sessions_[owner] = found->second.session;
  if (utility) EnsureUtilityWorld(frame);
  // Count observes the current document once; it does not wait for a new one.
  const int context = utility && (!count_queries_.contains(owner) || found->second.utility_context)
                          ? found->second.utility_context
                          : found->second.context;
  if (visibility_snapshots_.Contains(owner)) {
    const auto identity = context == found->second.utility_context ? found->second.utility_identity
                                                                   : found->second.context_identity;
    if (!visibility_snapshots_.Bind(owner, operation->generation, frame, found->second.session,
                                    found->second.loader, context, identity)) {
      Complete(owner, false, "Native visibility snapshot scope unavailable");
      return;
    }
    // A live document can be waiting for its utility world to initialize.
    // Absence of that utility world alone is not proof of an empty document.
    if (!context && !found->second.context) {
      CompleteVisibilityEmpty(owner);
      return;
    }
  }
  if (context > 0) {
    if (count_queries_.contains(owner)) count_contexts_[owner] = {found->second.session, context};
    action(context);
    return;
  }
  if (count_queries_.contains(owner)) {
    CompleteCountZero(owner);
    return;
  }
  CefPostDelayedTask(
      TID_UI,
      new Task([self = CefRefPtr<Source>(this), owner, generation = operation->generation, frame,
                action = std::move(action), utility] {
        if (self->operations_.IsCurrent(owner, generation))
          self->WithFrame(owner, frame, action, utility);
      }),
      10);
}

auto Source::ResolveFramePath(int id, uint64_t generation, Dict message, size_t index,
                              const std::string& frame, std::function<void(Dict)> done,
                              std::function<void()> unavailable) -> void {
  if (!operations_.IsCurrent(id, generation)) return;
  auto path = message->GetList("framePath");
  if (!path || index == path->GetSize()) {
    auto resolved = message->Copy(false);
    resolved->SetString("frame", frame);
    resolved->Remove("framePath");
    done(resolved);
    return;
  }
  if (path->GetSize() > 32 || path->GetType(index) != VTYPE_LIST) {
    Complete(id, false, "Invalid native frame locator path");
    return;
  }
  auto stage = CefDictionaryValue::Create();
  stage->SetList("query", message->GetList("framePath")->GetList(index)->Copy());
  const bool utility = !QueryNeedsMainWorld(stage);
  WithFrame(
      id, frame,
      [this, id, generation, message, index, frame, stage, done = std::move(done),
       unavailable](int context) {
        auto params = CefDictionaryValue::Create();
        params->SetInt("contextId", context);
        params->SetString("expression",
                          "(()=>{const nodes=" + QueryExpression(stage) +
                              ";if(!nodes.length)return null;if(nodes.length>1)throw new "
                              "Error('Strict frame locator matched multiple "
                              "elements');if(!['iframe','frame'].includes(nodes[0].localName))"
                              "throw new Error('Frame locator resolved to an element that is not "
                              "an iframe');return nodes[0];})()");
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        evaluation_groups_[id].insert(operation_sessions_[id]);
        auto callback = [this, id, generation, message, index, frame, done, unavailable](
                            bool, const std::string& json) {
          auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
          auto result = parsed ? parsed->GetDictionary() : nullptr;
          auto node = result ? result->GetDictionary("result") : nullptr;
          if (!node || result->HasKey("exceptionDetails")) {
            Complete(id, false, "Native frame locator resolution failed");
            return;
          }
          if (node->GetString("subtype") == "null") {
            if (unavailable) {
              unavailable();
              return;
            }
            if (count_queries_.contains(id)) {
              CompleteCountZero(id);
              return;
            }
            if (visibility_snapshots_.Contains(id)) {
              CompleteVisibilityEmpty(id);
              return;
            }
            CefPostDelayedTask(TID_UI,
                               new Task([self = CefRefPtr<Source>(this), id, generation, message,
                                         index, frame, done, unavailable] {
                                 self->ResolveFramePath(id, generation, message, index, frame, done,
                                                        unavailable);
                               }),
                               20);
            return;
          }
          if (node->GetType("objectId") != VTYPE_STRING) {
            Complete(id, false, "Native iframe owner unavailable");
            return;
          }
          auto params = CefDictionaryValue::Create();
          params->SetString("objectId", node->GetString("objectId"));
          DevTools(id, "DOM.describeNode", params,
                   [this, id, generation, message, index, frame, done, unavailable](
                       bool, const std::string& json) {
                     auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                     auto result = parsed ? parsed->GetDictionary() : nullptr;
                     auto node = result ? result->GetDictionary("node") : nullptr;
                     const auto child = node ? node->GetString("frameId").ToString() : "";
                     if (child.empty() || !frame_states_.contains(child)) {
                       if (unavailable) {
                         unavailable();
                         return;
                       }
                       if (count_queries_.contains(id)) {
                         CompleteCountZero(id);
                         return;
                       }
                       if (visibility_snapshots_.Contains(id)) {
                         CompleteVisibilityEmpty(id);
                         return;
                       }
                       CefPostDelayedTask(TID_UI,
                                          new Task([self = CefRefPtr<Source>(this), id, generation,
                                                    message, index, frame, done, unavailable] {
                                            self->ResolveFramePath(id, generation, message, index,
                                                                   frame, done, unavailable);
                                          }),
                                          20);
                       return;
                     }
                     ResolveFramePath(id, generation, message, index + 1, child, done, unavailable);
                   });
        };
        if (QueryUsesMultipleWorlds(stage)) {
          auto config = stage->Copy(false);
          config->SetString("frame", frame);
          QueryInspect(id, config,
                       "if(!nodes.length)return null;if(nodes.length>1)throw new Error('Strict "
                       "frame locator matched multiple "
                       "elements');if(!['iframe','frame'].includes(nodes[0].localName))throw new "
                       "Error('Frame locator resolved to an element that is not an iframe');return "
                       "nodes[0];",
                       true, false, std::move(callback));
        } else
          DevTools(id, "Runtime.evaluate", params, std::move(callback), std::nullopt, false,
                   kMaxResponseBytes, true);
      },
      utility);
}

auto Source::MapFramePoint(int id, const std::string& frame, double x, double y, double width,
                           double height, std::function<void(double, double)> done) -> void {
  if (!operations_.Contains(id)) return;
  const auto current = frame_states_.find(frame);
  if (current == frame_states_.end()) {
    Complete(id, false, "Native frame detached");
    return;
  }
  if (current->second.parent.empty()) {
    done(x, y);
    return;
  }
  const auto parent = frame_states_.find(current->second.parent);
  if (parent == frame_states_.end() || width <= 0 || height <= 0) {
    Complete(id, false, "Native frame geometry unavailable");
    return;
  }
  const auto parent_session = parent->second.session;
  const auto root = parent_session.empty() ? main_frame_id_ : sessions_[parent_session];
  operation_sessions_[id] = parent_session;
  auto params = CefDictionaryValue::Create();
  params->SetString("frameId", frame);
  DevTools(
      id, "DOM.getFrameOwner", params,
      [this, id, root, x, y, width, height, done](bool, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
        auto result = parsed ? parsed->GetDictionary() : nullptr;
        if (!result || result->GetType("backendNodeId") != VTYPE_INT) {
          Complete(id, false, "Native frame owner geometry unavailable");
          return;
        }
        auto params = CefDictionaryValue::Create();
        params->SetInt("backendNodeId", result->GetInt("backendNodeId"));
        DevTools(
            id, "DOM.getBoxModel", params,
            [this, id, root, x, y, width, height, done](bool, const std::string& json) {
              auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
              auto result = parsed ? parsed->GetDictionary() : nullptr;
              auto model = result ? result->GetDictionary("model") : nullptr;
              auto quad = model ? model->GetList("content") : nullptr;
              if (!quad || quad->GetSize() != 8) {
                Complete(id, false, "Native frame content quad unavailable");
                return;
              }
              double q[8];
              for (size_t index = 0; index < 8; ++index)
                q[index] = quad->GetType(index) == VTYPE_INT ? quad->GetInt(index)
                                                             : quad->GetDouble(index);
              const double dx1 = q[2] - q[4], dx2 = q[6] - q[4], dx3 = q[0] - q[2] + q[4] - q[6];
              const double dy1 = q[3] - q[5], dy2 = q[7] - q[5], dy3 = q[1] - q[3] + q[5] - q[7];
              const double determinant = dx1 * dy2 - dx2 * dy1;
              double g = 0, h = 0;
              if (std::abs(dx3) > 1e-9 || std::abs(dy3) > 1e-9) {
                if (std::abs(determinant) < 1e-12) {
                  Complete(id, false, "Native frame transform is degenerate");
                  return;
                }
                g = (dx3 * dy2 - dx2 * dy3) / determinant;
                h = (dx1 * dy3 - dx3 * dy1) / determinant;
              }
              const double u = x / width, v = y / height, divisor = g * u + h * v + 1;
              if (std::abs(divisor) < 1e-12) {
                Complete(id, false, "Native frame transform is degenerate");
                return;
              }
              const double mapped_x =
                  ((q[2] - q[0] + g * q[2]) * u + (q[6] - q[0] + h * q[6]) * v + q[0]) / divisor;
              const double mapped_y =
                  ((q[3] - q[1] + g * q[3]) * u + (q[7] - q[1] + h * q[7]) * v + q[1]) / divisor;
              if (root == main_frame_id_) {
                done(mapped_x, mapped_y);
                return;
              }
              Evaluate(
                  id, "({width:innerWidth,height:innerHeight})",
                  [this, id, root, mapped_x, mapped_y, done](bool, const std::string& json) {
                    auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                    auto result = parsed ? parsed->GetDictionary() : nullptr;
                    auto remote = result ? result->GetDictionary("result") : nullptr;
                    auto viewport = remote ? remote->GetDictionary("value") : nullptr;
                    if (!viewport) {
                      Complete(id, false, "Native parent viewport unavailable");
                      return;
                    }
                    MapFramePoint(id, root, mapped_x, mapped_y, Number(viewport, "width"),
                                  Number(viewport, "height"), done);
                  },
                  root, true);
            });
      });
}

}  // namespace suitecut::browser
