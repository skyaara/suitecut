// SPDX-License-Identifier: MIT
// Host events: what a window showing a page needs from the engine beyond its
// frames (loading state, title, cursor, the IME composition's place), and the
// commands such a window sends back (stop, IME composition, edit commands).
//
// They are off unless the engine was launched with --suitecut-host-events=1.
// A client that does not ask never sees a "host" packet, so older clients,
// which refuse packet types they do not know, keep working.
#include "page/source.h"

#include <climits>

#include "include/cef_command_line.h"

namespace suitecut::browser {
bool HostEventsEnabled() {
  static const bool on = [] {
    const auto line = CefCommandLine::GetGlobalCommandLine();
    return line && line->GetSwitchValue("suitecut-host-events") == "1";
  }();
  return on;
}

namespace {
bool HostEventsOn() { return HostEventsEnabled(); }

// CSS cursor names for CEF's cursor types.
const char* CssCursor(cef_cursor_type_t type) {
  switch (type) {
    case CT_POINTER: return "default";
    case CT_CROSS: return "crosshair";
    case CT_HAND: return "pointer";
    case CT_IBEAM: return "text";
    case CT_WAIT: return "wait";
    case CT_HELP: return "help";
    case CT_EASTRESIZE: return "e-resize";
    case CT_NORTHRESIZE: return "n-resize";
    case CT_NORTHEASTRESIZE: return "ne-resize";
    case CT_NORTHWESTRESIZE: return "nw-resize";
    case CT_SOUTHRESIZE: return "s-resize";
    case CT_SOUTHEASTRESIZE: return "se-resize";
    case CT_SOUTHWESTRESIZE: return "sw-resize";
    case CT_WESTRESIZE: return "w-resize";
    case CT_NORTHSOUTHRESIZE: return "ns-resize";
    case CT_EASTWESTRESIZE: return "ew-resize";
    case CT_NORTHEASTSOUTHWESTRESIZE: return "nesw-resize";
    case CT_NORTHWESTSOUTHEASTRESIZE: return "nwse-resize";
    case CT_COLUMNRESIZE: return "col-resize";
    case CT_ROWRESIZE: return "row-resize";
    case CT_MOVE: return "move";
    case CT_VERTICALTEXT: return "vertical-text";
    case CT_CELL: return "cell";
    case CT_CONTEXTMENU: return "context-menu";
    case CT_ALIAS: return "alias";
    case CT_PROGRESS: return "progress";
    case CT_NODROP: return "no-drop";
    case CT_COPY: return "copy";
    case CT_NONE: return "none";
    case CT_NOTALLOWED: return "not-allowed";
    case CT_ZOOMIN: return "zoom-in";
    case CT_ZOOMOUT: return "zoom-out";
    case CT_GRAB: return "grab";
    case CT_GRABBING: return "grabbing";
    case CT_MIDDLEPANNING:
    case CT_EASTPANNING:
    case CT_NORTHPANNING:
    case CT_NORTHEASTPANNING:
    case CT_NORTHWESTPANNING:
    case CT_SOUTHPANNING:
    case CT_SOUTHEASTPANNING:
    case CT_SOUTHWESTPANNING:
    case CT_WESTPANNING: return "all-scroll";
    case CT_CUSTOM: return "custom";
    default: return "default";
  }
}

// A range from a command: [start, end] in UTF-16 code units, or none.
CefRange RangeOf(const Dict& message, const char* key) {
  if (message->GetType(key) != VTYPE_LIST) return CefRange(UINT32_MAX, UINT32_MAX);
  const auto list = message->GetList(key);
  if (list->GetSize() != 2 || list->GetType(0) != VTYPE_INT || list->GetType(1) != VTYPE_INT ||
      list->GetInt(0) < 0 || list->GetInt(1) < list->GetInt(0))
    return CefRange(UINT32_MAX, UINT32_MAX);
  return CefRange(static_cast<uint32_t>(list->GetInt(0)), static_cast<uint32_t>(list->GetInt(1)));
}
}  // namespace

auto Source::HostEvent(const char* name, Dict fields) -> void {
  if (!HostEventsOn() || closing_page_) return;
  fields->SetString("event", name);
  const auto json = Json(fields);
  SendPacket(Header("host"), std::vector<uint8_t>(json.begin(), json.end()));
}

auto Source::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool loading, bool back, bool forward)
    -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnLoadingStateChange(browser, loading, back, forward);
  auto fields = CefDictionaryValue::Create();
  fields->SetBool("loading", loading);
  fields->SetBool("canGoBack", back);
  fields->SetBool("canGoForward", forward);
  HostEvent("loading", fields);
}

