// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::QueryOptions(bool metadata) -> Dict {
  auto options = CefDictionaryValue::Create();
  auto engines = CefDictionaryValue::Create();
  for (const auto& [name, entry] : engine_.selectors().entries()) {
    auto engine = CefDictionaryValue::Create();
    engine->SetString("source", entry.source);
    engine->SetBool("contentScript", entry.content_script);
    engines->SetDictionary(name, engine);
  }
  options->SetDictionary("engines", engines);
  options->SetString("registryKey", "__suitecut_selectors_" + handle_namespace_);
  if (metadata) options->SetBool("metadata", true);
  return options;
}

auto Source::QueryNeedsSelectorWorld(Dict message, bool isolated) -> bool {
  if (engine_.selectors().entries().empty()) return false;
  std::vector<CefRefPtr<CefValue>> pending;
  if (const auto query = message->GetValue("query")) pending.push_back(query);
  if ((message->HasKey("selector") || message->HasKey("selectorJSON"))) {
    const auto selector = suitecut::InboundString(message, "selector");
    const auto names = SuiteCutQueryRealms::Engines(selector);
    for (const auto& [name, entry] : engine_.selectors().entries())
      if (entry.content_script == isolated &&
          std::find(names.begin(), names.end(), name) != names.end())
        return true;
  }
  size_t visited = 0;
  while (!pending.empty() && ++visited <= 100000) {
    const auto value = pending.back();
    pending.pop_back();
    if (value->GetType() == VTYPE_LIST) {
      const auto list = value->GetList();
      for (size_t index = 0; index < list->GetSize(); ++index)
        pending.push_back(list->GetValue(index));
    } else if (value->GetType() == VTYPE_DICTIONARY) {
      const auto node = value->GetDictionary();
      if (node->GetString("engine") == "custom" && node->GetType("name") == VTYPE_STRING) {
        const auto entry = engine_.selectors().entries().find(node->GetString("name").ToString());
        if (entry != engine_.selectors().entries().end() &&
            entry->second.content_script == isolated)
          return true;
      }
      if ((node->HasKey("selector") || node->HasKey("selectorJSON"))) {
        const auto selector = suitecut::InboundString(node, "selector");
        const auto names = SuiteCutQueryRealms::Engines(selector);
        for (const auto& [name, entry] : engine_.selectors().entries())
          if (entry.content_script == isolated &&
              std::find(names.begin(), names.end(), name) != names.end())
            return true;
      }
      CefDictionaryValue::KeyList keys;
      node->GetKeys(keys);
      for (const auto& key : keys) pending.push_back(node->GetValue(key));
    }
  }
  return false;
}

auto Source::QueryNeedsMainWorld(Dict message) -> bool {
  return QueryNeedsSelectorWorld(message, false);
}

auto Source::QueryNeedsIsolatedWorld(Dict message) -> bool {
  return QueryNeedsSelectorWorld(message, true);
}

auto Source::QueryUsesMultipleWorlds(Dict message) -> bool {
  return QueryHasSnapshotReferences(message) ||
         (QueryNeedsMainWorld(message) && QueryNeedsIsolatedWorld(message));
}

auto Source::QueryNodesInMainWorld(int id, Dict message,
                                   std::function<void(const std::string&)> done) -> void {
  QueryNodesInWorld(id, message, false, std::move(done));
}

auto Source::QueryNodesInWorld(int id, Dict message, bool to_utility,
                               std::function<void(const std::string&)> done) -> void {
  if (!PrepareSnapshotReferenceScope(id, message)) return;
  const auto entry = operations_.Get(id);
  if (!entry) return;
  if (!SelectorWaits().Admit(id, entry->generation)) {
    Complete(id, false, "Native selected-node physical preparation capacity exceeded");
    return;
  }
  if (QueryBuiltinNodesInWorld(id, message, to_utility, done)) return;
  const auto frame_id = FrameId(message);
  WithFrame(
      id, frame_id,
      [this, id, generation = entry->generation, frame_id, message, to_utility,
       done = std::move(done)](int utility) mutable {
        const auto frame = frame_states_.find(frame_id);
        if (frame == frame_states_.end() || !frame->second.context) {
          Complete(id, false, "Native query main context unavailable");
          return;
        }
        const int main = frame->second.context, destination = to_utility ? utility : main;
        const auto loader = frame->second.loader, session = frame->second.session;
        const auto group = "suitecut-automation-" + std::to_string(id);
        evaluation_groups_[id].insert(session);
        const auto reference_document = QueryHasSnapshotReferences(message)
                                            ? ReferenceDocument(frame_id)
                                            : std::optional<SnapshotReferenceDocument>{};
        if (QueryHasSnapshotReferences(message) && !reference_document) {
          Complete(id, false, "Native snapshot reference plan document unavailable");
          return;
        }
        auto current = [this, id, generation, frame_id, main, utility, loader, session,
                        reference_document] {
          const auto frame = frame_states_.find(frame_id);
          return (!reference_document ||
                  ReferencePlanCurrent(id, generation, *reference_document)) &&
                 operations_.IsCurrent(id, generation) && frame != frame_states_.end() &&
                 frame->second.context == main && frame->second.utility_context == utility &&
                 frame->second.loader == loader && frame->second.session == session;
        };
        auto normalize = [this, id, generation, frame_id, message, current, main, utility,
                          destination, group, session,
                          done = std::move(done)](const std::string& root) mutable {
          auto query = message->GetValue("query");
          if (!query || query->GetType() != VTYPE_LIST) {
            auto steps = CefListValue::Create();
            auto step = CefDictionaryValue::Create();
            step->SetString("engine", "css");
            suitecut::InboundCopyString(message, step, "selector");
            steps->SetDictionary(0, step);
            query = CefValue::Create();
            query->SetList(steps);
          }
          auto options = QueryOptions();
          options->SetBool("planOnly", true);
          auto literal = CefValue::Create();
          literal->SetString(Json(options));
          auto params = CefDictionaryValue::Create();
          params->SetInt("contextId", utility);
          params->SetBool("returnByValue", true);
          params->SetString("expression",
                            "(" + std::string(kSuiteCutDomQuery) + ")(" +
                                CefWriteJSON(query, JSON_WRITER_DEFAULT).ToString() +
                                ",document,JSON.parse(" +
                                CefWriteJSON(literal, JSON_WRITER_DEFAULT).ToString() + "))");
          if (message->HasKey("nativeSelectorWaitState")) {
            const auto state = message->GetString("nativeSelectorWaitState").ToString();
            const auto expression = params->GetString("expression").ToString();
            params->SetString(
                "expression",
                "(()=>{const plan=" + expression + ";plan.push({engine:'selector-wait',state:'" +
                    state +
                    "',strict:" + std::string(message->GetBool("strict") ? "true" : "false") +
                    "});return plan;})()");
          }
          DevTools(
              id, "Runtime.evaluate", params,
              [this, id, generation, current, main, utility, destination, group, session, root,
               grouped = message->HasKey("nativeSelectorWaitState"),
               done = std::move(done)](bool ok, const std::string& json) mutable {
                if (!current()) {
                  if (operations_.IsCurrent(id, generation)) {
                    if (grouped && frame_states_.contains(operation_frames_[id]))
                      RetryContext(id);
                    else
                      Complete(id, false,
                               "Native query document changed during plan normalization");
                  }
                  return;
                }
                auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                auto result = parsed ? parsed->GetDictionary() : nullptr;
                auto remote = result ? result->GetDictionary("result") : nullptr;
                if (!ok || !result || result->HasKey("exceptionDetails") || !remote ||
                    remote->GetType("value") != VTYPE_LIST) {
                  auto details = result ? result->GetDictionary("exceptionDetails") : nullptr;
                  auto exception = details ? details->GetDictionary("exception") : nullptr;
                  Complete(id, false,
                           exception ? exception->GetString("description").ToString()
                                     : "Native selector plan normalization failed");
                  return;
                }
                SuiteCutQueryWorldPlan::Run(
                    [this, id, session](const std::string& method, Dict params,
                                        SuiteCutQueryNodeTransfer::Result result) {
                      const bool metadata = params && params->HasKey("serializationOptions");
                      DevTools(id, method, params, std::move(result), session, false,
                               metadata ? 16 * 1048576 : kMaxResponseBytes,
                               metadata || method == "Runtime.getProperties");
                    },
                    current,
                    [this, id, generation, current, grouped](const std::string& error) {
                      if (operations_.IsCurrent(id, generation)) {
                        if (grouped && !current() && frame_states_.contains(operation_frames_[id]))
                          RetryContext(id);
                        else
                          Complete(id, false, error);
                      }
                    },
                    main, utility, group, kSuiteCutDomQuery, QueryOptionsExpression(),
                    remote->GetList("value"),
                    [this](const std::string& name) {
                      const auto found = engine_.selectors().entries().find(name);
                      return found != engine_.selectors().entries().end() &&
                             found->second.content_script;
                    },
                    [this, id, generation, current, grouped, destination, group, session,
                     done = std::move(done)](SuiteCutQueryWorldPlan::Nodes nodes) mutable {
                      if (!current()) {
                        if (operations_.IsCurrent(id, generation))
                          Complete(
                              id, false,
                              "Native query document changed before selected-node publication");
                        return;
                      }
                      if (nodes.context == destination) {
                        done(nodes.object);
                        return;
                      }
                      SuiteCutQueryNodeTransfer::Transfer(
                          [this, id, session](const std::string& method, Dict params,
                                              SuiteCutQueryNodeTransfer::Result result) {
                            const bool metadata = params && params->HasKey("serializationOptions");
                            DevTools(id, method, params, std::move(result), session, false,
                                     metadata ? 16 * 1048576 : kMaxResponseBytes,
                                     metadata || method == "Runtime.getProperties");
                          },
                          current,
                          [this, id, generation, current, grouped](const std::string& error) {
                            if (operations_.IsCurrent(id, generation)) {
                              if (grouped && !current() &&
                                  frame_states_.contains(operation_frames_[id]))
                                RetryContext(id);
                              else
                                Complete(id, false, error);
                            }
                          },
                          nodes.object, destination, group, std::move(done));
                    },
                    grouped, root,
                    [this, id](const std::string& token, SuiteCutQueryWorldPlan::Nodes scope,
                               std::function<void(std::string)> done) {
                      ResolveSnapshotReference(id, token, scope, std::move(done));
                    });
              });
        };
        if (message->HasKey("nativeRootHandle")) {
          if (message->GetType("nativeRootHandle") != VTYPE_INT) {
            Complete(id, false, "Invalid native selector root handle");
            return;
          }
          const auto found = handles_.find(message->GetInt("nativeRootHandle"));
          if (found == handles_.end()) {
            Complete(id, false, MissingHandle(message->GetInt("nativeRootHandle")));
            return;
          }
          const auto& handle = found->second;
          if (handle.node_id.empty() || handle.frame_id != frame_id || handle.context != main ||
              handle.session != session) {
            Complete(id, false,
                     "Native selector root belongs to a different or destroyed DOM context");
            return;
          }
          SuiteCutQueryNodeTransfer::AdoptNode(
              [this, id, session](const std::string& method, Dict params,
                                  SuiteCutQueryNodeTransfer::Result callback) {
                DevTools(id, method, params, std::move(callback), session);
              },
              current, [this, id](const std::string& error) { Complete(id, false, error); },
              handle.node_id, utility, group,
              [normalize = std::move(normalize)](Dict node) mutable {
                normalize(node->GetString("objectId").ToString());
              });
        } else
          normalize("");
      },
      true);
}

