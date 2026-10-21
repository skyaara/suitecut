// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::ArgumentStep(std::shared_ptr<ArgumentWork> work) -> void {
  if (!operations_.IsCurrent(work->owner, work->generation)) return;
  if (work->index == work->handles.size()) {
    work->params->SetList("arguments", work->arguments);
    auto done = std::move(work->done);
    done();
    return;
  }
  const auto handle = work->handles[work->index];
  const auto destination = frame_states_.find(operation_frames_[work->owner]);
  if (destination == frame_states_.end() || destination->second.context != work->context) {
    Complete(work->owner, false, "Native execution context destroyed during argument adoption");
    return;
  }
  if (handle.frame_id == destination->first) {
    auto argument = CefDictionaryValue::Create();
    argument->SetString("objectId", handle.object_id);
    work->arguments->SetDictionary(work->index++, argument);
    ArgumentStep(work);
    return;
  }
  if (handle.node_id.empty()) {
    Complete(work->owner, false,
             "JSHandles can be evaluated only in the context they were created! Native handle "
             "belongs to a different frame context");
    return;
  }
  const auto source = frame_states_.find(handle.frame_id);
  if (source == frame_states_.end() || source->second.context != handle.context ||
      handle.session != destination->second.session || source->second.origin.empty() ||
      source->second.origin != destination->second.origin) {
    Complete(work->owner, false, "Unable to adopt element handle from a different document");
    return;
  }
  // Resolve the actual DOM backend node in the destination world. Each
  // temporary wrapper belongs to this operation's group, including failures
  // and cancellation while a later argument is being adopted.
  evaluation_groups_[work->owner].insert(operation_sessions_[work->owner]);
  auto describe = CefDictionaryValue::Create();
  describe->SetString("objectId", handle.node_id);
  DevTools(work->owner, "DOM.describeNode", describe, [this, work](bool, const std::string& json) {
    auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
    auto result = parsed ? parsed->GetDictionary() : nullptr;
    auto node = result ? result->GetDictionary("node") : nullptr;
    if (!node || node->GetType("backendNodeId") != VTYPE_INT) {
      Complete(work->owner, false, "Unable to adopt element handle from a different document");
      return;
    }
    auto resolve = CefDictionaryValue::Create();
    resolve->SetInt("backendNodeId", node->GetInt("backendNodeId"));
    resolve->SetInt("executionContextId", work->context);
    resolve->SetString("objectGroup", "suitecut-automation-" + std::to_string(work->owner));
    DevTools(work->owner, "DOM.resolveNode", resolve, [this, work](bool, const std::string& json) {
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
      auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto node = result ? result->GetDictionary("object") : nullptr;
      if (!node || node->GetType("objectId") != VTYPE_STRING) {
        Complete(work->owner, false, "Unable to adopt element handle from a different document");
        return;
      }
      auto wrap = CefDictionaryValue::Create();
      wrap->SetString("objectId", node->GetString("objectId"));
      wrap->SetString("functionDeclaration", "function(){return {value:this};}");
      wrap->SetString("objectGroup", "suitecut-automation-" + std::to_string(work->owner));
      DevTools(work->owner, "Runtime.callFunctionOn", wrap,
               [this, work](bool, const std::string& json) {
                 auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                 auto result = parsed ? parsed->GetDictionary() : nullptr;
                 auto wrapper = result ? result->GetDictionary("result") : nullptr;
                 if (!wrapper || result->HasKey("exceptionDetails") ||
                     wrapper->GetType("objectId") != VTYPE_STRING) {
                   Complete(work->owner, false,
                            "Unable to adopt element handle from a different document");
                   return;
                 }
                 auto argument = CefDictionaryValue::Create();
                 argument->SetString("objectId", wrapper->GetString("objectId"));
                 work->arguments->SetDictionary(work->index++, argument);
                 ArgumentStep(work);
               });
    });
  });
}

auto Source::HandleGuid(int id) -> std::string {
  return "suitecut-native-" + handle_namespace_ + ":" + std::to_string(id);
}

auto Source::MissingHandle(int id) -> std::string {
  return "no object with guid " + HandleGuid(id) +
         " (native handle is disposed or its execution context was destroyed by navigation)";
}

