// SPDX-License-Identifier: MIT
#include "process/prototype_tabs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_download_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "init_snapshot_scheme.h"
#if defined(__APPLE__)
#include <chrono>
#include <cmath>

#include "mac_capture.h"
#include "media_pipeline.h"
#include "window_frames.h"
#endif

namespace suitecut::browser::prototype {
namespace {

struct Options {
  std::filesystem::path root;
  bool chrome_pages = true;  // false: Alloy-style pages
  bool hide = false;         // true: keep every tab attached and hide the others
  bool selftest = false;
  bool toolbar = true;  // false: no toolbar view, to isolate Chrome-style limits
  bool control = false;     // JSON-line commands on stdin, answers and events on stdout
  bool autodialog = false;  // accept JS dialogs instead of showing them
  bool hold_dialogs = false;  // control mode: a dialog waits for a "dialog" command
  int width = 1200, height = 800;
  std::string first_url;
  std::string permissions = "ask";  // ask: our toolbar's permission bar; allow; deny
};
Options options;

std::string Arg(int argc, char** argv, const char* name) {
  const std::string prefix = std::string("--") + name + "=";
  for (int i = 1; i < argc; ++i)
    if (std::strncmp(argv[i], prefix.c_str(), prefix.size()) == 0) return argv[i] + prefix.size();
  return "";
}
bool Flag(int argc, char** argv, const char* name) {
  const std::string flag = std::string("--") + name;
  for (int i = 1; i < argc; ++i)
    if (flag == argv[i]) return true;
  return false;
}

void Report(const std::string& line) {
  std::fprintf(stderr, "PROTOTYPE %s\n", line.c_str());
  std::fflush(stderr);
}

// Control-mode output: one JSON object per line on stdout.
void Emit(CefRefPtr<CefDictionaryValue> message) {
  if (!options.control) return;
  auto value = CefValue::Create();
  value->SetDictionary(message);
  std::fprintf(stdout, "%s\n", CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString().c_str());
  std::fflush(stdout);
}
CefRefPtr<CefDictionaryValue> Event(const std::string& name, int tab) {
  auto event = CefDictionaryValue::Create();
  event->SetString("event", name);
  if (tab) event->SetInt("tab", tab);
  return event;
}

class Later : public CefTask {
 public:
  explicit Later(std::function<void()> work) : work_(std::move(work)) {}
  void Execute() override { work_(); }