auto Source::QueryInspect(int id, Dict message, const std::string& body, bool utility,
                          bool by_value, DevToolsCallback callback) -> void {
  QueryNodesInWorld(
      id, message, utility,
      [this, id, body, by_value, callback = std::move(callback)](const std::string& nodes) mutable {
        auto params = CefDictionaryValue::Create();
        params->SetString("objectId", nodes);
        params->SetString("functionDeclaration", "async function(){const nodes=this;" + body + "}");
        params->SetBool("awaitPromise", true);
        params->SetBool("returnByValue", by_value);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        DevTools(id, "Runtime.callFunctionOn", params, std::move(callback), std::nullopt, false,
                 kMaxResponseBytes, !by_value);
      });
}

auto Source::QueryOptionsExpression(bool metadata) -> std::string {
  auto literal = CefValue::Create();
  literal->SetString(Json(QueryOptions(metadata)));
  return "JSON.parse(" + CefWriteJSON(literal, JSON_WRITER_DEFAULT).ToString() + ")";
}

auto Source::QueryDiagnosticExpression(Dict message, const std::string& nodes) -> std::string {
  const auto expression = message->GetType("diagnosticExpressionJSON") == VTYPE_STRING &&
                                  message->GetString("diagnosticExpressionJSON").length() <= 16384
                              ? message->GetString("diagnosticExpressionJSON").ToString()
                              : std::string("\"\"");
  auto query = message->GetList("query");
  if (!query) query = CefListValue::Create();
  auto value = CefValue::Create();
  value->SetList(query->Copy());
  const auto original = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  return "(()=>{const query=(" + std::string(kSuiteCutDomQuery) +
         "),budget={remaining:40000},options={nativeDiagnosticsBudget:budget},tools=query([],"
         "document,{...options,accessibilityTools:true});return (" +
         std::string(kSuiteCutQueryDiagnostics) + ")(" + nodes + ",JSON.parse(" +
         Literal(expression) + "),{originalQuery:JSON.parse(" + Literal(original) +
         "),query:steps=>query(steps,document,options),role:tools.role,name:tools.name,remaining:()"
         "=>budget.remaining});})()";
}

auto Source::QueryExpression(Dict message, const std::string& native_query_intrinsics)
    -> std::string {
  if (QueryNeedsMainWorld(message) && QueryNeedsIsolatedWorld(message))
    return "(()=>{throw new Error('Mixed main and isolated native custom selector worlds require "
           "native step execution');})()";
  auto query = message->GetValue("query");
  if (!query || query->GetType() != VTYPE_LIST) {
    auto list = CefListValue::Create();
    auto step = CefDictionaryValue::Create();
    step->SetString("engine", "css");
    suitecut::InboundCopyString(message, step, "selector");
    list->SetDictionary(0, step);
    query = CefValue::Create();
    query->SetList(list);
  }
  // The optional fourth argument is Source-authored, never an IPC/options
  // property that author code can inherit or provide.
  return "(" + std::string(kSuiteCutDomQuery) + ")(" +
         CefWriteJSON(query, JSON_WRITER_DEFAULT).ToString() + ",document," +
         QueryOptionsExpression() +
         (native_query_intrinsics.empty() ? std::string() : "," + native_query_intrinsics) + ")";
}

