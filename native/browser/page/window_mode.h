// SPDX-License-Identifier: MIT
#pragma once

#include "include/cef_command_handler.h"
#include "include/cef_command_line.h"
#include "include/cef_id_mappers.h"
#include "include/cef_menu_model.h"
#include "include/internal/cef_types_wrappers.h"

namespace suitecut::browser {

// `--suitecut-window-mode=chrome` shows each page as a real Chrome-style
// window (tabs, omnibox, menus) instead of an offscreen Alloy browser. CEF
// requires Alloy style for windowless rendering, so frame capture, recording
// and the live viewer are unavailable for these pages.
inline bool ChromeWindowMode() {
  static const bool chrome =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-window-mode") == "chrome";
  return chrome;
}

// `--suitecut-new-pages=windows` gives each new page a window of its own;
// otherwise, like Chrome, a context's later pages open as tabs.
inline bool WindowsByDefault() {
  static const bool windows =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-new-pages") == "windows";
  return windows;
}

inline void ConfigurePageWindow(CefWindowInfo& window, int width, int height) {
  if (ChromeWindowMode()) {
    // A null parent asks CEF for its default Chrome window. Bounds size the
    // content area; the window manager places it.
    window.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    window.bounds = CefRect(0, 0, width, height);
    return;
  }
  window.SetAsWindowless(0);
#if defined(__APPLE__)
  window.shared_texture_enabled =
      CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-shared-texture") != "0";
#endif
}

// Chrome window mode keeps page inspection out of a person's reach: DevTools,
// View Source and Inspect are hidden from menus and refused from shortcuts.
inline bool InspectionCommand(int id) {
  static const int ids[] = {
      cef_id_for_command_id_name("IDC_DEV_TOOLS"),
      cef_id_for_command_id_name("IDC_DEV_TOOLS_CONSOLE"),
      cef_id_for_command_id_name("IDC_DEV_TOOLS_DEVICES"),
      cef_id_for_command_id_name("IDC_DEV_TOOLS_INSPECT"),
      cef_id_for_command_id_name("IDC_DEV_TOOLS_TOGGLE"),
      cef_id_for_command_id_name("IDC_VIEW_SOURCE"),
      cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_INSPECTELEMENT"),
      cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_INSPECTELEMENT_WITH_DEVTOOLS"),
      cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_INSPECTBACKGROUNDPAGE"),
      cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_VIEWPAGESOURCE"),
      cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_VIEWFRAMESOURCE"),
  };
  for (const int known : ids)
    if (known > 0 && known == id) return true;
  return false;
}

class ChromeWindowCommands : public CefCommandHandler {
 public:
  bool OnChromeCommand(CefRefPtr<CefBrowser>, int id, cef_window_open_disposition_t) override {
    return InspectionCommand(id);
  }
  bool IsChromeAppMenuItemVisible(CefRefPtr<CefBrowser>, int id) override {
    return !InspectionCommand(id);
  }
  bool IsChromeAppMenuItemEnabled(CefRefPtr<CefBrowser>, int id) override {
    return !InspectionCommand(id);
  }

 private:
  IMPLEMENT_REFCOUNTING(ChromeWindowCommands);
};

inline void RemoveInspectionItems(CefRefPtr<CefMenuModel> model) {
  for (size_t i = model->GetCount(); i-- > 0;) {
    if (InspectionCommand(model->GetCommandIdAt(i))) model->RemoveAt(i);
    else if (auto sub = model->GetSubMenuAt(i)) RemoveInspectionItems(sub);
  }
}

}  // namespace suitecut::browser