 private:
  std::function<void()> work_;
  IMPLEMENT_REFCOUNTING(Later);
};
void After(int64_t ms, std::function<void()> work) {
  CefPostDelayedTask(TID_UI, new Later(std::move(work)), ms);
}

std::string DataUrl(const std::string& html) {
  return "data:text/html;base64," + CefBase64Encode(html.data(), html.size()).ToString();
}

// The toolbar is an ordinary page: it posts commands as console messages and
// the shell answers by calling render(state).
const char kToolbar[] = R"html(<!doctype html><meta charset=utf-8><style>
:root{color-scheme:dark;font:13px -apple-system,system-ui,sans-serif}
body{margin:0;background:#202124;color:#e8eaed;user-select:none;overflow:hidden}
#tabs{display:flex;gap:2px;padding:8px 8px 0;height:30px;align-items:flex-end}
.tab{display:flex;align-items:center;gap:6px;max-width:200px;min-width:60px;padding:6px 10px;
  border-radius:8px 8px 0 0;background:#2b2c2f;color:#bdc1c6;cursor:default}
.tab.on{background:#35363a;color:#fff}
.tab span{overflow:hidden;white-space:nowrap;text-overflow:ellipsis;flex:1}
.tab b{font-weight:400;opacity:.6;padding:0 2px}.tab b:hover{opacity:1}
#new{padding:4px 10px;background:none;border:0;color:#bdc1c6;font-size:16px}
#bar{display:flex;gap:6px;align-items:center;padding:6px 8px;background:#35363a;height:28px}
#bar button{background:none;border:0;color:#e8eaed;font-size:15px;width:28px}
#bar button:disabled{opacity:.35}
#url{flex:1;height:26px;border-radius:13px;border:0;padding:0 12px;background:#202124;color:#fff;font:inherit}
#ask{display:none;flex:1;align-items:center;gap:8px;height:26px;padding:0 12px;border-radius:13px;background:#3c4043}
#ask span{flex:1}#ask button{width:auto;padding:2px 10px;border-radius:10px;background:#8ab4f8;color:#202124;font-size:12px}
</style>
<div id=tabs></div>
<div id=bar><button id=back>&#8249;</button><button id=fwd>&#8250;</button><button id=reload>&#8635;</button>
<input id=url spellcheck=false placeholder="Search or type a URL">
<div id=ask><span></span><button id=allow>Allow</button><button id=block>Block</button></div></div>
<script>
const send = (m) => console.log('suitecut-ui:' + JSON.stringify(m));
const tabs = document.getElementById('tabs');
function render(s) {
  tabs.replaceChildren(...s.tabs.map((t) => {
    const el = document.createElement('div');
    el.className = 'tab' + (t.active ? ' on' : '');
    el.innerHTML = '<span></span><b>&times;</b>';
    el.firstChild.textContent = t.title || t.url || 'New tab';
    el.onclick = (e) => send(e.target.tagName === 'B' ? { cmd: 'close', id: t.id } : { cmd: 'select', id: t.id });
    return el;
  }), Object.assign(document.createElement('button'), { id: 'new', textContent: '+', onclick: () => send({ cmd: 'new' }) }));
  if (document.activeElement !== url) url.value = s.url || '';
  back.disabled = !s.back; fwd.disabled = !s.forward;
  ask.style.display = s.prompt ? 'flex' : 'none';
  url.style.display = s.prompt ? 'none' : '';
  if (s.prompt) ask.firstChild.textContent = s.prompt + ' wants: ' + s.wants;
}
allow.onclick = () => send({ cmd: 'permission', allow: true });
block.onclick = () => send({ cmd: 'permission', allow: false });
back.onclick = () => send({ cmd: 'back' });
fwd.onclick = () => send({ cmd: 'forward' });
reload.onclick = () => send({ cmd: 'reload' });
url.onkeydown = (e) => { if (e.key === 'Enter') { send({ cmd: 'navigate', url: url.value }); url.blur(); } };
send({ cmd: 'ready' });
</script>)html";

// A page that shows whether it kept running while its tab was in the background.
std::string ProbePage(const std::string& name) {
  return DataUrl("<!doctype html><title>" + name + "</title><h1>" + name +
                 "</h1><input id=f><p>ticks: <b id=o>0</b></p><script>"
                 "window.token=Math.random().toString(36).slice(2);window.ticks=0;"
                 "window.start=Date.now();window.hiddenSeen=false;"
                 "setInterval(()=>{ticks++;if(document.hidden)hiddenSeen=true;o.textContent=ticks},100);"
                 "document.addEventListener('visibilitychange',()=>{if(document.hidden)hiddenSeen=true});"
                 "</script>");
}
const char kProbe[] =
    "JSON.stringify({token,ticks,elapsed:Date.now()-start,visibility:document.visibilityState,"
    "hiddenSeen,field:f.value})";

class Client;

#if defined(__APPLE__)
using Reply = std::function<void(bool, CefRefPtr<CefValue>)>;
// A recording of the window from the compositor frames macOS hands this process
// (WindowFrames), encoded by the engine's media pipeline. Only frames due at the
// requested rate are copied, on the GPU, cropped to the active tab's page unless
// the whole window was asked for.
struct Recording {
  std::string id, output, ffmpeg;
  double fps = 60, last_ms = -1;
  bool window_area = false;
  int width = 0, height = 0;               // requested encoder canvas; 0 follows the page
  int canvas_width = 0, canvas_height = 0;  // the encoder canvas in use
  int source_width = 0, source_height = 0;  // the first frame's crop
  int64_t max_bytes = 0;
  std::unique_ptr<suitecut::WindowFrames> frames;
  std::unique_ptr<suitecut::MacCapturePool> pool;
  std::shared_ptr<suitecut::MediaPipeline> pipeline;
  uint64_t copied = 0, skipped = 0, failed_copies = 0;
  Reply on_start, on_stop;
};
#endif

struct Tab {
  int id = 0;
  CefRefPtr<CefBrowserView> view;
  std::string title, url;
  bool attached = false;
  CefRefPtr<CefRegistration> devtools;
  int browser_id() const {
    const auto browser = view ? view->GetBrowser() : nullptr;
    return browser ? browser->GetIdentifier() : 0;
  }
};

class Shell {
 public:
  void Build(CefRefPtr<CefWindow> window);
  int NewTab(const std::string& url);
  void Control(const std::string& line);
  void Cdp(int tab, const std::string& method, CefRefPtr<CefDictionaryValue> params,
           std::function<void(bool, std::string)> done);
  void Select(int id);
  void CloseTab(int id);
  void Command(const std::string& json);
  void Render();
  void Created(CefRefPtr<CefBrowser> browser, bool page);
  void Closed(CefRefPtr<CefBrowser> browser);
  void Changed(CefRefPtr<CefBrowser> browser, const std::string* title, const std::string* url);
  void Evaluate(int tab, const std::string& expression, std::function<void(std::string)> done,
                bool gesture = false);
  void Result(int id, bool ok, const std::string& json);
  bool CanClose();
#if defined(__APPLE__)
  void StartRecording(CefRefPtr<CefDictionaryValue> message, Reply reply);
  void StopRecording(const std::string& id, bool cancel, Reply reply);
  void RecordingFrame(const std::shared_ptr<Recording>& recording, void* surface, int width, int height,
                      double now);
  bool StartPipeline(const std::shared_ptr<Recording>& recording, int width, int height, double now);
  void FailRecording(const std::shared_ptr<Recording>& recording, const std::string& reason);
  CefRect PageRect(int width, int height);
  std::map<std::string, std::shared_ptr<Recording>> recordings;
  int next_recording_ = 1;
#endif
  Tab* Find(int id);
  Tab* ByBrowser(CefRefPtr<CefBrowser> browser);
  void SelfTest();

  CefRefPtr<CefWindow> window;
  CefRefPtr<Client> pages, toolbar_client;

 private:
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefBrowserView> toolbar_;
  std::vector<Tab> tabs_;
  std::vector<CefRefPtr<CefBrowserView>> closing_views_;

 public:
  // One prompt at a time, as Chrome queues them.
  struct Prompt {
    std::string origin, wants;
    std::function<void(bool)> answer;
  };
  std::vector<Prompt> prompts;
  std::map<int, CefRefPtr<CefJSDialogCallback>> dialogs;
  void Ask(Prompt prompt);
  void Answer(bool allow);
  int active_ = 0, next_tab_ = 1, next_call_ = 1, open_browsers_ = 0;
  bool closing_ = false, close_now_ = false;
  std::map<int, std::function<void(bool, std::string)>> calls_;
};
Shell shell;

class Client : public CefClient,
               public CefDisplayHandler,
               public CefLifeSpanHandler,
               public CefDownloadHandler,
               public CefJSDialogHandler,
               public CefPermissionHandler,
               public CefLoadHandler,
               public CefDevToolsMessageObserver {
 public:
  explicit Client(bool toolbar) : toolbar_(toolbar) {}
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool loading, bool back, bool forward) override {
    if (toolbar_) return;
    auto event = Event("loading", TabOf(browser));
    event->SetBool("loading", loading);
    event->SetString("url", browser->GetMainFrame()->GetURL());
    event->SetBool("back", back);
    event->SetBool("forward", forward);
    Emit(event);
  }
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode code,
                   const CefString& text, const CefString& url) override {
    if (toolbar_ || !frame->IsMain() || code == ERR_ABORTED) return;
    auto event = Event("load-error", TabOf(browser));
    event->SetInt("code", code);
    event->SetString("error", text);
    event->SetString("url", url);
    Emit(event);
  }

  // Alloy cancels downloads without a handler; save them like Chrome does.
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem>,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override {
    const auto path = options.root / "Downloads" / suggested_name.ToString();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    callback->Continue(path.string(), false);
    auto event = Event("download-started", TabOf(browser));
    event->SetString("path", path.string());
    Emit(event);
    return true;
  }
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> item,
                         CefRefPtr<CefDownloadItemCallback>) override {
    if (!item->IsComplete() && !item->IsCanceled()) return;
    auto event = Event(item->IsComplete() ? "download-complete" : "download-canceled", TabOf(browser));
    event->SetString("path", item->GetFullPath().ToString());
    event->SetDouble("bytes", static_cast<double>(item->GetReceivedBytes()));
    Emit(event);
  }
  bool OnJSDialog(CefRefPtr<CefBrowser> browser, const CefString&, JSDialogType type,
                  const CefString& message, const CefString&, CefRefPtr<CefJSDialogCallback> callback,
                  bool&) override {
    auto event = Event("dialog", TabOf(browser));
    event->SetInt("type", type);
    event->SetString("message", message);
    Emit(event);
    if (options.autodialog) return callback->Continue(true, ""), true;
    if (!options.hold_dialogs) return false;  // CEF shows its native dialog
    // An earlier unanswered dialog on this tab is dismissed, as a page can show one at a time.
    auto& held = shell.dialogs[TabOf(browser)];
    if (held) held->Continue(false, "");
    held = callback;
    return true;
  }
  // Alloy has no permission UI: without an answer a prompt waits forever and
  // queues every later one. Ours is a bar in the toolbar.
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser> browser, uint64_t, const CefString& origin,
                              uint32_t requested, CefRefPtr<CefPermissionPromptCallback> callback) override {
    auto event = Event("permission-prompt", TabOf(browser));
    event->SetString("origin", origin);
    event->SetInt("permissions", static_cast<int>(requested));
    Emit(event);
    shell.Ask({origin.ToString(), Wants(requested), [callback](bool allow) {
                 callback->Continue(allow ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
               }});
    return true;
  }
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
                                      const CefString& origin, uint32_t requested,
                                      CefRefPtr<CefMediaAccessCallback> callback) override {
    auto event = Event("media-permission", TabOf(browser));
    event->SetString("origin", origin);
    event->SetInt("permissions", static_cast<int>(requested));
    Emit(event);
    shell.Ask({origin.ToString(), "camera or microphone", [callback, requested](bool allow) {
                 callback->Continue(allow ? requested : 0);
               }});
    return true;
  }
  static std::string Wants(uint32_t requested) {
    std::string out;
    const std::pair<uint32_t, const char*> names[] = {
        {CEF_PERMISSION_TYPE_CLIPBOARD, "clipboard"}, {CEF_PERMISSION_TYPE_NOTIFICATIONS, "notifications"},
        {CEF_PERMISSION_TYPE_GEOLOCATION, "location"}, {CEF_PERMISSION_TYPE_CAMERA_STREAM, "camera"},
        {CEF_PERMISSION_TYPE_MIC_STREAM, "microphone"}, {CEF_PERMISSION_TYPE_MULTIPLE_DOWNLOADS, "downloads"}};
    for (const auto& [bit, name] : names)
      if (requested & bit) out += (out.empty() ? "" : ", ") + std::string(name);
    return out.empty() ? "a permission" : out;
  }
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser, bool fullscreen) override {
    auto event = Event("fullscreen", TabOf(browser));
    event->SetBool("fullscreen", fullscreen);
    Emit(event);
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    if (toolbar_) return;
    const auto value = title.ToString();
    shell.Changed(browser, &value, nullptr);
  }
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override {
    if (toolbar_ || !frame->IsMain()) return;
    const auto value = url.ToString();
    shell.Changed(browser, nullptr, &value);
  }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message,
                        const CefString&, int) override {
    const auto text = message.ToString();
    if (!toolbar_ || text.rfind("suitecut-ui:", 0) != 0) return false;
    shell.Command(text.substr(12));
    return true;
  }
  // Links and window.open become tabs in our strip, like Chrome's default.
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int,
                     const CefString& target_url, const CefString&, WindowOpenDisposition,
                     bool gesture, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
                     CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override {
    const auto url = target_url.ToString();
    const int opener = TabOf(browser);
    After(0, [url, opener, gesture] {
      auto event = Event("popup", opener);
      event->SetInt("opened", shell.NewTab(url));
      event->SetString("url", url);
      event->SetBool("gesture", gesture);
      Emit(event);
    });
    return true;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { shell.Created(browser, !toolbar_); }
  static int TabOf(CefRefPtr<CefBrowser> browser) {
    const Tab* tab = shell.ByBrowser(browser);
    return tab ? tab->id : 0;
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override { shell.Closed(browser); }
  void OnResetDialogState(CefRefPtr<CefBrowser> browser) override { shell.dialogs.erase(TabOf(browser)); }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id, bool success,
                              const void* result, size_t size) override {
    shell.Result(message_id, success, std::string(static_cast<const char*>(result), size));
  }

 private:
  const bool toolbar_;
  IMPLEMENT_REFCOUNTING(Client);
};