auto Source::EvaluateFixedNode(int id, Dict config, const std::string& expression,
                               DevToolsCallback callback) -> void {
  const int handle = config->GetInt("nativeHandle");
  const auto found = handles_.find(handle);
  if (found == handles_.end()) {
    Complete(id, false, MissingHandle(handle));
    return;
  }
  if (found->second.node_id.empty()) {
    Complete(id, false, "Native handle is not a DOM node");
    return;
  }
  const auto frame_id = found->second.frame_id;
  const auto node_id = found->second.node_id;
  WithFrame(
      id, frame_id,
      [this, id, node_id, expression, callback = std::move(callback)](int context) mutable {
        evaluation_groups_[id].insert(operation_sessions_[id]);
        auto params = CefDictionaryValue::Create();
        params->SetString("objectId", node_id);
        DevTools(id, "DOM.describeNode", params,
                 [this, id, context, expression, callback = std::move(callback)](
                     bool, const std::string& json) mutable {
                   auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                   auto result = parsed ? parsed->GetDictionary() : nullptr;
                   auto node = result ? result->GetDictionary("node") : nullptr;
                   if (!node) {
                     Complete(id, false, "Native DOM node unavailable");
                     return;
                   }
                   auto params = CefDictionaryValue::Create();
                   params->SetInt("backendNodeId", node->GetInt("backendNodeId"));
                   params->SetInt("executionContextId", context);
                   params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
                   DevTools(id, "DOM.resolveNode", params,
                            [this, id, expression, callback = std::move(callback)](
                                bool, const std::string& json) mutable {
                              auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                              auto result = parsed ? parsed->GetDictionary() : nullptr;
                              auto node = result ? result->GetDictionary("object") : nullptr;
                              if (!node || node->GetType("objectId") != VTYPE_STRING) {
                                Complete(id, false, "Native DOM node resolution failed");
                                return;
                              }
                              auto params = CefDictionaryValue::Create();
                              params->SetString("objectId", node->GetString("objectId"));
                              params->SetString(
                                  "functionDeclaration",
                                  "async function(){return await(" + expression + ");}");
                              params->SetBool("awaitPromise", true);
                              params->SetBool("returnByValue", true);
                              params->SetString("objectGroup",
                                                "suitecut-automation-" + std::to_string(id));
                              DevTools(id, "Runtime.callFunctionOn", params, std::move(callback));
                            });
                 });
      },
      true);
}