auto Source::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser, double progress) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnLoadingProgressChange(browser, progress);
  auto fields = CefDictionaryValue::Create();
  fields->SetDouble("progress", progress);
  HostEvent("progress", fields);
}

auto Source::OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor, cef_cursor_type_t type,
                            const CefCursorInfo& custom) -> bool {
  if (auto tab = AdoptedTab(browser)) return tab->OnCursorChange(browser, cursor, type, custom);
  if (!HostEventsOn()) return false;
  auto fields = CefDictionaryValue::Create();
  fields->SetString("cursor", CssCursor(type));
  if (type == CT_CUSTOM) {
    fields->SetInt("hotspotX", custom.hotspot.x);
    fields->SetInt("hotspotY", custom.hotspot.y);
    fields->SetInt("width", custom.size.width);
    fields->SetInt("height", custom.size.height);
  }
  HostEvent("cursor", fields);
  return true;  // the host draws it; there is no native window here
}

auto Source::OnImeCompositionRangeChanged(CefRefPtr<CefBrowser> browser, const CefRange& range,
                                          const RectList& bounds) -> void {
  if (auto tab = AdoptedTab(browser)) return tab->OnImeCompositionRangeChanged(browser, range, bounds);
  auto fields = CefDictionaryValue::Create();
  auto selected = CefListValue::Create();
  selected->SetInt(0, static_cast<int>(range.from));
  selected->SetInt(1, static_cast<int>(range.to));
  fields->SetList("range", selected);
  auto rects = CefListValue::Create();
  for (size_t i = 0; i < bounds.size(); ++i) {
    auto rect = CefListValue::Create();
    rect->SetInt(0, bounds[i].x);
    rect->SetInt(1, bounds[i].y);
    rect->SetInt(2, bounds[i].width);
    rect->SetInt(3, bounds[i].height);
    rects->SetList(i, rect);
  }
  fields->SetList("bounds", rects);
  HostEvent("ime", fields);
}

// {"command":"host","action":"stop"|"ime"|"edit", ...}
auto Source::HostCommand(int id, Dict message) -> void {
  if (!browser_) {
    Complete(id, false, "The page has no browser yet");
    return;
  }
  const auto action = message->GetString("action").ToString();
  auto host = browser_->GetHost();
  if (action == "stop") {
    browser_->StopLoad();
    Complete(id, true);
    return;
  }
  if (action == "ime") {
    const auto op = message->GetString("op").ToString();
    // Composition goes to the focused editable of a focused view.
    host->SetFocus(true);
    if (op == "set") {
      if (message->GetType("text") != VTYPE_STRING) {
        Complete(id, false, "An IME composition has text");
        return;
      }
      const auto text = message->GetString("text");
      auto selection = RangeOf(message, "selection");
      if (selection.from == UINT32_MAX) {
        const auto end = static_cast<uint32_t>(text.length());
        selection = CefRange(end, end);
      }
      host->ImeSetComposition(text, {}, RangeOf(message, "replacement"), selection);
    } else if (op == "commit") {
      if (message->GetType("text") != VTYPE_STRING) {
        Complete(id, false, "An IME commit has text");
        return;
      }
      host->ImeCommitText(message->GetString("text"), RangeOf(message, "replacement"), 0);
    } else if (op == "finish") {
      host->ImeFinishComposingText(message->GetType("keepSelection") == VTYPE_BOOL && message->GetBool("keepSelection"));
    } else if (op == "cancel") {
      host->ImeCancelComposition();
    } else {
      Complete(id, false, "An IME op is set, commit, finish or cancel");
      return;
    }
    Complete(id, true);
    return;
  }
  if (action == "edit") {
    auto frame = browser_->GetFocusedFrame();
    if (!frame) frame = browser_->GetMainFrame();
    const auto edit = message->GetString("edit").ToString();
    host->SetFocus(true);
    if (edit == "copy") frame->Copy();
    else if (edit == "cut") frame->Cut();
    else if (edit == "paste") frame->Paste();
    else if (edit == "undo") frame->Undo();
    else if (edit == "redo") frame->Redo();
    else if (edit == "selectAll") frame->SelectAll();
    else if (edit == "delete") frame->Delete();
    else {
      Complete(id, false, "An edit is copy, cut, paste, undo, redo, selectAll or delete");
      return;
    }
    Complete(id, true);
    return;
  }
  Complete(id, false, "A host command is stop, ime or edit");
}

}  // namespace suitecut::browser