class ViewDelegate : public CefBrowserViewDelegate {
 public:
  ViewDelegate(cef_runtime_style_t style, int height) : style_(style), height_(height) {}
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return style_; }
  // Our own toolbar replaces Chrome's.
  ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>) override {
    return CEF_CTT_NONE;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return height_ ? CefSize(800, height_) : CefSize();
  }

 private:
  const cef_runtime_style_t style_;
  const int height_;
  IMPLEMENT_REFCOUNTING(ViewDelegate);
};

class WindowDelegate : public CefWindowDelegate {
 public:
  // A Chrome-style window may hold Alloy views (our toolbar) and at most one
  // Chrome-style page view; an Alloy window holds only Alloy views.
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return options.chrome_pages ? CEF_RUNTIME_STYLE_CHROME : CEF_RUNTIME_STYLE_ALLOY;
  }
  CefRect GetInitialBounds(CefRefPtr<CefWindow>) override {
    return CefRect(120, 120, options.width, options.height);
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override { shell.Build(window); }
  bool CanClose(CefRefPtr<CefWindow>) override { return shell.CanClose(); }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    shell.window = nullptr;
    CefQuitMessageLoop();
  }

 private:
  IMPLEMENT_REFCOUNTING(WindowDelegate);
};

void Shell::Build(CefRefPtr<CefWindow> created) {
  window = created;
  window->SetTitle("SuiteCut custom UI prototype");
  CefBoxLayoutSettings box;
  box.horizontal = false;
  auto layout = window->SetToBoxLayout(box);
  if (options.toolbar) {
    toolbar_ = CefBrowserView::CreateBrowserView(toolbar_client, DataUrl(kToolbar),
                                                 CefBrowserSettings(), nullptr, nullptr,
                                                 new ViewDelegate(CEF_RUNTIME_STYLE_ALLOY, 76));
    window->AddChildView(toolbar_);
  }
  content_ = CefPanel::CreatePanel(nullptr);
  content_->SetToFillLayout();
  window->AddChildView(content_);
  layout->SetFlexForView(content_, 1);
  NewTab(options.first_url.empty() ? ProbePage("Tab A") : options.first_url);
  window->Show();
  Emit(Event("ready", tabs_.front().id));
  if (options.selftest) After(1500, [this] { SelfTest(); });
}