auto Source::LocatorAction(int id, std::uint64_t generation, const std::string& selector,
                           const std::string& action, const std::string& text, Dict config,
                           std::shared_ptr<InputWork> pointer_work) -> void {
  if (!operations_.IsCurrent(id, generation) || !browser_) return;
  if (pointer_work && operations_.IsExpired(id, generation, Now())) {
    Complete(id, false, "Native pointer original deadline expired before press");
    return;
  }
  // A renderer/context retry can race the delayed preparation retry. Once a
  // pointer owner is queued, its final recheck owns preparation; never admit
  // a second gesture for the same still-live operation.
  if (!pointer_work && action == "click" && input_work_.contains(id)) return;
  if (action == "press" || action == "type") {
    if (operations_.IsExpired(id, generation, Now())) {
      Complete(id, false, "Native targeted keyboard original deadline expired");
      return;
    }
    const auto found = input_work_.find(id);
    if (found == input_work_.end()) {
      BeginTargetedKeyboard(id, generation, config, action, text);
      return;
    }
    if (found->second->generation != generation || !found->second->keyboard_target ||
        found->second->action != action) {
      Complete(id, false, "Native targeted keyboard owner mismatch");
      return;
    }
    if (keyboard_input_ != id) return;
  }
  if (action == "selectText") {
    SelectTextAction(id, generation, config);
    return;
  }
  if (action == "blur") {
    BlurAction(id, generation, config);
    return;
  }
  if (action == "dispatchEvent") {
    DispatchEventAction(id, generation, config);
    return;
  }
  if (action == "inputValue") {
    InputValueAction(id, generation, config);
    return;
  }
  if (action == "boundingBox") {
    ElementBoundsAction(id, generation, config);
    return;
  }
  if (action == "waitForFunction") {
    LocatorPredicateAction(id, generation, config);
    return;
  }
  if (action == "ariaSnapshotJSON") {
    AriaJSONAction(id, generation, config);
    return;
  }
  if (action == "scrollIntoViewIfNeeded") {
    ScrollAction(id, generation, config);
    return;
  }
  if (action == "fill" || action == "clear") {
    if (operations_.IsExpired(id, generation, Now())) {
      Complete(id, false, action + ": Native operation timed out");
      return;
    }
    if (!SelectorWaits().Admit(id, generation)) {
      Complete(id, false, "Native editable action physical capacity exceeded");
      return;
    }
  }
  if (config->HasKey("diagnosticExpressionJSON")) {
    if (operations_.IsExpired(id, generation, Now())) {
      Complete(id, false, action + ": Native operation timed out");
      return;
    }
    if (!SelectorWaits().Admit(id, generation)) {
      Complete(id, false, "Native action diagnostic physical capacity exceeded");
      return;
    }
  }
  if (action_pauses_.Contains(id)) return;
  if (action == "sendKeys") {
    if (!input_work_.contains(id)) {
      BeginSendKeys(id, generation, config);
      return;
    }
    const auto work = input_work_.at(id);
    if (work->generation != generation) {
      Complete(id, false, "Native send-keys owner ID is physically retained");
      return;
    }
    if (keyboard_input_ != id) return;
  }
  auto path = config->GetList("framePath");
  if (path && path->GetSize()) {
    ResolveFramePath(id, generation, config, 0, FrameId(config),
                     [this, id, generation, selector, action, text, pointer_work](Dict resolved) {
                       LocatorAction(id, generation, selector, action, text, resolved,
                                     pointer_work);
                     });
    return;
  }
  if (!pointer_work && !locator_handlers_.empty() &&
      (action == "click" || action == "tap" || action == "hover" || action == "fill" ||
       action == "clear") &&
      !drag_work_.contains(id) && !config->HasKey("formNode") &&
      LocatorHandlerBefore(id, generation, config,
                           [this, id, generation, selector, action, text, config] {
                             LocatorAction(id, generation, selector, action, text, config);
                           }))
    return;
  browser_->GetHost()->SetFocus(true);
  const bool pause_before_stable = !pointer_work && config->GetBool("pauseBeforeStable") &&
                                   !config->GetBool("resumeBeforeStable");
  config->Remove("resumeBeforeStable");
  const char* alignments[] = {"'center'", "'end'", "'start'", "'nearest'"};
  const auto alignment = pointer_work ? "'nearest'" : alignments[locator_attempts_[id]++ % 4];
  // Authored DOM logic: inspect current state on each attempt. C++ owns the
  // operation, deadline, retry scheduling and input dispatch.
  const auto expression =
      std::string("(async()=>{const list=") +
      (config->HasKey("nativeHandle") || config->HasKey("formNode") ? "[this]"
       : (QueryNeedsMainWorld(config) || QueryHasSnapshotReferences(config))
           ? "this"
           : QueryExpression(config)) +
      ";"
      "if(list.length>1&&" +
      std::string(config->GetType("strict") == VTYPE_BOOL && !config->GetBool("strict") ? "false"
                                                                                        : "true") +
      ")" +
      (config->HasKey("diagnosticExpressionJSON")
           ? "return {strictError:" + QueryDiagnosticExpression(config, "list") + "};"
           : std::string("throw new Error('Strict locator matched multiple elements');")) +
      "if(!list.length)return {ready:false,reason:'element to be attached'};const "
      "selected=list[0],textNode=selected.nodeType===3&&['click','tap','hover'].includes(" +
      Literal(action) +
      ");let e=textNode?selected.parentElement:selected;"
      "if(!selected.isConnected||!e)throw new Error('Element is not attached to the "
      "DOM');if(e.nodeType!==1)throw new Error('Node is not an Element');"
      "if(['fill','clear'].includes(" +
      Literal(action) + "))e=(" + std::string(kSuiteCutEditableTarget) +
      ")(e);"
      "if(['click','tap'].includes(" +
      Literal(action) +
      ")&&e instanceof HTMLInputElement&&e.labels&&e.labels.length){const "
      "r=e.getBoundingClientRect();if(!r.width||!r.height)e=e.labels[0];}"
      "if(!" +
      std::string(config->GetBool("formCheck") ? "true" : "false") + "&&['click','tap'].includes(" +
      Literal(action) +
      "))e=e.closest('button,a[href],[role=button],[role=link]')||e.closest('label')||e;"
      "if(" +
      Literal(action) + "==='sendKeys')return (" + std::string(kSuiteCutSendKeysDom) + ")(e," +
      Literal(text) +
      ");"
      "if(" +
      Literal(action) + "==='type')return (" + std::string(kSuiteCutTypeDom) +
      ")(e);"
      "if(['focus','press'].includes(" +
      Literal(action) +
      ")){if(typeof e.focus==='function')e.focus();return {ready:true,focused:true};}"
      "const textRange=()=>{const range=document.createRange();range.selectNode(selected);return "
      "range;};"
      "const bounds=()=>{if(!textNode)return e.getBoundingClientRect();const "
      "range=textRange(),rect=range.getBoundingClientRect();range.detach();return rect;};"
      "const rectangles=()=>{if(!textNode)return [...e.getClientRects()];const "
      "range=textRange(),rects=[...range.getClientRects()];range.detach();return rects;};"
      "const visible=()=>{const r=bounds(),s=getComputedStyle(e);return "
      "r.width>0&&r.height>0&&s.visibility!=='hidden'&&s.visibility!=='collapse'&&s.display!=='"
      "none'};"
      "if(!visible()){if(" +
      std::string(config->GetBool("force") ? "true" : "false") +
      "){const "
      "rects=rectangles(),s=getComputedStyle(e);if(rects.length&&!rects.some(r=>r.width>0&&r."
      "height>0&&r.right>0&&r.bottom>0&&r.left<innerWidth&&r.top<innerHeight)&&s.visibility!=='"
      "hidden'&&s.visibility!=='collapse'&&s.display!=='none')throw new Error('Element is outside "
      "of the viewport');throw new Error('Element is not visible');}return "
      "{ready:false,reason:'element to be visible'};}"
      "if(!" +
      std::string(config->GetBool("force") ? "true" : "false") + "&&" + Literal(action) +
      "!=='hover'&&(e.matches(':disabled')||e.closest('[aria-disabled=\"true\"]')))return "
      "{ready:false,reason:'element to be enabled'};"
      "if(textNode){if(!rectangles().some(r=>r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<"
      "innerWidth&&r.top<innerHeight))return {ready:false,textScroll:true};}else "
      "e.scrollIntoView({block:" +
      std::string(config->HasKey("position") ? "'start'" : alignment) +
      ",inline:" + std::string(config->HasKey("position") ? "'start'" : "'nearest'") +
      ",behavior:'instant'});"
      "const position=" +
      (config->GetDictionary("position") ? Json(config->GetDictionary("position")) : "null") +
      ";"
      "if(position){const point=()=>{const "
      "r=e.getBoundingClientRect(),s=getComputedStyle(e);return "
      "{x:r.left+parseFloat(s.borderLeftWidth)+position.x,y:r.top+parseFloat(s.borderTopWidth)+"
      "position.y}};"
      "for(let "
      "p=e.parentElement;p;p=p.parentElement){if(p===document.body||p===document.documentElement)"
      "continue;const "
      "r=p.getBoundingClientRect(),s=getComputedStyle(p),q=point(),left=r.left+parseFloat(s."
      "borderLeftWidth),top=r.top+parseFloat(s.borderTopWidth);if(p.scrollWidth>p.clientWidth)p."
      "scrollLeft+=q.x<left?q.x-left-4:q.x>=left+p.clientWidth?q.x-left-p.clientWidth+4:0;if(p."
      "scrollHeight>p.clientHeight)p.scrollTop+=q.y<top?q.y-top-4:q.y>=top+p.clientHeight?q.y-top-"
      "p.clientHeight+4:0;}"
      "const "
      "q=point(),width=document.documentElement.clientWidth,height=document.documentElement."
      "clientHeight;window.scrollBy({left:q.x<0||q.x>=width?q.x-width/"
      "2:0,top:q.y<0||q.y>=height?q.y-height/2:0,behavior:'instant'});}"
      "if(" +
      std::string(pause_before_stable ? "true" : "false") +
      ")return {ready:false,beforeStable:true};"
      // Click stability belongs after focus/hover, on the node we will actually
      // press. Trial clicks still prove stability without admitting real input.
      "const a=bounds();if(" +
      std::string(pointer_work || action != "click" || config->GetBool("trial") ? "true"
                                                                                : "false") +
      "&&!" + std::string(config->GetBool("force") ? "true" : "false") + "&&" + Literal(action) +
      "!=='fill'&&" + Literal(action) +
      "!=='clear'){await new Promise(requestAnimationFrame);await new "
      "Promise(requestAnimationFrame);}"
      "if((textNode&&!selected.isConnected)||!e.isConnected){" +
      std::string(config->HasKey("nativeHandle")
                      ? "throw new Error('Element is not attached to the DOM');"
                      : "return {ready:false};") +
      "}if(!visible())return {ready:false};const b=bounds();"
      "if(!" +
      std::string(config->GetBool("force") ? "true" : "false") + "&&" + Literal(action) +
      "!=='fill'&&" + Literal(action) +
      "!=='clear'&&(a.x!==b.x||a.y!==b.y||a.width!==b.width||a.height!==b.height))return "
      "{ready:false,reason:'element position to be stable'};"
      "const style=getComputedStyle(e);"
      "let "
      "x=position?b.left+parseFloat(style.borderLeftWidth)+position.x:(Math.max(0,b.left)+Math.min("
      "innerWidth,b.right))/"
      "2,y=position?b.top+parseFloat(style.borderTopWidth)+position.y:(Math.max(0,b.top)+Math.min("
      "innerHeight,b.bottom))/2;"
      "if(!position){const "
      "rects=rectangles().filter(r=>r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<"
      "innerWidth&&r.top<innerHeight);if(rects.length){const "
      "r=rects[0];x=(Math.max(0,r.left)+Math.min(innerWidth,r.right))/"
      "2;y=(Math.max(0,r.top)+Math.min(innerHeight,r.bottom))/2;}}"
      "if(x<0||y<0||x>=innerWidth||y>=innerHeight){if(" +
      std::string(config->GetBool("force") ? "true" : "false") +
      ")throw new Error('Element is outside of the viewport');return {ready:false};}"
      "if(!" +
      std::string(config->GetBool("force") ? "true" : "false") + "&&" + Literal(action) +
      "!=='fill'&&" + Literal(action) +
      "!=='clear'){let "
      "target=document.elementFromPoint(x,y);while(target&&target.shadowRoot){const "
      "child=target.shadowRoot.elementsFromPoint(x,y)[0];if(!child||child===target)break;target="
      "child;}let receives=false;for(let "
      "n=target;n;n=n.assignedSlot||n.parentElement||n.getRootNode().host){if(n===e){receives=true;"
      "break;}}if(!receives)return {ready:false};}"
      "if(" +
      Literal(action) + "==='clear'){const result=(" + std::string(kSuiteCutClearDom) +
      ")(e);if(!result.ready||result.done)return result;}"
      "if(" +
      Literal(action) +
      "==='fill'){"
      "if(e instanceof HTMLInputElement||e instanceof HTMLTextAreaElement){"
      "if(e.readOnly)return {ready:false};const text=" +
      Literal(text) +
      ";"
      "if(e instanceof HTMLInputElement){const type=e.type;"
      "if(['color','date','time','datetime-local','month','range','week'].includes(type)){"
      "const "
      "probe=e.cloneNode();probe.value=text;if(type==='color'?probe.value.toLowerCase()!==text."
      "toLowerCase():type==='range'?(!Number.isFinite(Number(text))||text.trim()===''||Number("
      "probe.value)!==Number(text)):(text!==''&&probe.value===''))throw new Error('Malformed "
      "value');"
      "e.focus();e.value=probe.value;e.dispatchEvent(new "
      "Event('input',{bubbles:true,composed:true}));e.dispatchEvent(new "
      "Event('change',{bubbles:true}));return {ready:true,done:true};}"
      "if(type==='number'&&text!==''&&!Number.isFinite(Number(text)))throw new Error('Cannot type "
      "text into input[type=number]');"
      "if(!['text','search','url','tel','email','password','number'].includes(type))throw new "
      "Error('Input of type '+JSON.stringify(type)+' cannot be filled');}e.focus();e.select();"
      "}else if(e instanceof HTMLElement&&e.isContentEditable){e.focus();const "
      "r=document.createRange();r.selectNodeContents(e);const "
      "selection=getSelection();selection.removeAllRanges();selection.addRange(r);"
      "}else throw new Error('Element is not an <input>, <textarea> or [contenteditable] "
      "element');}"
      "if(['fill','clear'].includes(" +
      Literal(action) + ")&&!(" + std::string(kSuiteCutEditableFocus) + ")(e)){if(" +
      std::string(config->GetBool("force") ? "true" : "false") +
      ")throw new Error('Element cannot receive native text editing focus');return "
      "{ready:false,reason:'element to receive native text editing focus'};}return "
      "{ready:true,x,y,viewportWidth:innerWidth,viewportHeight:innerHeight};})()";
  DevToolsCallback callback = [this, id, generation, selector, action, text, config, pointer_work](
                                  bool ok, const std::string& json) {
    if (!operations_.IsCurrent(id, generation)) return;
    if ((action == "fill" || action == "clear") && operations_.IsExpired(id, generation, Now())) {
      Complete(id, false, action + ": Native operation timed out");
      return;
    }
    auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
    auto result = parsed ? parsed->GetDictionary() : nullptr;
    if (!ok || !result || result->HasKey("exceptionDetails")) {
      auto details = result ? result->GetDictionary("exceptionDetails") : nullptr;
      auto exception = details ? details->GetDictionary("exception") : nullptr;
      std::string error = exception ? exception->GetString("description").ToString()
                                    : "Native locator evaluation failed";
      error = error.substr(0, error.find('\n'));
      Complete(id, false, action + ": " + error + "\nCall log:\n - resolving " + selector);
      return;
    }
    auto remote = result->GetDictionary("result");
    auto state = remote ? remote->GetDictionary("value") : nullptr;
    if (!state) {
      Complete(id, false, "Invalid locator evaluation result");
      return;
    }
    if (state->GetType("strictError") == VTYPE_STRING) {
      if (operations_.IsExpired(id, generation, Now()))
        Complete(id, false, action + ": Native operation timed out");
      else
        Complete(id, false, action + ": " + state->GetString("strictError").ToString());
      return;
    }
    if (state->GetBool("textScroll")) {
      const auto handle = config->HasKey("nativeHandle")
                              ? handles_.find(config->GetInt("nativeHandle"))
                              : handles_.end();
      if (handle == handles_.end()) {
        Complete(id, false, "Native text-node pointer owner is unavailable");
        return;
      }
      auto params = CefDictionaryValue::Create();
      params->SetString("objectId", handle->second.node_id);
      DevTools(id, "DOM.scrollIntoViewIfNeeded", params,
               [this, id, generation, selector, action, text, config, pointer_work](
                   bool success, const std::string& json) {
                 auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                 const auto result = parsed ? parsed->GetDictionary() : nullptr;
                 if (!success || !result || result->HasKey("error")) {
                   Complete(id, false,
                            "Native text-node pointer scrolling failed: " + json.substr(0, 1024));
                   return;
                 }
                 auto release = CefDictionaryValue::Create();
                 release->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
                 DevTools(id, "Runtime.releaseObjectGroup", release,
                          [this, id, generation, selector, action, text, config, pointer_work](
                              bool released, const std::string&) {
                            if (!released) {
                              Complete(id, false,
                                       "Native text-node pointer temporary group release failed");
                              return;
                            }
                            LocatorAction(id, generation, selector, action, text, config,
                                          pointer_work);
                          });
               });
      return;
    }
    if (state->GetBool("beforeStable")) {
      PauseActionBeforeStable(id, generation, selector, action, text, config);
      return;
    }
    if (state->GetBool("done")) {
      Complete(id, true);
      return;
    }
    if (!state->GetBool("ready")) {
      locator_waits_[id] = state->GetType("reason") == VTYPE_STRING
                               ? state->GetString("reason").ToString().substr(0, 1024)
                               : "element actionability";
      if (pointer_work) {
        // No button has been pressed. Yield the mouse lane so an overlay's
        // locator handler (or another queued gesture) can run before retrying
        // this same operation with its original deadline and generation.
        action_navigations_.erase(id);
        CleanupInput(id);
        context_retries_[id] = [this, id, generation, selector, action, text, config] {
          LocatorAction(id, generation, selector, action, text, config);
        };
        CefPostDelayedTask(TID_UI,
                           new Task([self = CefRefPtr<Source>(this), id, generation, selector,
                                     action, text, config] {
                             self->LocatorAction(id, generation, selector, action, text, config);
                           }),
                           20);
        return;
      }
      if (!pointer_work && selector_waits_ && selector_waits_->Contains(id)) {
        RetryContext(id);
        return;
      }
      CefPostDelayedTask(TID_UI,
                         new Task([self = CefRefPtr<Source>(this), id, generation, selector, action,
                                   text, config, pointer_work] {
                           self->LocatorAction(id, generation, selector, action, text, config,
                                               pointer_work);
                         }),
                         20);
      return;
    }
    if (action == "focus") {
      Complete(id, true);
      return;
    }
    if (action == "sendKeys") {
      SendKeysPrepared(id, state);
      return;
    }
    if (action == "press" || action == "type") {
      TargetedKeyboardPrepared(id, generation);
      return;
    }
    if (action == "fill" || action == "clear") {
      if (!operations_.IsCurrent(id, generation)) return;
      if (operations_.IsExpired(id, generation, Now())) {
        Complete(id, false, action + ": Native operation timed out");
        return;
      }
      CefKeyEvent key;
      key.type = KEYEVENT_RAWKEYDOWN;
      key.windows_key_code = 8;
      key.native_key_code = 0xff08;
      if (text.empty()) browser_->GetHost()->SendKeyEvent(key);
      key.type = KEYEVENT_KEYUP;
      if (text.empty()) browser_->GetHost()->SendKeyEvent(key);
      operation_sessions_[id] = "";
      auto params = CefDictionaryValue::Create();
      params->SetString("text", text);
      DevTools(id, "Input.insertText", params,
               [this, id, generation](bool success, const std::string&) {
                 if (!operations_.IsCurrent(id, generation)) return;
                 if (operations_.IsExpired(id, generation, Now())) {
                   Complete(id, false, "Native text input operation timed out");
                   return;
                 }
                 Complete(id, success, success ? "" : "Native text input failed");
               });
    } else {
      if (config->GetBool("trial")) {
        Complete(id, true);
        return;
      }
      auto input = CefDictionaryValue::Create();
      input->SetString("kind", action == "tap" ? "touch" : "mouse");
      input->SetString("action", action == "tap" ? "tap" : action == "click" ? "click" : "move");
      const auto target_frame = FrameId(config);
      const double local_x = Number(state, "x"), local_y = Number(state, "y");
      input->SetString("button", config->GetType("button") == VTYPE_STRING
                                     ? config->GetString("button")
                                     : CefString("left"));
      input->SetInt("clickCount",
                    config->GetType("clickCount") == VTYPE_INT ? config->GetInt("clickCount") : 1);
      input->SetInt("delay", config->GetInt("delay"));
      input->SetInt("steps", config->GetType("steps") == VTYPE_INT ? config->GetInt("steps") : 1);
      input->SetBool("waitForNavigation",
                     (action == "click" || action == "tap") && !config->GetBool("noWaitAfter"));
      MapFramePoint(
          id, target_frame, local_x, local_y, Number(state, "viewportWidth"),
          Number(state, "viewportHeight"),
          [this, id, generation, input, target_frame, config, action, pointer_work](double x,
                                                                                    double y) {
            if (pointer_work) {
              const auto current = input_work_.find(id);
              if (!operations_.IsCurrent(id, generation) || mouse_input_ != id ||
                  current == input_work_.end() || current->second != pointer_work)
                return;
              if (operations_.IsExpired(id, generation, Now())) {
                Complete(id, false, "Native pointer original deadline expired before press");
                return;
              }
              const bool moved = std::abs(x - mouse_x_) > .01 || std::abs(y - mouse_y_) > .01;
              pointer_work->message->SetDouble("x", x);
              pointer_work->message->SetDouble("y", y);
              pointer_work->pointer_verified = !moved;
              if (moved) {
                pointer_work->phase = 0;
                pointer_work->step = 1;
                pointer_work->start_x = mouse_x_;
                pointer_work->start_y = mouse_y_;
              }
              pointer_work->busy = false;
              ContinueInput(pointer_work);
              return;
            }
            // Two renderer/context retries can have evaluated concurrently before
            // either admitted input. Only the first may acquire this gesture.
            if (input_work_.contains(id)) return;
            input->SetDouble("x", x);
            input->SetDouble("y", y);
            operation_frames_[id] = target_frame;
            context_retries_.erase(id);
            BeginInput(
                id, generation, input, nullptr,
                action == "click" && !config->GetBool("force") ? config->Copy(false) : nullptr);
          });
    }
  };
  if (config->HasKey("formNode"))
    FormNodeEvaluate(id, config, expression, std::move(callback));
  else if (config->HasKey("nativeHandle"))
    EvaluateFixedNode(id, config, expression, std::move(callback));
  else if (QueryNeedsMainWorld(config) || QueryHasSnapshotReferences(config))
    QueryInspect(id, config, "return await(" + expression + ");", true, true, std::move(callback));
  else
    Evaluate(id, expression, std::move(callback), FrameId(config), !QueryNeedsMainWorld(config));
}

