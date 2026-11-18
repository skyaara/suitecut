// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include "devtools_utf16.h"

namespace suitecut {
// Only selected Inspector metadata is projected. In particular an author's
// RemoteObject.value is never traversed by the native scalar tag serializer.
class DebuggerPauseProjection {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  static bool Build(Dict packet, const std::string& session,
                    const std::string& pause, std::string& output) {
    DebuggerPauseProjection writer;
    if (!packet || packet->GetType("callFrames") != VTYPE_LIST) return false;
    writer.out_ = "{\"session\":" + DevToolsQuoteUnits(CefString(session).ToString16()) +
                  ",\"pause\":" + DevToolsQuoteUnits(CefString(pause).ToString16()) + ",\"reason\":";
    if (!writer.Text(packet->GetValue("reason"))) return false;
    writer.out_ += ",\"callFrames\":[";
    const auto frames = packet->GetList("callFrames");
    if (frames->GetSize() == 0) return false;
    for (size_t i = 0; i < frames->GetSize(); ++i) {
      if (i) writer.out_ += ',';
      if (frames->GetType(i) != VTYPE_DICTIONARY || !writer.Frame(frames->GetDictionary(i))) return false;
    }
    writer.out_ += "],\"hitBreakpoints\":[";
    if (packet->HasKey("hitBreakpoints")) {
      if (packet->GetType("hitBreakpoints") != VTYPE_LIST) return false;
      const auto hits = packet->GetList("hitBreakpoints");
      for (size_t i = 0; i < hits->GetSize(); ++i) {
        if (i) writer.out_ += ',';
        if (!writer.Text(hits->GetValue(i))) return false;
      }
    }
    writer.out_ += "]}";
    output = std::move(writer.out_);
    return true;
  }
 private:
  std::string out_;
  bool Text(CefRefPtr<CefValue> value) {
    if (!value) return false;
    std::u16string units;
    if (value->GetType() == VTYPE_STRING) units = value->GetString().ToString16();
    else if (!DevToolsReadTag(value, units)) return false;
    out_ += DevToolsQuoteUnits(units);
    return true;
  }
  bool Field(Dict object, const std::string& name, bool required = false) {
    if (!object->HasKey(name)) return !required;
    out_ += ",\"" + name + "\":";
    return Text(object->GetValue(name));
  }
  bool Location(Dict location) {
    if (!location || location->GetType("scriptId") != VTYPE_STRING ||
        location->GetString("scriptId").empty() ||
        location->GetType("lineNumber") != VTYPE_INT || location->GetInt("lineNumber") < 0) return false;
    out_ += "{\"scriptId\":";
    if (!Text(location->GetValue("scriptId"))) return false;
    out_ += ",\"lineNumber\":" + std::to_string(location->GetInt("lineNumber"));
    if (location->HasKey("columnNumber")) {
      if (location->GetType("columnNumber") != VTYPE_INT || location->GetInt("columnNumber") < 0) return false;
      out_ += ",\"columnNumber\":" + std::to_string(location->GetInt("columnNumber"));
    }
    out_ += '}';
    return true;
  }
  bool Remote(Dict object) {
    if (!object) return false;
    out_ += "{\"type\":";
    if (!Text(object->GetValue("type")) || !Field(object, "subtype") ||
        !Field(object, "className") || !Field(object, "description") ||
        !Field(object, "objectId")) return false;
    out_ += '}';
    return true;
  }
  bool Frame(Dict frame) {
    if (!frame || frame->GetType("location") != VTYPE_DICTIONARY ||
        frame->GetType("scopeChain") != VTYPE_LIST || frame->GetType("callFrameId") != VTYPE_STRING ||
        frame->GetString("callFrameId").empty()) return false;
    out_ += "{\"callFrameId\":";
    if (!Text(frame->GetValue("callFrameId")) || !Field(frame, "functionName", true) ||
        !Field(frame, "url")) return false;
    out_ += ",\"location\":";
    if (!Location(frame->GetDictionary("location"))) return false;
    if (frame->HasKey("functionLocation")) {
      if (frame->GetType("functionLocation") != VTYPE_DICTIONARY) return false;
      out_ += ",\"functionLocation\":";
      if (!Location(frame->GetDictionary("functionLocation"))) return false;
    }
    const auto scopes = frame->GetList("scopeChain");
    out_ += ",\"scopeChain\":[";
    for (size_t i = 0; i < scopes->GetSize(); ++i) {
      if (scopes->GetType(i) != VTYPE_DICTIONARY) return false;
      const auto scope = scopes->GetDictionary(i);
      if (i) out_ += ',';
      out_ += "{\"type\":";
      if (!Text(scope->GetValue("type")) || !Field(scope, "name") ||
          scope->GetType("object") != VTYPE_DICTIONARY) return false;
      out_ += ",\"object\":";
      if (!Remote(scope->GetDictionary("object"))) return false;
      for (const auto* key : {"startLocation", "endLocation"}) {
        if (!scope->HasKey(key)) continue;
        if (scope->GetType(key) != VTYPE_DICTIONARY) return false;
        out_ += std::string(",\"") + key + "\":";
        if (!Location(scope->GetDictionary(key))) return false;
      }
      out_ += '}';
    }
    out_ += ']';
    if (frame->HasKey("this")) {
      if (frame->GetType("this") != VTYPE_DICTIONARY) return false;
      out_ += ",\"this\":";
      if (!Remote(frame->GetDictionary("this"))) return false;
    }
    out_ += '}';
    return true;
  }
};
}  // namespace suitecut