int Shell::NewTab(const std::string& url) {
  if (closing_ || !window) return 0;
  Tab tab;
  tab.id = next_tab_++;
  tab.url = url;
  tab.view = CefBrowserView::CreateBrowserView(
      pages, url, CefBrowserSettings(), nullptr, nullptr,
      new ViewDelegate(options.chrome_pages ? CEF_RUNTIME_STYLE_CHROME : CEF_RUNTIME_STYLE_ALLOY, 0));
  tabs_.push_back(tab);
  Select(tab.id);
  return tab.id;
}

void Shell::Select(int id) {
  Tab* tab = Find(id);
  if (!tab || !content_) return;
  if (options.hide) {
    if (!tab->attached) content_->AddChildView(tab->view);
    tab->attached = true;
    for (auto& other : tabs_) other.view->SetVisible(other.id == id);
  } else {
    // Swap: only the active tab is in the window; the rest are kept alive by
    // the references this shell holds.
    content_->RemoveAllChildViews();
    for (auto& other : tabs_) other.attached = false;
    content_->AddChildView(tab->view);
    tab->attached = true;
  }
  content_->Layout();
  active_ = id;
  tab->view->RequestFocus();
  Render();
}

void Shell::CloseTab(int id) {
  if (tabs_.size() <= 1) return;
  const auto found = std::find_if(tabs_.begin(), tabs_.end(), [&](const Tab& t) { return t.id == id; });
  if (found == tabs_.end()) return;
  const auto index = static_cast<size_t>(found - tabs_.begin());
  // Show the neighbour first, so the closing view is no longer in the window.
  if (active_ == id) Select(tabs_[index + 1 < tabs_.size() ? index + 1 : index - 1].id);
  const auto closing = std::find_if(tabs_.begin(), tabs_.end(), [&](const Tab& t) { return t.id == id; });
  if (closing->attached) content_->RemoveChildView(closing->view);
  // The view must outlive its browser's close; OnBeforeClose releases it.
  closing_views_.push_back(closing->view);
  if (auto browser = closing->view->GetBrowser()) browser->GetHost()->CloseBrowser(true);
  tabs_.erase(closing);
  dialogs.erase(id);
  Emit(Event("closed", id));
  Render();
}

void Shell::Command(const std::string& json) {
  const auto value = CefParseJSON(json, JSON_PARSER_RFC);
  const auto message = value ? value->GetDictionary() : nullptr;
  if (!message) return;
  const auto cmd = message->GetString("cmd").ToString();
  const auto id = message->GetInt("id");
  Tab* tab = Find(active_);
  const auto browser = tab ? tab->view->GetBrowser() : nullptr;
  if (cmd == "ready") Render();
  else if (cmd == "new") NewTab(ProbePage("Tab " + std::to_string(next_tab_)));
  else if (cmd == "select") Select(id);
  else if (cmd == "permission") Answer(message->GetBool("allow"));
  else if (cmd == "close") CloseTab(id);
  else if (cmd == "back" && browser) browser->GoBack();
  else if (cmd == "forward" && browser) browser->GoForward();
  else if (cmd == "reload" && browser) browser->Reload();
  else if (cmd == "navigate" && browser) {
    auto url = message->GetString("url").ToString();
    if (url.find("://") == std::string::npos && url.rfind("data:", 0) != 0 &&
        url.rfind("about:", 0) != 0)
      url = (url.rfind("localhost", 0) == 0 ? "http://" : "https://") + url;
    browser->GetMainFrame()->LoadURL(url);
  }
}

void Shell::Ask(Prompt prompt) {
  if (options.permissions != "ask") return prompt.answer(options.permissions == "allow");
  prompts.push_back(std::move(prompt));
  Render();
}

void Shell::Answer(bool allow) {
  if (prompts.empty()) return;
  auto prompt = std::move(prompts.front());
  prompts.erase(prompts.begin());
  prompt.answer(allow);
  Render();
}

void Shell::Render() {
  const auto browser = toolbar_ ? toolbar_->GetBrowser() : nullptr;
  if (!browser) return;
  auto state = CefDictionaryValue::Create();
  auto list = CefListValue::Create();
  for (const auto& tab : tabs_) {
    auto item = CefDictionaryValue::Create();
    item->SetInt("id", tab.id);
    item->SetString("title", tab.title);
    item->SetString("url", tab.url.rfind("data:", 0) == 0 ? "" : tab.url);
    item->SetBool("active", tab.id == active_);
    list->SetDictionary(list->GetSize(), item);
  }
  state->SetList("tabs", list);
  if (!prompts.empty()) {
    state->SetString("prompt", prompts.front().origin);
    state->SetString("wants", prompts.front().wants);
  }
  if (Tab* tab = Find(active_)) {
    const auto page = tab->view->GetBrowser();
    state->SetString("url", tab->url.rfind("data:", 0) == 0 ? "" : tab->url);
    state->SetBool("back", page && page->CanGoBack());
    state->SetBool("forward", page && page->CanGoForward());
  }
  auto value = CefValue::Create();
  value->SetDictionary(state);
  browser->GetMainFrame()->ExecuteJavaScript(
      "render(" + CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString() + ")", "", 0);
}

void Shell::Created(CefRefPtr<CefBrowser> browser, bool page) {
  ++open_browsers_;
  if (!page) return;
  if (Tab* tab = ByBrowser(browser)) tab->devtools = browser->GetHost()->AddDevToolsMessageObserver(pages);
}

void Shell::Closed(CefRefPtr<CefBrowser> browser) {
  std::erase_if(closing_views_, [&](const CefRefPtr<CefBrowserView>& view) {
    const auto closed = view->GetBrowser();
    return !closed || closed->IsSame(browser);
  });
  if (--open_browsers_ == 0 && closing_ && window) window->Close();
}

void Shell::Changed(CefRefPtr<CefBrowser> browser, const std::string* title, const std::string* url) {
  Tab* tab = ByBrowser(browser);
  if (!tab) return;
  if (title) tab->title = *title;
  if (url) tab->url = *url;
  auto event = Event("page", tab->id);
  event->SetString("title", tab->title);
  event->SetString("url", tab->url);
  Emit(event);
  Render();
}

