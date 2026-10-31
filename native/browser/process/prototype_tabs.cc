// SPDX-License-Identifier: MIT
#include "process/prototype_tabs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "init_snapshot_scheme.h"

namespace suitecut::browser::prototype {
namespace {

struct Options {
  std::filesystem::path root;
  bool chrome_pages = true;  // false: Alloy-style pages
  bool hide = false;         // true: keep every tab attached and hide the others
  bool selftest = false;
  bool toolbar = true;  // false: no toolbar view, to isolate Chrome-style limits
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
</style>
<div id=tabs></div>
<div id=bar><button id=back>&#8249;</button><button id=fwd>&#8250;</button><button id=reload>&#8635;</button>
<input id=url spellcheck=false placeholder="Search or type a URL"></div>
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
}
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
  void NewTab(const std::string& url);
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
  Tab* Find(int id);
  Tab* ByBrowser(CefRefPtr<CefBrowser> browser);
  void SelfTest();

  CefRefPtr<CefWindow> window;
  CefRefPtr<Client> pages, toolbar_client;

 private:
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefBrowserView> toolbar_;
  std::vector<Tab> tabs_;
  int active_ = 0, next_tab_ = 1, next_call_ = 1, open_browsers_ = 0;
  bool closing_ = false, close_now_ = false;
  std::map<int, std::function<void(std::string)>> calls_;
};
Shell shell;

class Client : public CefClient,
               public CefDisplayHandler,
               public CefLifeSpanHandler,
               public CefDevToolsMessageObserver {
 public:
  explicit Client(bool toolbar) : toolbar_(toolbar) {}
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

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
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& target_url,
                     const CefString&, WindowOpenDisposition, bool, const CefPopupFeatures&,
                     CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
                     CefRefPtr<CefDictionaryValue>&, bool*) override {
    const auto url = target_url.ToString();
    After(0, [url] { shell.NewTab(url); });
    return true;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { shell.Created(browser, !toolbar_); }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override { shell.Closed(browser); }
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
  CefRect GetInitialBounds(CefRefPtr<CefWindow>) override { return CefRect(120, 120, 1200, 800); }
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
  NewTab(ProbePage("Tab A"));
  window->Show();
  if (options.selftest) After(1500, [this] { SelfTest(); });
}

void Shell::NewTab(const std::string& url) {
  if (closing_ || !window) return;
  Tab tab;
  tab.id = next_tab_++;
  tab.url = url;
  tab.view = CefBrowserView::CreateBrowserView(
      pages, url, CefBrowserSettings(), nullptr, nullptr,
      new ViewDelegate(options.chrome_pages ? CEF_RUNTIME_STYLE_CHROME : CEF_RUNTIME_STYLE_ALLOY, 0));
  tabs_.push_back(tab);
  Select(tab.id);
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
  const auto index = found - tabs_.begin();
  if (found->attached) content_->RemoveChildView(found->view);
  if (auto browser = found->view->GetBrowser()) browser->GetHost()->CloseBrowser(true);
  tabs_.erase(found);
  if (active_ == id) Select(tabs_[std::min<size_t>(index, tabs_.size() - 1)].id);
  else Render();
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

void Shell::Closed(CefRefPtr<CefBrowser>) {
  if (--open_browsers_ == 0 && closing_ && window) window->Close();
}

void Shell::Changed(CefRefPtr<CefBrowser> browser, const std::string* title, const std::string* url) {
  Tab* tab = ByBrowser(browser);
  if (!tab) return;
  if (title) tab->title = *title;
  if (url) tab->url = *url;
  Render();
}

void Shell::Evaluate(int id, const std::string& expression, std::function<void(std::string)> done,
                     bool gesture) {
  Tab* tab = Find(id);
  const auto browser = tab ? tab->view->GetBrowser() : nullptr;
  if (!browser) return done("{\"error\":\"no browser\"}");
  if (!tab->devtools) tab->devtools = browser->GetHost()->AddDevToolsMessageObserver(pages);
  auto params = CefDictionaryValue::Create();
  params->SetString("expression", expression);
  params->SetBool("returnByValue", true);
  params->SetBool("userGesture", gesture);
  const int call = next_call_++;
  calls_[call] = std::move(done);
  if (!browser->GetHost()->ExecuteDevToolsMethod(call, "Runtime.evaluate", params)) {
    auto failed = std::move(calls_[call]);
    calls_.erase(call);
    failed("{\"error\":\"devtools refused\"}");
  }
}

void Shell::Result(int id, bool ok, const std::string& json) {
  const auto found = calls_.find(id);
  if (found == calls_.end()) return;
  auto done = std::move(found->second);
  calls_.erase(found);
  const auto value = ok ? CefParseJSON(json, JSON_PARSER_RFC) : nullptr;
  const auto root = value ? value->GetDictionary() : nullptr;
  const auto result = root ? root->GetDictionary("result") : nullptr;
  if (!result) return done(ok ? json : "{\"error\":" + (json.empty() ? std::string("\"failed\"") : json) + "}");
  if (result->GetType("value") == VTYPE_STRING) return done(result->GetString("value").ToString());
  auto copy = CefValue::Create();
  copy->SetDictionary(result->Copy(false));
  done(CefWriteJSON(copy, JSON_WRITER_DEFAULT).ToString());
}

bool Shell::CanClose() {
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
  // Renderer helpers register the same scheme whatever the browser mode.
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
    RegisterSuiteCutInitScheme(registrar);
  }
  void OnContextInitialized() override {
    shell.pages = new Client(false);
    shell.toolbar_client = new Client(true);
    CefWindow::CreateTopLevelWindow(new WindowDelegate);
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