auto Source::HandleArguments(int id, Dict message, Dict params, bool destination_receiver,
                             std::function<void()> done) -> void {
  if (MaterializeConsoleArguments(
          id, message, [this, id, message, params, destination_receiver, done] {
            HandleArguments(id, message, params, destination_receiver, done);
          }))
    return;
  auto ids = message->GetList("handles");
  if (!ids || ids->GetSize() > 1024) {
    Complete(id, false, "Invalid native handle arguments");
    return;
  }
  std::vector<ObjectHandle> handles;
  for (size_t index = 0; index < ids->GetSize(); ++index) {
    if (ids->GetType(index) != VTYPE_INT) {
      Complete(id, false, "Invalid native handle argument");
      return;
    }
    const auto found = handles_.find(ids->GetInt(index));
    if (found == handles_.end()) {
      Complete(id, false, MissingHandle(ids->GetInt(index)));
      return;
    }
    handles.push_back(found->second);
  }
  WithFrame(id, FrameId(message),
            [this, id, params, destination_receiver, handles = std::move(handles),
             done = std::move(done)](int context) mutable {
              const auto entry = operations_.Get(id);
              if (!entry) return;
              if (destination_receiver) params->SetInt("executionContextId", context);
              auto work = std::make_shared<ArgumentWork>();
              work->owner = id;
              work->context = context;
              work->generation = entry->generation;
              work->params = params;
              work->arguments = CefListValue::Create();
              work->handles = std::move(handles);
              work->done = std::move(done);
              ArgumentStep(work);
            });
}