void Shell::Cdp(int id, const std::string& method, CefRefPtr<CefDictionaryValue> params,
                std::function<void(bool, std::string)> done) {
  Tab* tab = Find(id);
  const auto browser = tab ? tab->view->GetBrowser() : nullptr;
  if (!browser) return done(false, "{\"message\":\"no browser\"}");
  if (!tab->devtools) tab->devtools = browser->GetHost()->AddDevToolsMessageObserver(pages);
  const int call = next_call_++;
  calls_[call] = std::move(done);
  if (!browser->GetHost()->ExecuteDevToolsMethod(call, method, params)) {
    auto failed = std::move(calls_[call]);
    calls_.erase(call);
    failed(false, "{\"message\":\"devtools refused\"}");
  }
}

void Shell::Evaluate(int id, const std::string& expression, std::function<void(std::string)> done,
                     bool gesture) {
  auto params = CefDictionaryValue::Create();
  params->SetString("expression", expression);
  params->SetBool("returnByValue", true);
  params->SetBool("userGesture", gesture);
  Cdp(id, "Runtime.evaluate", params, [done = std::move(done)](bool ok, std::string json) {
    const auto value = ok ? CefParseJSON(json, JSON_PARSER_RFC) : nullptr;
    const auto root = value ? value->GetDictionary() : nullptr;
    const auto result = root ? root->GetDictionary("result") : nullptr;
    if (!result) return done("{\"error\":" + (json.empty() ? std::string("\"failed\"") : json) + "}");
    if (result->GetType("value") == VTYPE_STRING) return done(result->GetString("value").ToString());
    auto copy = CefValue::Create();
    copy->SetDictionary(result->Copy(false));
    done(CefWriteJSON(copy, JSON_WRITER_DEFAULT).ToString());
  });
}

void Shell::Result(int id, bool ok, const std::string& json) {
  const auto found = calls_.find(id);
  if (found == calls_.end()) return;
  auto done = std::move(found->second);
  calls_.erase(found);
  done(ok, json);
}

// {"id":1,"cmd":"open","url":...} -> {"id":1,"ok":true,"result":...}
void Shell::Control(const std::string& line) {
  const auto parsed = CefParseJSON(line, JSON_PARSER_RFC);
  const auto message = parsed ? parsed->GetDictionary() : nullptr;
  if (!message) return;
  const int id = message->GetInt("id");
  const auto cmd = message->GetString("cmd").ToString();
  const int tab = message->GetInt("tab");
  auto reply = [id](bool ok, CefRefPtr<CefValue> result) {
    auto answer = CefDictionaryValue::Create();
    answer->SetInt("id", id);
    answer->SetBool("ok", ok);
    if (result) answer->SetValue("result", result);
    Emit(answer);
  };
  auto number = [](int value) {
    auto result = CefValue::Create();
    result->SetInt(value);
    return result;
  };
  if (cmd == "open") return reply(true, number(NewTab(message->GetString("url"))));
  if (cmd == "select") return Select(tab), reply(Find(tab) != nullptr, nullptr);
  if (cmd == "close") return CloseTab(tab), reply(true, nullptr);
  if (cmd == "back" || cmd == "forward" || cmd == "reload" || cmd == "stop") {
    Tab* found = Find(tab);
    const auto browser = found ? found->view->GetBrowser() : nullptr;
    if (!browser) return reply(false, nullptr);
    if (cmd == "back") browser->GoBack();
    else if (cmd == "forward") browser->GoForward();
    else if (cmd == "reload") browser->Reload();
    else browser->StopLoad();
    return reply(true, nullptr);
  }
  if (cmd == "permission") {
    const bool pending = !prompts.empty();
    Answer(message->GetBool("allow"));
    return reply(pending, nullptr);
  }
  if (cmd == "dialog") {
    const auto held = dialogs.find(tab);
    if (held == dialogs.end()) return reply(false, nullptr);
    const auto callback = held->second;
    dialogs.erase(held);
    callback->Continue(message->GetBool("accept"), message->GetString("text"));
    return reply(true, nullptr);
  }
  // Runs script in our toolbar, e.g. clicking its permission bar like a user.
  if (cmd == "ui") {
    const auto browser = toolbar_ ? toolbar_->GetBrowser() : nullptr;
    if (browser) browser->GetMainFrame()->ExecuteJavaScript(message->GetString("js"), "", 0);
    return reply(browser != nullptr, nullptr);
  }
  if (cmd == "quit") {
    reply(true, nullptr);
    if (window) window->Close();
    return;
  }
  if (cmd == "tabs") {
    auto list = CefListValue::Create();
    for (const auto& item : tabs_) {
      auto entry = CefDictionaryValue::Create();
      entry->SetInt("tab", item.id);
      entry->SetString("title", item.title);
      entry->SetString("url", item.url);
      entry->SetBool("active", item.id == active_);
      list->SetDictionary(list->GetSize(), entry);
    }
    auto result = CefValue::Create();
    result->SetList(list);
    return reply(true, result);
  }
  if (cmd == "navigate") {
    Tab* found = Find(tab);
    const auto browser = found ? found->view->GetBrowser() : nullptr;
    if (browser) browser->GetMainFrame()->LoadURL(message->GetString("url"));
    return reply(browser != nullptr, nullptr);
  }
#if defined(__APPLE__)
  // {"cmd":"record-start","path":"/abs/out.mp4","fps":60,"area":"page"|"window","width":0,"height":0,
  //  "ffmpegPath":"ffmpeg","maxFileBytes":0} -> {"recording":"recording-1",...} once frames are encoding.
  if (cmd == "record-start") return StartRecording(message, reply);
  // {"cmd":"record-stop","recording":"recording-1","cancel":false} -> the finished file's details.
  if (cmd == "record-stop") return StopRecording(message->GetString("recording"), message->GetBool("cancel"), reply);
  // Diagnostics: {"cmd":"frame-probe","ms":5000,"path":"/abs/last.png"} -> frame counts and timing.
  if (cmd == "frame-probe") {
    if (!window) return reply(false, nullptr);
    return ProbeWindowFrames(window->GetWindowHandle(), message->GetInt("ms"), message->GetString("path"),
                             [reply](std::string json) { reply(true, CefParseJSON(json, JSON_PARSER_RFC)); });
  }
#endif
  if (cmd == "cdp") {
    auto params = message->HasKey("params") ? message->GetDictionary("params")->Copy(false)
                                            : CefDictionaryValue::Create();
    return Cdp(tab, message->GetString("method"), params, [reply](bool ok, std::string json) {
      reply(ok, CefParseJSON(json, JSON_PARSER_RFC));
    });
  }
  reply(false, nullptr);
}