auto Source::ValidateVisibilitySelectors(int id, uint64_t generation, Dict message) -> void {
  // Compile the complete grammar in a real pristine utility context before
  // observing an absent frame. Registered engine code is never invoked here.
  auto queries = CefListValue::Create();
  auto query = message->GetList("query");
  if (!query) {
    query = CefListValue::Create();
    if (!message->HasKey("nativeHandle")) {
      auto step = CefDictionaryValue::Create();
      step->SetString("engine", "css");
      suitecut::InboundCopyString(message, step, "selector");
      query->SetDictionary(0, step);
    }
  }
  queries->SetList(0, query->Copy());
  if (const auto path = message->GetList("framePath")) {
    if (path->GetSize() > 32) {
      Complete(id, false, "Invalid native frame locator path");
      return;
    }
    for (size_t index = 0; index < path->GetSize(); ++index) {
      if (path->GetType(index) != VTYPE_LIST) {
        Complete(id, false, "Invalid native frame locator path");
        return;
      }
      queries->SetList(index + 1, path->GetList(index)->Copy());
    }
  }
  auto list = CefValue::Create();
  list->SetList(queries);
  WithFrame(
      id, main_frame_id_,
      [this, id, generation, message, list](int context) {
        if (!operations_.IsCurrent(id, generation)) return;
        const auto frame = frame_states_.find(main_frame_id_);
        if (frame == frame_states_.end() || frame->second.utility_context != context) {
          Complete(id, false, "Native visibility selector validation context unavailable");
          return;
        }
        const auto frame_id = frame->first, session = frame->second.session,
                   loader = frame->second.loader, identity = frame->second.utility_identity;
        auto params = CefDictionaryValue::Create();
        params->SetInt("contextId", context);
        params->SetBool("returnByValue", true);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        params->SetString("expression", "(()=>{const validate=(" + std::string(kSuiteCutDomQuery) +
                                            "),options=" + QueryOptionsExpression() +
                                            ";options.validateOnly=true;for(const query of " +
                                            CefWriteJSON(list, JSON_WRITER_DEFAULT).ToString() +
                                            ")validate(query,document,options);return true;})()");
        DevTools(
            id, "Runtime.evaluate", params,
            [this, id, generation, message, frame_id, session, loader, identity, context](
                bool ok, const std::string& json) {
              if (!operations_.IsCurrent(id, generation)) return;
              if (operations_.IsExpired(id, generation, Now())) {
                Complete(id, false, "Native operation timed out");
                return;
              }
              auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
              auto result = parsed ? parsed->GetDictionary() : nullptr;
              auto remote = result ? result->GetDictionary("result") : nullptr;
              if (!ok || !result || result->HasKey("exceptionDetails") || !remote ||
                  remote->GetType("value") != VTYPE_BOOL || !remote->GetBool("value")) {
                auto details = result ? result->GetDictionary("exceptionDetails") : nullptr;
                auto exception = details ? details->GetDictionary("exception") : nullptr;
                const auto error =
                    exception ? suitecut::DevToolsString(exception->GetValue("description"))
                    : result  ? result->GetString("message").ToString()
                              : "";
                Complete(id, false,
                         error.empty() ? "Native visibility selector validation failed" : error);
                return;
              }
              const auto frame = frame_states_.find(frame_id);
              if (frame == frame_states_.end() || frame->second.session != session ||
                  frame->second.loader != loader || frame->second.utility_context != context ||
                  frame->second.utility_identity != identity) {
                Complete(id, false, "Native visibility selector validation document changed");
                return;
              }
              if (!visibility_snapshots_.MarkValidated(id, generation)) {
                Complete(id, false, "Native visibility snapshot generation unavailable");
                return;
              }
              QueryEvaluate(id, generation, message);
            },
            session, false, kMaxResponseBytes, true);
      },
      true);
}