auto Source::Handle(int id, Dict message, bool frame_ready) -> void {
  const auto action = message->GetString("action").ToString();
  if (action == "frameElement") {
    FrameElementHandle(id, message);
    return;
  }
  if (action != "create" && action != "dispose" && action != "disposeMany" &&
      console_arguments_.contains(message->GetInt("handle"))) {
    MaterializeConsoleArgument(id, message->GetInt("handle"), [this, id, message, frame_ready] {
      Handle(id, message, frame_ready);
    });
    return;
  }
  if (action == "ownerFrame") {
    ElementOwnerFrame(id, message);
    return;
  }
  if (action == "create" && message->GetType("query") == VTYPE_LIST &&
      !message->HasKey("expression")) {
    const auto path = message->GetList("framePath");
    if (path && path->GetSize()) {
      const auto entry = operations_.Get(id);
      if (!entry) return;
      ResolveFramePath(id, entry->generation, message, 0, FrameId(message),
                       [this, id](Dict resolved) { Handle(id, resolved); });
      return;
    }
    if (!message->HasKey("nativeQueryNodes")) {
      if (message->GetType("queryFunction") == VTYPE_STRING)
        context_retries_[id] = [this, id, message] { Handle(id, message); };
      QueryNodesInMainWorld(id, message, [this, id, message](const std::string& array) {
        auto config = message->Copy(false);
        config->SetString("nativeQueryNodes", array);
        Handle(id, config);
      });
      return;
    }
    auto config = message->Copy(false);
    if (message->GetType("queryFunction") == VTYPE_STRING) {
      config->Remove("query");
      config->Remove("queryFunction");
      config->SetString(
          "function",
          "async function(...handles){const nodes=" +
              (message->HasKey("nativeQueryNodes") ? std::string("this")
                                                   : QueryExpression(message)) +
              ";if(nodes.length>1)throw new Error('strict mode violation: locator matched multiple "
              "elements');" +
              "if(!nodes.length)return {suitecutQueryMissing:true};return {value:await (" +
              message->GetString("queryFunction").ToString() + ")(nodes[0],...handles)};}");
      config->SetBool("queryHandle", true);
      // Keep the original query for deadline-preserving retries on absence.
      if (!message->HasKey("nativeQueryNodes"))
        context_retries_[id] = [this, id, message] { Handle(id, message); };
      Handle(id, config);
      return;
    }
    const auto expression =
        "(()=>{const nodes=" + QueryExpression(message) + ";" +
        (message->GetBool("all")
             ? std::string("return nodes;")
             : std::string(message->GetBool("strict")
                               ? "if(nodes.length>1)throw new Error('strict mode violation: "
                                 "locator matched multiple elements');"
                               : "") +
                   "return nodes[0]||null;") +
        "})()";
    if (message->HasKey("nativeQueryNodes")) {
      config->Remove("query");
      config->SetString(
          "function", "function(){const nodes=this;" +
                          (message->GetBool("all")
                               ? std::string("return {value:nodes};")
                               : std::string(message->GetBool("strict")
                                                 ? "if(nodes.length>1)throw new Error('strict mode "
                                                   "violation: locator matched multiple elements');"
                                                 : "") +
                                     "return {value:nodes[0]||null};") +
                          "}");
      if (!config->HasKey("handles")) config->SetList("handles", CefListValue::Create());
    } else
      config->SetString("expression", expression);
    Handle(id, config);
    return;
  }
  if ((action == "create" || action == "focus") && !frame_ready) {
    WithFrame(
        id, FrameId(message), [this, id, message](int) { Handle(id, message, true); },
        action == "focus");
    return;
  }
  if (action == "create" || action == "property") {
    if (handles_.size() + pending_handle_groups_.size() >= 1024 ||
        next_handle_ == std::numeric_limits<int>::max()) {
      Complete(id, false, "Native object handle limit exceeded");
      return;
    }
    if (action == "create" && message->GetType("expression") != VTYPE_STRING &&
        message->GetType("function") != VTYPE_STRING) {
      Complete(id, false, "Handle creation requires an expression or function");
      return;
    }
    const auto group = "suitecut-handle-" + std::to_string(++next_handle_);
    pending_handle_groups_.emplace(id, group);
    const auto frame_id = FrameId(message);
    const auto frame = frame_states_.find(frame_id);
    if (frame == frame_states_.end()) {
      Complete(id, false, "Native frame detached");
      return;
    }
    const int context = frame->second.context;
    const auto context_identity = frame->second.context_identity;
    const auto renderer_epoch = native_pristine_renderer_epoch_;
    if (message->HasKey("bindingArgumentContext") &&
        (message->GetInt("bindingArgumentContext") != context ||
         message->GetString("bindingArgumentSession") != frame->second.session)) {
      Complete(id, false, "Native binding argument context was destroyed");
      return;
    }
    operation_sessions_[id] = frame->second.session;
    operation_frames_[id] = frame_id;
    auto params = CefDictionaryValue::Create();
    std::string method = "Runtime.evaluate";
    if (action == "property") {
      const auto found = handles_.find(message->GetInt("handle"));
      if (found == handles_.end() || message->GetType("name") != VTYPE_STRING) {
        Complete(id, false, "Invalid native property handle");
        return;
      }
      if (found->second.frame_id != frame_id) {
        Complete(id, false, "Native handle belongs to a different frame context");
        return;
      }
      method = "Runtime.callFunctionOn";
      params->SetString("objectId", found->second.object_id);
      params->SetString("functionDeclaration", "function(name){return {value:this.value[name]};}");
      auto arguments = CefListValue::Create();
      auto name = CefDictionaryValue::Create();
      name->SetString("value", message->GetString("name"));
      arguments->SetDictionary(0, name);
      params->SetList("arguments", arguments);
    } else if (message->GetType("function") == VTYPE_STRING) {
      method = "Runtime.callFunctionOn";
      params->SetString("functionDeclaration", message->GetString("function"));
      if (message->GetType("nativeQueryNodes") == VTYPE_STRING)
        params->SetString("objectId", message->GetString("nativeQueryNodes"));
    } else {
      params->SetString("expression", "(async()=>({value:await(" +
                                          message->GetString("expression").ToString() + ")}))()");
      params->SetInt("contextId", context);
    }
    params->SetBool("awaitPromise", true);
    params->SetBool("returnByValue", false);
    params->SetString("objectGroup", group);
    const int handle_id = next_handle_;
    auto dispatch = [this, id, method, params, handle_id, group, frame_id, context,
                     context_identity, renderer_epoch, message] {
      DevTools(
          id, method, params,
          [this, id, handle_id, group, frame_id, context, context_identity, renderer_epoch,
           message](bool ok, const std::string& json) {
            auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
            auto result = parsed ? parsed->GetDictionary() : nullptr;
            auto remote = result ? result->GetDictionary("result") : nullptr;
            if (!ok || !remote || result->HasKey("exceptionDetails") ||
                remote->GetType("objectId") != VTYPE_STRING) {
              auto release = CefDictionaryValue::Create();
              release->SetString("objectGroup", group);
              SendDevTools("Runtime.releaseObjectGroup", release, operation_sessions_[id]);
              const auto details = result ? result->GetDictionary("exceptionDetails") : nullptr;
              const auto exception = details ? details->GetDictionary("exception") : nullptr;
              const std::string detail =
                  exception && exception->GetType("description") == VTYPE_STRING
                      ? exception->GetString("description").ToString()
                  : details ? details->GetString("text").ToString()
                            : "";
              Complete(id, false,
                       "Native handle creation failed" +
                           (detail.empty() ? std::string("") : ": " + detail));
              return;
            }
            const auto object_id = remote->GetString("objectId").ToString();
            auto properties = CefDictionaryValue::Create();
            properties->SetString("objectId", object_id);
            properties->SetBool("ownProperties", true);
            DevToolsCallback metadata = [this, id, handle_id, group, object_id, frame_id, context,
                                         context_identity, renderer_epoch,
                                         message](bool ok, const std::string& json) {
              auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
              auto result = parsed ? parsed->GetDictionary() : nullptr;
              auto fields = result ? result->GetList("result") : nullptr;
              Dict value;
              bool query_missing = false;
              if (ok && fields)
                for (size_t i = 0; i < fields->GetSize(); ++i) {
                  auto field = fields->GetDictionary(i);
                  if (field && field->GetString("name") == "suitecutQueryMissing") {
                    const auto marker = field->GetDictionary("value");
                    query_missing = marker && marker->GetBool("value");
                  }
                  if (field && field->GetString("name") == "value") {
                    value = field->GetDictionary("value");
                    break;
                  }
                }
              if (message->GetBool("queryHandle") && query_missing) {
                auto release = CefDictionaryValue::Create();
                release->SetString("objectGroup", group);
                SendDevTools("Runtime.releaseObjectGroup", release, operation_sessions_[id]);
                pending_handle_groups_.erase(id);
                RetryContext(id);
                return;
              }
              if (!value) {
                Complete(id, false, "Native handle metadata unavailable");
                return;
              }
              if (message->GetBool("predicate")) {
                const auto type = value->GetString("type").ToString();
                const auto special = value->GetString("unserializableValue").ToString();
                const bool truthy =
                    type != "undefined" && value->GetString("subtype") != "null" &&
                    !(type == "boolean" && !value->GetBool("value")) &&
                    !(type == "string" &&
                      suitecut::DevToolsString(value->GetValue("value")).empty()) &&
                    !(type == "number" && (special == "NaN" || special == "-0" ||
                                           (special.empty() && Number(value, "value") == 0))) &&
                    !(type == "bigint" && (special == "0n" || special == "-0n"));
                if (!truthy) {
                  auto release = CefDictionaryValue::Create();
                  release->SetString("objectGroup", group);
                  SendDevTools("Runtime.releaseObjectGroup", release, operation_sessions_[id]);
                  pending_handle_groups_.erase(id);
                  const auto entry = operations_.Get(id);
                  if (!entry) return;
                  const int interval = message->GetType("pollInterval") == VTYPE_INT
                                           ? message->GetInt("pollInterval")
                                           : 0;
                  CefPostDelayedTask(TID_UI,
                                     new Task([self = CefRefPtr<Source>(this), id,
                                               generation = entry->generation, message] {
                                       if (!self->operations_.IsCurrent(id, generation)) return;
                                       // Array snapshots from a planned selector must never become
                                       // the root of later predicate attempts. Re-run the original
                                       // owned query, preserving its deadline and frame path.
                                       if (message->HasKey("nativeQueryNodes"))
                                         self->RetryContext(id);
                                       else
                                         self->Handle(id, message);
                                     }),
                                     interval);
                  return;
                }
              }
              std::string preview;
              if (value->HasKey("unserializableValue"))
                preview = value->GetString("unserializableValue");
              else if (value->GetString("type") == "string" && value->HasKey("value"))
                preview = suitecut::DevToolsString(value->GetValue("value"));
              else if (value->HasKey("value")) {
                auto primitive = value->GetValue("value");
                preview = CefWriteJSON(primitive, JSON_WRITER_DEFAULT);
              } else if (value->HasKey("description"))
                preview = suitecut::DevToolsString(value->GetValue("description"));
              else
                preview = value->GetString("type");
              const bool node = value->GetString("subtype") == "node";
              const auto node_id = node ? value->GetString("objectId").ToString() : "";
              auto finish = [this, id, handle_id, object_id, group, frame_id, context,
                             context_identity, renderer_epoch, node, node_id,
                             message](std::string preview) {
                if (message->HasKey("nativeFrameOwnerChild")) {
                  const auto child =
                      frame_states_.find(message->GetString("nativeFrameOwnerChild").ToString());
                  if (child == frame_states_.end() || child->second.parent != frame_id) {
                    Complete(id, false, "Native frame detached before frame owner delivery");
                    return;
                  }
                }
                const auto frame = frame_states_.find(frame_id);
                if (frame == frame_states_.end() || frame->second.context != context ||
                    frame->second.session != operation_sessions_[id] ||
                    frame->second.context_identity != context_identity ||
                    native_pristine_renderer_epoch_ != renderer_epoch) {
                  Complete(id, false,
                           "Native handle context destroyed by navigation or detachment");
                  return;
                }
                preview = suitecut::DevToolsTruncateWTF8(preview, 8192);
                auto response = CefDictionaryValue::Create();
                response->SetInt("handle", handle_id);
                response->SetString("guid", HandleGuid(handle_id));
                suitecut::DevToolsSetString(response, "preview",
                                            node ? "JSHandle@" + preview : preview);
                response->SetBool("node", node);
                response->SetString("frame", frame_id);
                auto typed = CefValue::Create();
                typed->SetDictionary(response->Copy(false));
                std::string json;
                if (!suitecut::DevToolsLosslessJSON(typed, json, 65536)) {
                  Complete(id, false, "Native handle metadata limit exceeded");
                  return;
                }
                const auto delivery_owner = operations_.Get(id);
                if (!delivery_owner) return;
                if (operations_.IsExpired(id, delivery_owner->generation, Now())) {
                  Complete(id, false, "Native handle metadata operation timed out");
                  return;
                }
                if (selector_waits_) selector_waits_->Keep(id, group, operation_sessions_[id]);
                handles_.emplace(handle_id, ObjectHandle{object_id, group, frame_id, context,
                                                         frame->second.session, node_id,
                                                         context_identity, renderer_epoch});
                pending_handle_groups_.erase(id);
                Complete(id, true, "", json);
              };
              if (!node) {
                finish(std::move(preview));
                return;
              }
              if (node_id.empty()) {
                Complete(id, false, "Native DOM handle object ID is unavailable");
                return;
              }
              const auto preview_owner = operations_.Get(id);
              if (!preview_owner) return;
              const auto preview_generation = preview_owner->generation;
              if (operations_.IsExpired(id, preview_generation, Now())) {
                Complete(id, false, "Native handle preview operation timed out");
                return;
              }
              auto metadata = CefDictionaryValue::Create();
              metadata->SetString("objectId", node_id);
              metadata->SetInt("depth", 1);
              // Read genuine native node fields, not author properties or serialized
              // descendants. The caption never supplies handle/backend identity.
              DevTools(
                  id, "DOM.describeNode", metadata,
                  [this, id, preview_generation, finish = std::move(finish)](
                      bool ok, const std::string& json) mutable {
                    if (!operations_.IsCurrent(id, preview_generation)) return;
                    if (operations_.IsExpired(id, preview_generation, Now())) {
                      Complete(id, false, "Native handle preview operation timed out");
                      return;
                    }
                    auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
                    auto result = parsed ? parsed->GetDictionary() : nullptr;
                    std::string preview;
                    if (!ok || !result ||
                        !suitecut::DOMHandlePreviewFromInspector(result->GetDictionary("node"),
                                                                 preview)) {
                      Complete(id, false, "Native DOM handle preview metadata is unavailable");
                      return;
                    }
                    if (operations_.IsExpired(id, preview_generation, Now())) {
                      Complete(id, false, "Native handle preview operation timed out");
                      return;
                    }
                    finish(std::move(preview));
                  },
                  std::nullopt, false, kMaxResponseBytes, true);
            };
            if (message->HasKey("bindingArgumentContext"))
              BindingArgumentMetadata(id, object_id, group, std::move(metadata));
            else
              DevTools(id, "Runtime.getProperties", properties, std::move(metadata), std::nullopt,
                       false, kMaxResponseBytes, true);
          });
    };
    if (action == "create" && message->GetType("function") == VTYPE_STRING)
      HandleArguments(id, message, params, !message->HasKey("nativeQueryNodes"),
                      std::move(dispatch));
    else
      dispatch();
    return;
  }
  const int handle_id = message->GetInt("handle");
  const auto found = handles_.find(handle_id);
  if (action == "dispose" || action == "disposeMany") {
    DisposeNativeHandles(id, message);
    return;
  }
  if (found == handles_.end()) {
    Complete(id, false, MissingHandle(handle_id));
    return;
  }
  if (found->second.frame_id != FrameId(message)) {
    Complete(id, false, "Native handle belongs to a different frame context");
    return;
  }
  operation_frames_[id] = found->second.frame_id;
  operation_sessions_[id] = found->second.session;
  if (action == "boundingBox") {
    if (found->second.node_id.empty()) {
      Complete(id, false, "Native handle is not a DOM node");
      return;
    }
    ElementBoundingBox(id, found->second.node_id, found->second.frame_id);
    return;
  }
  if (action == "contentFrame") {
    if (found->second.node_id.empty()) {
      Complete(id, false, "Native handle is not a DOM node");
      return;
    }
    auto params = CefDictionaryValue::Create();
    params->SetString("objectId", found->second.node_id);
    DevTools(id, "DOM.describeNode", params, [this, id](bool ok, const std::string& json) {
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
      auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto node = result ? result->GetDictionary("node") : nullptr;
      if (!ok || !node) {
        Complete(id, false, "Native DOM node unavailable");
        return;
      }
      const auto frame = node->GetString("frameId").ToString();
      Complete(id, true, "",
               frame.empty() || !frame_states_.contains(frame) ? "null" : Literal(frame));
    });
    return;
  }
  if (action == "focus") {
    if (found->second.node_id.empty()) {
      Complete(id, false, "Native handle is not a DOM node");
      return;
    }
    const auto frame = frame_states_.find(found->second.frame_id);
    if (frame == frame_states_.end() || !frame->second.utility_context) {
      Complete(id, false, "Native DOM context unavailable");
      return;
    }
    const int context = frame->second.utility_context;
    evaluation_groups_[id].insert(found->second.session);
    auto describe = CefDictionaryValue::Create();
    describe->SetString("objectId", found->second.node_id);
    DevTools(id, "DOM.describeNode", describe, [this, id, context](bool, const std::string& json) {
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
      auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto node = result ? result->GetDictionary("node") : nullptr;
      if (!node) {
        Complete(id, false, "Native DOM node unavailable");
        return;
      }
      auto resolve = CefDictionaryValue::Create();
      resolve->SetInt("backendNodeId", node->GetInt("backendNodeId"));
      resolve->SetInt("executionContextId", context);
      resolve->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
      DevTools(id, "DOM.resolveNode", resolve, [this, id](bool, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
        auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto node = result ? result->GetDictionary("object") : nullptr;
        if (!node || node->GetType("objectId") != VTYPE_STRING) {
          Complete(id, false, "Native DOM node could not be resolved");
          return;
        }
        auto params = CefDictionaryValue::Create();
        params->SetString("objectId", node->GetString("objectId"));
        params->SetString("functionDeclaration",
                          "function(){if(this.nodeType!==1)throw new Error('Node is not an "
                          "HTMLElement');if(typeof this.focus==='function')this.focus();}");
        params->SetBool("returnByValue", true);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        DevTools(id, "Runtime.callFunctionOn", params, [this, id](bool, const std::string& json) {
          auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
          auto result = parsed ? parsed->GetDictionary() : nullptr;
          if (!result || result->HasKey("exceptionDetails")) {
            Complete(id, false, "Native element focus failed");
            return;
          }
          Complete(id, true);
        });
      });
    });
    return;
  }
  if (action != "evaluate" || message->GetType("function") != VTYPE_STRING) {
    Complete(id, false, "Invalid native handle operation");
    return;
  }
  auto params = CefDictionaryValue::Create();
  params->SetString("objectId", found->second.object_id);
  params->SetString("functionDeclaration", message->GetString("function"));
  params->SetBool("awaitPromise", true);
  params->SetBool("returnByValue", true);
  const auto group = "suitecut-automation-" + std::to_string(id);
  evaluation_groups_[id].insert(operation_sessions_[id]);
  params->SetString("objectGroup", group);
  auto dispatch = [this, id, params] {
    DevTools(id, "Runtime.callFunctionOn", params, [this, id](bool ok, const std::string& json) {
      Complete(id, ok, ok ? "" : "Native handle evaluation failed", json);
    });
  };
  if (message->HasKey("handles"))
    HandleArguments(id, message, params, false, std::move(dispatch));
  else
    dispatch();
}

}  // namespace suitecut::browser