bool Shell::CanClose() {
#if defined(__APPLE__)
  // Stop watching the window's layers before it goes; an unfinished file is removed.
  for (auto& [id, recording] : recordings) {
    recording->frames.reset();
    if (recording->pipeline) recording->pipeline->Cancel("The shared browser closed");
  }
#endif
  if (open_browsers_ == 0 || close_now_) return true;
  if (!closing_) {
    closing_ = true;
    // A detached view's browser may never report closing; do not hang on it.
    After(3000, [this] {
      close_now_ = true;
      if (window) window->Close();
    });
    // Detached tabs are not in the window, so close every browser ourselves.
    if (auto browser = toolbar_ ? toolbar_->GetBrowser() : nullptr) browser->GetHost()->CloseBrowser(true);
    for (const auto& tab : tabs_)
      if (auto browser = tab.view->GetBrowser()) browser->GetHost()->CloseBrowser(true);
  }
  return false;
}

#if defined(__APPLE__)
namespace {
CefRefPtr<CefValue> Text(const std::string& text) {
  auto value = CefValue::Create();
  value->SetString(text);
  return value;
}
double Number(CefRefPtr<CefDictionaryValue> message, const char* key, double fallback) {
  const auto type = message->GetType(key);
  return type == VTYPE_INT ? message->GetInt(key) : type == VTYPE_DOUBLE ? message->GetDouble(key) : fallback;
}
CefRefPtr<CefValue> RecordingInfo(const Recording& recording, const suitecut::MediaPipelineResult* result) {
  auto info = CefDictionaryValue::Create();
  info->SetString("recording", recording.id);
  info->SetString("outputPath", recording.output);
  info->SetDouble("framesPerSecond", recording.fps);
  info->SetString("canvas", std::to_string(recording.canvas_width) + "x" + std::to_string(recording.canvas_height));
  info->SetString("source", std::to_string(recording.source_width) + "x" + std::to_string(recording.source_height));
  info->SetDouble("copiedFrames", static_cast<double>(recording.copied));
  info->SetDouble("skippedFrames", static_cast<double>(recording.skipped));
  if (recording.failed_copies) info->SetDouble("failedCopies", static_cast<double>(recording.failed_copies));
  if (result) {
    info->SetString("encoder", result->encoder);
    info->SetString("container", result->container);
    info->SetDouble("durationMs", result->duration_ms);
    info->SetDouble("inputFrames", static_cast<double>(result->input_frames));
    info->SetDouble("gpuNv12Frames", static_cast<double>(result->gpu_nv12_frames));
    info->SetDouble("outputFrames", static_cast<double>(result->output_frames));
    info->SetDouble("heldFrames", static_cast<double>(result->held_frames));
    info->SetDouble("coalescedFrames", static_cast<double>(result->coalesced_frames));
    info->SetDouble("supersededFrames", static_cast<double>(result->superseded_frames));
    info->SetDouble("outputBytes", static_cast<double>(result->output_bytes));
  }
  auto value = CefValue::Create();
  value->SetDictionary(info);
  return value;
}
}  // namespace

void Shell::StartRecording(CefRefPtr<CefDictionaryValue> message, Reply reply) {
  if (!window) return reply(false, Text("The window is not open"));
  auto recording = std::make_shared<Recording>();
  recording->output = message->GetString("path").ToString();
  recording->ffmpeg = message->HasKey("ffmpegPath") ? message->GetString("ffmpegPath").ToString() : "ffmpeg";
  recording->fps = Number(message, "fps", 60);
  recording->window_area = message->GetString("area") == "window";
  recording->width = static_cast<int>(Number(message, "width", 0));
  recording->height = static_cast<int>(Number(message, "height", 0));
  recording->max_bytes = static_cast<int64_t>(Number(message, "maxFileBytes", 0));
  if (recording->output.empty() || recording->output.front() != '/')
    return reply(false, Text("The recording path must be absolute"));
  if (!(recording->fps >= 1 && recording->fps <= 120))
    return reply(false, Text("fps must be from 1 through 120"));
  if ((recording->width != 0) != (recording->height != 0) || recording->width < 0 || recording->height < 0)
    return reply(false, Text("Give both width and height, or neither"));
  for (const auto& [id, other] : recordings)
    if (other->output == recording->output) return reply(false, Text("That file is already being recorded"));
  // The encoder never replaces a file when it finishes; say so now rather than at stop.
  if (std::error_code error; std::filesystem::exists(recording->output, error))
    return reply(false, Text("The recording file already exists"));
  recording->id = "recording-" + std::to_string(next_recording_++);
  recording->on_start = std::move(reply);
  recordings[recording->id] = recording;
  std::weak_ptr<Recording> weak = recording;
  // Delivers the frame already on screen at once, which starts the pipeline.
  recording->frames = std::make_unique<suitecut::WindowFrames>(
      window->GetWindowHandle(), [this, weak](void* surface, int width, int height, double now) {
        if (const auto recording = weak.lock()) RecordingFrame(recording, surface, width, height, now);
      });
  if (recording->frames->Remote())
    return FailRecording(recording, "Chromium is drawing this window remotely, so its frames never reach "
                                    "the browser process; do not pass --suitecut-window-frames=off");
  After(5000, [this, weak] {
    const auto recording = weak.lock();
    if (recording && !recording->pipeline && recording->on_start)
      FailRecording(recording, "No compositor frames reached the browser process within 5 seconds");
  });
}

// Runs on the main thread the moment the window's layer receives a frame.
void Shell::RecordingFrame(const std::shared_ptr<Recording>& recording, void* surface, int width, int height,
                           double now) {
  // Copy only frames due at the requested rate. A quarter-interval tolerance
  // keeps a 60 Hz display at 60 fps despite jitter; the pipeline holds the last
  // frame through any slot with nothing new.
  if (recording->last_ms >= 0 && now - recording->last_ms < 750.0 / recording->fps) {
    ++recording->skipped;
    return;
  }
  CefRect crop = recording->window_area ? CefRect(0, 0, width, height) : PageRect(width, height);
  crop.width &= ~1;
  crop.height &= ~1;
  if (crop.width < 2 || crop.height < 2) return;
  if (!recording->pipeline && !StartPipeline(recording, crop.width, crop.height, now)) return;
  if (!recording->pipeline->WantsFrames()) return;
  // A crop that matches the canvas goes to the encoder as NV12 without leaving
  // the GPU; any other size (a resized window) is fitted by the media worker.
  const bool nv12 = crop.width == recording->canvas_width && crop.height == recording->canvas_height;
  auto copy = recording->pool->Copy(surface, crop.x, crop.y, crop.width, crop.height, nv12);
  if (!copy) {
    ++recording->failed_copies;
    return;
  }
  if (recording->pipeline->NativeCanvasFrame(std::move(copy), now, {})) {
    ++recording->copied;
    recording->last_ms = now;
  }
}