auto Source::QueryEvaluate(int id, uint64_t generation, Dict message) -> void {
  if (!operations_.IsCurrent(id, generation)) return;
  if (AssertionDeadlineExpire(id, generation)) return;
  if (CompleteExpiredExpectation(id, generation)) return;
  if (message->GetBool("domGetter") || message->GetBool("strictDiagnostics")) {
    if (operations_.IsExpired(id, generation, Now())) {
      Complete(id, false, "Native operation timed out");
      return;
    }
    if (!SelectorWaits().Admit(id, generation)) {
      Complete(id, false, "Native DOM observation physical capacity exceeded");
      return;
    }
  }
  if (visibility_snapshots_.Contains(id) && !visibility_snapshots_.Validated(id, generation)) {
    ValidateVisibilitySelectors(id, generation, message);
    return;
  }
  auto path = message->GetList("framePath");
  if (path && path->GetSize()) {
    ResolveFramePath(
        id, generation, message, 0, FrameId(message),
        [this, id, generation](Dict resolved) { QueryEvaluate(id, generation, resolved); });
    return;
  }
  if (message->GetBool("stableState") && !PrepareStableElementState(id, generation, message))
    return;
  if (message->GetBool("accessibilitySnapshot") &&
      !message->HasKey("nativeAccessibilitySnapshot") &&
      !message->HasKey("nativeSemanticSnapshotJson")) {
    if (!lazy_queries_.contains(id)) lazy_queries_[id] = message;
    if (message->GetBool("accessibilitySemantics")) {
      CaptureDOMSemantic(
          id, message,
          [this, id, generation, message](const std::string& array, const std::string& json) {
            auto config = message->Copy(false);
            config->SetString("nativeQueryNodes", array);
            config->SetString("nativeSemanticSnapshotJson", json);
            QueryEvaluate(id, generation, config);
          });
      return;
    }
    CaptureAXForest(
        id, message,
        [this, id, generation, message](const std::string& array, CefRefPtr<CefListValue> forest) {
          auto config = message->Copy(false);
          config->SetString("nativeQueryNodes", array);
          config->SetList("nativeAccessibilitySnapshot", forest->Copy());
          QueryEvaluate(id, generation, config);
        });
    return;
  }
  // Custom engines retain their declared author realm for selection. Native
  // DOM metadata observes those actual backend nodes in the isolated realm,
  // so page replacements of browser intrinsics cannot forge native states.
  const bool native_metadata = message->GetBool("visibility") || message->GetBool("elementState") ||
                               message->GetBool("accessibility");
  if (((!message->GetBool("dom") && !message->HasKey("nativeHandle")) ||
       QueryUsesMultipleWorlds(message) ||
       ((native_metadata || message->GetBool("domGetter")) && message->GetBool("dom") &&
        QueryNeedsMainWorld(message)) ||
       message->HasKey("nativeRootHandle")) &&
      !message->HasKey("nativeQueryNodes")) {
    if (!lazy_queries_.contains(id)) lazy_queries_[id] = message;
    QueryNodesInWorld(id, message, message->GetBool("dom"),
                      [this, id, generation, message](const std::string& array) {
                        auto config = message->Copy(false);
                        config->SetString("nativeQueryNodes", array);
                        QueryEvaluate(id, generation, config);
                      });
    return;
  }
  const bool all = message->GetBool("all");
  const bool native_getter = message->GetBool("domGetter");
  const bool immediate = native_getter && message->GetBool("immediate");
  const bool strict =
      !(native_getter && message->GetType("strict") == VTYPE_BOOL && !message->GetBool("strict"));
  const bool diagnostic_strict =
      message->GetBool("dom") && (native_getter || message->GetBool("strictDiagnostics")) &&
      strict &&
      (!all || (message->GetBool("expectation") && message->GetBool("strictDiagnostics")));
  const auto accessibility =
      message->GetBool("stableState")
          ? "," + ElementStates().Expression(id, "e=>(" + std::string(kSuiteCutDomQuery) +
                                                     ")([],e,{elementStateOnly:true})")
      : message->GetBool("visibility") || message->GetBool("elementState")
          ? ",(nodes[0]?(" + std::string(kSuiteCutDomQuery) + ")([],nodes[0],{" +
                std::string(message->GetBool("elementState") ? "elementStateOnly:true"
                                                             : "visibilityOnly:true") +
                "}):undefined)"
      : message->GetBool("expectation") && message->GetBool("accessibility")
          ? ",(nodes[0]?(" + std::string(kSuiteCutDomQuery) + ")([],nodes[0]," +
                QueryOptionsExpression(true) + "):undefined)"
          : std::string("");
  std::string accessibility_snapshot;
  if (message->HasKey("nativeAccessibilitySnapshot")) {
    auto value = CefValue::Create();
    value->SetList(message->GetList("nativeAccessibilitySnapshot")->Copy());
    std::string native_json;
    if (!suitecut::DevToolsLosslessJSON(value, native_json)) {
      Complete(id, false, "Native accessibility snapshot exceeds 64 KiB");
      return;
    }
    accessibility_snapshot = (accessibility.empty() ? ",undefined," : ",") + native_json;
  }
  if (message->HasKey("nativeSemanticSnapshotJson"))
    accessibility_snapshot =
        std::string(accessibility.empty() ? ",undefined," : ",") + "JSON.parse(" +
        DOMSemanticQuote(message->GetString("nativeSemanticSnapshotJson").ToString()) + ")";
  const auto body =
      "const nodes=" +
      (message->HasKey("nativeHandle")       ? std::string("[this]")
       : message->HasKey("nativeQueryNodes") ? std::string("this")
                                             : QueryExpression(message)) +
      ";" +
      (all ? std::string("")
           : std::string(immediate ? "" : "if(!nodes.length)return {waiting:true};")) +
      (diagnostic_strict
           ? "if(nodes.length>1)return {strictError:" +
                 QueryDiagnosticExpression(message, "nodes") + "};"
           : (!all && strict
                  ? std::string("if(nodes.length>1)throw new Error('strict mode violation: locator "
                                "resolved to '+nodes.length+' elements');")
                  : std::string(""))) +
      "return {waiting:false,value:await (" + message->GetString("function").ToString() + ")(" +
      (all ? "nodes" : "nodes[0]") + accessibility + accessibility_snapshot + ",...handles)};";
  DevToolsCallback callback = [this, id, generation, message, diagnostic_strict](
                                  bool ok, const std::string& json) {
    if (assertion_deadlines_.Get(id, generation)) {
      if (!AssertionDeadlineObserve(id, generation, ok, json) ||
          AssertionDeadlineExpire(id, generation))
        return;
    }
    if (message->GetBool("domGetter") || diagnostic_strict) {
      if (!operations_.IsCurrent(id, generation)) return;
      if (operations_.IsExpired(id, generation, Now())) {
        if (CompleteExpiredExpectation(id, generation)) return;
        Complete(id, false, "Native operation timed out");
        return;
      }
    }
    if ((message->HasKey("nativeAccessibilitySnapshot") ||
         message->HasKey("nativeSemanticSnapshotJson")) &&
        message->GetType("nativeQueryNodes") == VTYPE_STRING) {
      auto release = CefDictionaryValue::Create();
      release->SetString("objectId", message->GetString("nativeQueryNodes"));
      SendDevTools("Runtime.releaseObject", release, operation_sessions_[id]);
    }
    auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
    auto result = parsed ? parsed->GetDictionary() : nullptr;
    auto remote = result ? result->GetDictionary("result") : nullptr;
    auto value = remote ? remote->GetDictionary("value") : nullptr;
    first_observations_.erase(id);
    if (!result || result->HasKey("exceptionDetails") || !value) {
      Complete(id, true, "", json);
      return;
    }
    if (diagnostic_strict && value->GetType("strictError") == VTYPE_STRING) {
      if (operations_.IsExpired(id, generation, Now()))
        Complete(id, false, "Native operation timed out");
      else
        Complete(id, false, value->GetString("strictError").ToString());
      return;
    }
    if (value->GetBool("waiting")) {
      if (selector_waits_ && selector_waits_->Contains(id)) {
        RetryContext(id);
        return;
      }
      const auto retry = lazy_queries_.contains(id) ? lazy_queries_.at(id) : message;
      CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation, retry] {
                           self->QueryEvaluate(id, generation, retry);
                         }),
                         20);
      return;
    }
    auto actual = value->GetValue("value");
    if (actual)
      remote->SetValue("value", actual->Copy());
    else
      remote->SetNull("value");
    if (visibility_snapshots_.Contains(id) && !SuiteCutVisibilitySnapshotReply(result, actual)) {
      Complete(id, false, "Invalid native visibility boolean observation");
      return;
    }
    if (message->GetBool("expectation")) {
      auto observation = remote->GetDictionary("value");
      if (!observation || observation->GetType("matches") != VTYPE_BOOL) {
        Complete(id, false, "Invalid native assertion observation");
        return;
      }
      if (observation->GetBool("matches") == message->GetBool("isNot")) {
        auto snapshot = Json(result);
        if (snapshot.size() > 65536) {
          auto abbreviated = CefListValue::Create();
          abbreviated->SetString(0, "primitive");
          abbreviated->SetString(1, "<actual snapshot exceeds 64 KiB>");
          observation->SetList("actual", abbreviated);
          observation->SetBool("truncated", true);
          snapshot = Json(result);
        }
        if (AssertionDeadlineExpire(id, generation)) return;
        expect_results_[id] = std::move(snapshot);
        if (selector_waits_ && selector_waits_->Contains(id)) {
          RetryContext(id);
          return;
        }
        const auto retry = lazy_queries_.contains(id) ? lazy_queries_.at(id) : message;
        CefPostDelayedTask(TID_UI,
                           new Task([self = CefRefPtr<Source>(this), id, generation, retry] {
                             self->QueryEvaluate(id, generation, retry);
                           }),
                           20);
        return;
      }
    }
    if (AssertionDeadlineExpire(id, generation)) return;
    if (assertion_deadlines_.Get(id, generation)) assertion_deadlines_.Admitted(id, generation);
    const auto output = Json(result);
    if (message->GetBool("domGetter") && operations_.IsExpired(id, generation, Now())) {
      Complete(id, false, "Native operation timed out");
      return;
    }
    Complete(id, true, "", output);
  };
  const auto handles = message->GetList("handles");
  if (message->HasKey("nativeHandle"))
    EvaluateFixedNode(id, message, "(async()=>{const handles=[];" + body + "})()",
                      std::move(callback));
  else if (message->HasKey("nativeQueryNodes")) {
    WithFrame(
        id, FrameId(message),
        [this, id, message, body, callback = std::move(callback)](int) mutable {
          evaluation_groups_[id].insert(operation_sessions_[id]);
          auto params = CefDictionaryValue::Create();
          params->SetString("objectId", message->GetString("nativeQueryNodes"));
          params->SetString("functionDeclaration", "async function(...handles){" + body + "}");
          params->SetBool("awaitPromise", true);
          params->SetBool("returnByValue", true);
          params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
          auto config = message->Copy(false);
          if (!config->HasKey("handles")) config->SetList("handles", CefListValue::Create());
          if (message->GetBool("dom"))
            DevTools(id, "Runtime.callFunctionOn", params, std::move(callback));
          else
            HandleArguments(id, config, params, false,
                            [this, id, params, callback = std::move(callback)]() mutable {
                              author_query_evaluations_.insert(id);
                              DevTools(id, "Runtime.callFunctionOn", params, std::move(callback));
                            });
        },
        message->GetBool("dom"));
  } else if (handles && handles->GetSize()) {
    WithFrame(
        id, FrameId(message),
        [this, id, message, body, callback = std::move(callback)](int) mutable {
          evaluation_groups_[id].insert(operation_sessions_[id]);
          auto params = CefDictionaryValue::Create();
          params->SetString("functionDeclaration", "async function(...handles){" + body + "}");
          params->SetBool("awaitPromise", true);
          params->SetBool("returnByValue", true);
          params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
          HandleArguments(id, message, params, true,
                          [this, id, params, callback = std::move(callback)]() mutable {
                            DevTools(id, "Runtime.callFunctionOn", params, std::move(callback));
                          });
        });
  } else
    Evaluate(id, "(async()=>{const handles=[];" + body + "})()", std::move(callback),
             FrameId(message), message->GetBool("dom") && !QueryNeedsMainWorld(message),
             message->GetString("nativeAssertionKind").ToString());
}

