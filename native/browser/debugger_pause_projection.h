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
    if (!writer.Text(packet->GetValue("reason"), 1024)) return false;
    writer.out_ += ",\"callFrames\":[";
    const auto frames = packet->GetList("callFrames");
    if (frames->GetSize() == 0 || frames->GetSize() > 64) return false;
    for (size_t i = 0; i < frames->GetSize(); ++i) {
      if (i) writer.out_ += ',';
      if (frames->GetType(i) != VTYPE_DICTIONARY || !writer.Frame(frames->GetDictionary(i))) return false;
    }
    writer.out_ += "],\"hitBreakpoints\":[";
    if (packet->HasKey("hitBreakpoints")) {
      if (packet->GetType("hitBreakpoints") != VTYPE_LIST) return false;
      const auto hits = packet->GetList("hitBreakpoints");
      if (hits->GetSize() > 64) return false;
      for (size_t i = 0; i < hits->GetSize(); ++i) {
        if (i) writer.out_ += ',';
        if (!writer.Text(hits->GetValue(i), 512)) return false;
      }
    }
    writer.out_ += "]}";
    if (writer.out_.size() > kOutput) return false;
    output = std::move(writer.out_);
    return true;
  }
 private:
  static constexpr size_t kOutput = 2 * 1048576;
  std::string out_;
  size_t scopes_ = 0;
  bool Text(CefRefPtr<CefValue> value, size_t maximum) {
    if (!value) return false;
    std::u16string units;
    if (value->GetType() == VTYPE_STRING) units = value->GetString().ToString16();
    else if (!DevToolsReadTag(value, units)) return false;
    if (units.size() > maximum) return false;
    const auto quoted = DevToolsQuoteUnits(units);
    if (quoted.size() > kOutput - std::min(kOutput, out_.size())) return false;
    out_ += quoted;
    return true;
  }
  bool Field(Dict object, const std::string& name, size_t maximum, bool required = false) {
    if (!object->HasKey(name)) return !required;
    out_ += ",\"" + name + "\":";
    return Text(object->GetValue(name), maximum);
  }
  bool Location(Dict location) {
    if (!location || location->GetType("scriptId") != VTYPE_STRING ||
        location->GetString("scriptId").empty() ||
        location->GetType("lineNumber") != VTYPE_INT || location->GetInt("lineNumber") < 0) return false;
    out_ += "{\"scriptId\":";
    if (!Text(location->GetValue("scriptId"), 128)) return false;
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
    if (!Text(object->GetValue("type"), 64) || !Field(object, "subtype", 128) ||
        !Field(object, "className", 8192) || !Field(object, "description", 8192) ||
        !Field(object, "objectId", 512)) return false;
    out_ += '}';
    return true;
  }
  bool Frame(Dict frame) {
    if (!frame || frame->GetType("location") != VTYPE_DICTIONARY ||
        frame->GetType("scopeChain") != VTYPE_LIST || frame->GetType("callFrameId") != VTYPE_STRING ||
        frame->GetString("callFrameId").empty()) return false;
    out_ += "{\"callFrameId\":";
    if (!Text(frame->GetValue("callFrameId"), 512) || !Field(frame, "functionName", 8192, true) ||
        !Field(frame, "url", 8192)) return false;
    out_ += ",\"location\":";
    if (!Location(frame->GetDictionary("location"))) return false;
    if (frame->HasKey("functionLocation")) {
      if (frame->GetType("functionLocation") != VTYPE_DICTIONARY) return false;
      out_ += ",\"functionLocation\":";
      if (!Location(frame->GetDictionary("functionLocation"))) return false;
    }
    const auto scopes = frame->GetList("scopeChain");
    if (scopes->GetSize() > 64 - scopes_) return false;
    scopes_ += scopes->GetSize();
    out_ += ",\"scopeChain\":[";
    for (size_t i = 0; i < scopes->GetSize(); ++i) {
      if (scopes->GetType(i) != VTYPE_DICTIONARY) return false;
      const auto scope = scopes->GetDictionary(i);
      if (i) out_ += ',';
      out_ += "{\"type\":";
      if (!Text(scope->GetValue("type"), 64) || !Field(scope, "name", 8192) ||
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
    return out_.size() <= kOutput;
  }
};
}  // namespace suitecut