bool Shell::StartPipeline(const std::shared_ptr<Recording>& recording, int width, int height, double now) {
  recording->source_width = width;
  recording->source_height = height;
  int canvas_width = recording->width ? recording->width : width;
  int canvas_height = recording->height ? recording->height : height;
  // The encoder takes at most 3840x2160; a larger page is scaled to fit.
  const double scale = std::min({1.0, 3840.0 / canvas_width, 2160.0 / canvas_height});
  recording->canvas_width = static_cast<int>(canvas_width * scale) & ~1;
  recording->canvas_height = static_cast<int>(canvas_height * scale) & ~1;
  suitecut::MediaPipelineOptions options;
  options.launcher_path = CefCommandLine::GetGlobalCommandLine()->GetProgram().ToString();
  options.ffmpeg_path = recording->ffmpeg;
  options.output_path = recording->output;
  options.source_width = options.output_width = recording->canvas_width;
  options.source_height = options.output_height = recording->canvas_height;
  options.frames_per_second = options.capture_frames_per_second = recording->fps;
  options.capture_timestamps_exact = true;
  options.hardware_h264 = std::filesystem::path(recording->output).extension() == ".mp4";
  options.worker_canvas_fit = true;
  options.worker_nv12 = true;
  options.start_timestamp_ms = now;
  options.first_frame_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  if (recording->max_bytes > 0) options.max_file_bytes = static_cast<uint64_t>(recording->max_bytes);
  const std::string id = recording->id;
  try {
    if (options.ffmpeg_path.front() != '/') options.ffmpeg_path = suitecut::ResolveMediaExecutable(options.ffmpeg_path);
    // Room for every frame the pipeline may hold at once: its queue (fps / 5),
    // the encoder's four in flight, and the held frame, as NV12.
    const uint64_t frame_bytes = static_cast<uint64_t>(recording->canvas_width) * recording->canvas_height * 3 / 2;
    recording->pool = std::make_unique<suitecut::MacCapturePool>(
        frame_bytes * static_cast<uint64_t>(std::ceil(recording->fps / 5) + 6));
    // Pipeline callbacks run on its worker; everything else happens here.
    recording->pipeline = std::make_shared<suitecut::MediaPipeline>(
        std::move(options),
        [this, id](const suitecut::MediaPipelineResult& result) {
          CefPostTask(TID_UI, new Later([this, id, result] {
            const auto found = recordings.find(id);
            if (found == recordings.end()) return;
            const auto recording = found->second;
            recordings.erase(found);
            recording->frames.reset();
            if (recording->on_start) recording->on_start(false, Text(result.failure));
            else if (recording->on_stop)
              recording->on_stop(result.failure.empty() || result.failure == "Native recording canceled",
                                 result.failure.empty() ? RecordingInfo(*recording, &result) : Text(result.failure));
          }));
        },
        [this, id](double) {
          CefPostTask(TID_UI, new Later([this, id] {
            const auto found = recordings.find(id);
            if (found == recordings.end() || !found->second->on_start) return;
            auto reply = std::move(found->second->on_start);
            found->second->on_start = nullptr;
            reply(true, RecordingInfo(*found->second, nullptr));
          }));
        },
        [this, id](const suitecut::MediaPipelineResult& result) {
          // The encoder is ready; a static page will not paint again by itself.
          if (result.first_timestamp_ms >= 0 || !result.preparation_packets) return;
          CefPostTask(TID_UI, new Later([this, id] {
            const auto found = recordings.find(id);
            if (found == recordings.end() || !found->second->frames) return;
            found->second->last_ms = -1;
            found->second->frames->Redeliver();
          }));
        });
  } catch (const std::exception& error) {
    FailRecording(recording, error.what());
    return false;
  }
  return true;
}

void Shell::FailRecording(const std::shared_ptr<Recording>& recording, const std::string& reason) {
  recording->frames.reset();
  if (recording->pipeline) return recording->pipeline->Cancel(reason);  // its completion replies
  recordings.erase(recording->id);
  if (recording->on_start) recording->on_start(false, Text(reason));
  recording->on_start = nullptr;
}

void Shell::StopRecording(const std::string& id, bool cancel, Reply reply) {
  const auto found = recordings.find(id);
  if (found == recordings.end() || found->second->on_start)
    return reply(false, Text("No recording " + id + " is running"));
  const auto recording = found->second;
  recording->on_stop = std::move(reply);
  recording->frames.reset();
  if (cancel) recording->pipeline->Cancel("Native recording canceled");
  else
    recording->pipeline->Stop(suitecut::WindowFramesNow(),
                              std::chrono::steady_clock::now() + std::chrono::seconds(30));
}

// The active tab's page inside the window's compositor surface, in pixels.
CefRect Shell::PageRect(int width, int height) {
  const Tab* tab = Find(active_);
  if (!window || !tab || !tab->view) return CefRect(0, 0, width, height);
  const CefRect client = window->GetClientAreaBoundsInScreen();
  const CefRect page = tab->view->GetBoundsInScreen();
  if (client.width <= 0 || client.height <= 0) return CefRect();
  const double sx = static_cast<double>(width) / client.width, sy = static_cast<double>(height) / client.height;
  const auto x = [&](int value) { return std::clamp(static_cast<int>(std::lround((value - client.x) * sx)), 0, width); };
  const auto y = [&](int value) { return std::clamp(static_cast<int>(std::lround((value - client.y) * sy)), 0, height); };
  const int left = x(page.x), top = y(page.y);
  return CefRect(left, top, x(page.x + page.width) - left, y(page.y + page.height) - top);
}
#endif

Tab* Shell::Find(int id) {
  for (auto& tab : tabs_)
    if (tab.id == id) return &tab;
  return nullptr;
}
Tab* Shell::ByBrowser(CefRefPtr<CefBrowser> browser) {
  for (auto& tab : tabs_)
    if (tab.browser_id() == browser->GetIdentifier()) return &tab;
  return nullptr;
}