auto Source::RetryContext(int id) -> void {
  if (const auto owner = operations_.Get(id); owner && LocatorHandlerPaused(id, owner->generation))
    return;
  if (const auto screenshot = screenshot_assertion_work_.find(id);
      screenshot != screenshot_assertion_work_.end()) {
    ScreenshotAssertionFreeze(screenshot->second->identity, false, nullptr,
                              "Native screenshot context changed during capture preparation");
    return;
  }
  if (const auto entry = operations_.Get(id);
      entry && AssertionDeadlineExpire(id, entry->generation))
    return;
  if (reference_frame_batches_.contains(id)) {
    if (ReferenceFrameOperationCurrent(id))
      Complete(id, false, "Native aggregate snapshot frame context unavailable");
    return;
  }
  ax_captures_.erase(id);
  if (count_queries_.contains(id)) {
    CompleteCountZero(id);
    return;
  }
  if (visibility_snapshots_.Contains(id)) {
    if (VisibilityScopeGone(id))
      CompleteVisibilityEmpty(id);
    else
      Complete(id, false, "Native visibility snapshot context unavailable before observation");
    return;
  }
  const auto entry = operations_.Get(id);
  const auto retry = context_retries_.find(id);
  // The SDK owns the callback until it resumes. Its native reacquisition uses
  // the current main frame and must not emit a second pause after navigation.
  if (artifacts_ && artifacts_->HookPending(id)) return;
  if (!entry || retry == context_retries_.end()) return;
  if (selector_waits_ && selector_waits_->Contains(id)) {
    // No attempt can reenter while its old document still has physical
    // commands in flight. Their callbacks settle ownership but cannot run
    // query continuations after Pause, and the whole old group is released
    // below only after all genuine ACKs have arrived.
    selector_waits_->Pause(id);
    if (!selector_waits_->Ready(id)) {
      if (!context_retry_queued_.insert(id).second) return;
      CefPostDelayedTask(
          TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation = entry->generation] {
            if (self->operations_.IsCurrent(id, generation)) {
              self->context_retry_queued_.erase(id);
              self->RetryContext(id);
            }
          }),
          10);
      return;
    }
  }
  if (!context_retry_queued_.insert(id).second) return;
  if (artifact_configs_.contains(id)) {
    artifact_configs_.at(id)->SetBool("__artifactNavigationRetry", true);
    if (artifacts_) artifacts_->Cancel(id);
  }
  for (auto it = devtools_.begin(); it != devtools_.end();) {
    if (it->second.owner == id && !it->second.retain_after_cancel)
      it = devtools_.erase(it);
    else
      ++it;
  }
  const auto pending = pending_handle_groups_.find(id);
  if (pending != pending_handle_groups_.end()) {
    auto params = CefDictionaryValue::Create();
    params->SetString("objectGroup", pending->second);
    SendDevTools("Runtime.releaseObjectGroup", params, operation_sessions_[id]);
    pending_handle_groups_.erase(pending);
  }
  const auto groups = evaluation_groups_.find(id);
  if (groups != evaluation_groups_.end()) {
    const auto sessions = groups->second;
    evaluation_groups_.erase(groups);
    for (const auto& session : sessions) {
      auto params = CefDictionaryValue::Create();
      params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
      SendDevTools("Runtime.releaseObjectGroup", params, session);
    }
  }
  const auto callback = retry->second;
  CefPostDelayedTask(
      TID_UI,
      new Task([self = CefRefPtr<Source>(this), id, generation = entry->generation, callback] {
        if (self->operations_.IsCurrent(id, generation)) {
          self->context_retry_queued_.erase(id);
          if (self->selector_waits_) self->selector_waits_->Resume(id);
          callback();
        }
      }),
      10);
}

}  // namespace suitecut::browser