// Background tabs: A runs a 100 ms timer and holds typed state, B is opened
// over it for five seconds, then A is shown again. Then Chrome features.
void Shell::SelfTest() {
  const int a = tabs_.front().id;
  const int a_browser = tabs_.front().browser_id();
  Report(std::string("{\"mode\":\"") + (options.chrome_pages ? "chrome" : "alloy") + "\",\"switch\":\"" +
         (options.hide ? "hide" : "swap") + "\"}");
  Evaluate(a, "f.value='kept';token", [this, a, a_browser](std::string token) {
    Report("{\"step\":\"A before\",\"token\":\"" + token + "\"}");
    NewTab(ProbePage("Tab B"));
    After(5000, [this, a, a_browser] {
      Evaluate(a, kProbe, [](std::string probe) { Report("{\"step\":\"A in background\",\"probe\":" + probe + "}"); });
      After(300, [this, a, a_browser] {
        Select(a);
        After(1000, [this, a, a_browser] {
          const Tab* tab = Find(a);
          Report("{\"step\":\"A same browser\",\"same\":" +
                 std::string(tab && tab->browser_id() == a_browser ? "true" : "false") + "}");
          Evaluate(a, kProbe, [](std::string probe) { Report("{\"step\":\"A shown again\",\"probe\":" + probe + "}"); });
          Evaluate(a,
                   "JSON.stringify({ua:navigator.userAgent,pdfViewer:navigator.pdfViewerEnabled,"
                   "chromeObject:typeof chrome,brands:navigator.userAgentData&&navigator.userAgentData.brands.map(b=>b.brand)})",
                   [](std::string probe) { Report("{\"step\":\"features\",\"probe\":" + probe + "}"); });
          Evaluate(a, "window.open('" + ProbePage("Popup") + "');'opened'",
                   [](std::string) {}, true);
          After(1500, [this] {
            Report("{\"step\":\"after window.open\",\"tabs\":" + std::to_string(tabs_.size()) + "}");
            NewTab("chrome://version");
            const int version = tabs_.back().id;
            After(2000, [this, version] {
              Evaluate(version, "JSON.stringify({title:document.title,href:location.href,text:document.body.innerText.slice(0,80)})",
                       [this](std::string probe) {
                         Report("{\"step\":\"chrome://version\",\"probe\":" + probe + "}");
                         Report("{\"step\":\"done\",\"tabs\":" + std::to_string(tabs_.size()) + "}");
                         After(500, [this] { if (window) window->Close(); });
                       });
            });
          });
        });
      });
    });
  });
}

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  // A page in a new document widget (RenderDocument, or the process swap
  // BackForwardCache makes) reads outerWidth, outerHeight and screenX as 0
  // until shortly after load in Alloy Views on macOS; Chrome never reports 0 for
  // a visible window, and BotD flags it as headless. 7 of 40 navigations hit
  // it, none with both features off.
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    if (!process_type.empty()) return;
    std::string disabled = command_line->GetSwitchValue("disable-features");
    disabled += std::string(disabled.empty() ? "" : ",") + "RenderDocument,BackForwardCache";
#if defined(__APPLE__)
    // Chromium then hands each finished frame to this process as an IOSurface
    // on the window's layer, instead of drawing into the window from the GPU
    // process. That is what recording reads (WindowFrames). Pages cannot tell.
    if (command_line->GetSwitchValue("suitecut-window-frames") != "off") disabled += ",RemoteCoreAnimationAPI";
#endif
    command_line->AppendSwitchWithValue("disable-features", disabled);
  }
  // Renderer helpers register the same scheme whatever the browser mode.
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
    RegisterSuiteCutInitScheme(registrar);
  }
  void OnContextInitialized() override {
    shell.pages = new Client(false);
    shell.toolbar_client = new Client(true);
    CefWindow::CreateTopLevelWindow(new WindowDelegate);
    if (options.control)
      std::thread([] {
        std::string line;
        while (std::getline(std::cin, line))
          CefPostTask(TID_UI, new Later([line] { shell.Control(line); }));
        CefPostTask(TID_UI, new Later([] { if (shell.window) shell.window->Close(); }));
      }).detach();
  }

 private:
  IMPLEMENT_REFCOUNTING(App);
};

}  // namespace

bool Requested(int argc, char** argv) { return Arg(argc, argv, "suitecut-prototype") == "tabs"; }

int Run(const CefMainArgs& args, int argc, char** argv) {
  // Never a real profile: the caller passes an empty temporary directory.
  options.root = Arg(argc, argv, "suitecut-prototype-root");
  if (options.root.empty() || !options.root.is_absolute()) {
    std::fprintf(stderr, "SuiteCut prototype: --suitecut-prototype-root=<absolute temp dir> is required\n");
    return 1;
  }
  options.chrome_pages = Arg(argc, argv, "suitecut-prototype-pages") != "alloy";
  options.hide = Arg(argc, argv, "suitecut-prototype-switch") == "hide";
  options.selftest = Flag(argc, argv, "suitecut-prototype-selftest");
  options.toolbar = Arg(argc, argv, "suitecut-prototype-toolbar") != "none";
  options.control = Flag(argc, argv, "suitecut-prototype-control");
  options.autodialog = Flag(argc, argv, "suitecut-prototype-autodialog");
  options.first_url = Arg(argc, argv, "suitecut-prototype-url");
  options.hold_dialogs = options.control && Arg(argc, argv, "suitecut-prototype-dialogs") == "control";
  if (const auto size = Arg(argc, argv, "suitecut-prototype-size"); !size.empty())
    std::sscanf(size.c_str(), "%dx%d", &options.width, &options.height);
  if (const auto value = Arg(argc, argv, "suitecut-prototype-permissions"); !value.empty())
    options.permissions = value;
  if (options.hide && options.chrome_pages) {
    std::fprintf(stderr, "SuiteCut prototype: a window holds one Chrome-style page; hide needs alloy pages\n");
    return 1;
  }
  std::error_code error;
  std::filesystem::create_directories(options.root, error);
  if (error) return 1;
  CefSettings settings;
  CefString(&settings.root_cache_path) = options.root.string();
  CefString(&settings.cache_path) = options.root.string();
  settings.log_severity = LOGSEVERITY_ERROR;
  if (!CefInitialize(args, settings, new App, nullptr)) return 1;
  CefRunMessageLoop();
  shell = Shell();
  CefShutdown();
  return 0;
}

}  // namespace suitecut::browser::prototype
