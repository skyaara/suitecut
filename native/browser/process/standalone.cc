// SPDX-License-Identifier: MIT
#include "process/standalone.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include "browser_profiles.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_request_context.h"
#include "init_snapshot_scheme.h"
#include "profile_lock.h"

namespace suitecut::browser::standalone {
namespace {

std::filesystem::path DefaultRoot() {
  const char* home = std::getenv("HOME");
  const std::filesystem::path base = home && *home ? home : "/tmp";
  // Beside other SuiteCut settings (the release signing config), not among them.
#if defined(__APPLE__)
  return base / "Library" / "Application Support" / "SuiteCut" / "Browser";
#else
  return base / ".config" / "SuiteCut" / "Browser";
#endif
}

class Client : public CefClient, public CefLifeSpanHandler {
 public:
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { browsers_.push_back(browser); }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    std::erase_if(browsers_, [&](const auto& open) { return open->IsSame(browser); });
#if defined(__APPLE__)
    // A Mac browser keeps running with no windows until the person quits.
    if (quitting_ && browsers_.empty()) CefQuitMessageLoop();
#else
    if (browsers_.empty()) CefQuitMessageLoop();
#endif
  }
  void Quit() {
    quitting_ = true;
    if (browsers_.empty()) {
      CefQuitMessageLoop();
      return;
    }
    // Pages may still ask to confirm leaving; a cancelled close keeps that
    // window, and closing it later finishes the quit.
    for (const auto& browser : std::vector(browsers_)) browser->GetHost()->CloseBrowser(false);
  }
  const std::vector<CefRefPtr<CefBrowser>>& browsers() const { return browsers_; }

 private:
  std::vector<CefRefPtr<CefBrowser>> browsers_;
  bool quitting_ = false;
  IMPLEMENT_REFCOUNTING(Client);
};

class Browser : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit Browser(std::filesystem::path root)
      : root_(std::move(root)), profiles_(root_), client_(new Client) {}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  // Renderer helpers register the same scheme whatever the browser mode.
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
    RegisterSuiteCutInitScheme(registrar);
  }
  // The registry is read with CEF's JSON parser, usable only from here on.
  void OnContextInitialized() override {
    profiles_.Load();
    Open("");
  }
  // Tabs and windows Chrome opens itself (Cmd+T, links, popups) use this.
  CefRefPtr<CefClient> GetDefaultClient() override { return client_; }

  void Open(const std::string& requested) {
    const auto dir = requested.empty() ? profiles_.last() : requested;
    if (!profiles_.Find(dir)) return;
    profiles_.SetLast(dir);
    profiles_.Save();
    CefWindowInfo window;
    window.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    CefBrowserHost::CreateBrowser(window, client_, "chrome://newtab/", CefBrowserSettings(),
                                  nullptr, Context(dir));
  }
  void Add(const std::string& name) {
    if (const auto profile = profiles_.Add(name)) Open(profile->dir);
  }
  void Rename(const std::string& dir, const std::string& name) {
    if (profiles_.Rename(dir, name)) profiles_.Save();
  }
  std::string DirOf(CefRefPtr<CefBrowser> browser) const {
    const auto path = std::filesystem::path(
        browser->GetHost()->GetRequestContext()->GetCachePath().ToString());
    return path.filename().string();
  }
  BrowserProfiles& profiles() { return profiles_; }
  CefRefPtr<Client> client() { return client_; }

 private:
  // Every profile, "Default" too, is named by its directory. The global
  // context would follow Chromium's own last-used profile instead.
  CefRefPtr<CefRequestContext> Context(const std::string& dir) {
    auto& context = contexts_[dir];
    if (!context) {
      CefRequestContextSettings settings;
      CefString(&settings.cache_path) = profiles_.PathFor(dir).string();
      settings.persist_session_cookies = true;
      context = CefRequestContext::CreateContext(settings, nullptr);
    }
    return context;
  }

  std::filesystem::path root_;
  BrowserProfiles profiles_;
  CefRefPtr<Client> client_;
  std::map<std::string, CefRefPtr<CefRequestContext>> contexts_;
  IMPLEMENT_REFCOUNTING(Browser);
};

CefRefPtr<Browser>& Current() {
  static CefRefPtr<Browser> browser;
  return browser;
}

// Chrome's own switch picks another set of profiles, e.g. to try things out.
std::filesystem::path Root(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strncmp(argv[i], "--user-data-dir=", 16) == 0) {
      const std::filesystem::path path = argv[i] + 16;
      if (path.is_absolute()) return path.lexically_normal();
    }
  return DefaultRoot();
}

}  // namespace

bool Requested(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strncmp(argv[i], "--suitecut-profile", 18) == 0 ||
        std::strncmp(argv[i], "--type=", 7) == 0)
      return false;
  return true;
}

int Run(const CefMainArgs& args, int argc, char** argv) {
  const auto root = Root(argc, argv);
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) return 1;
  // An SDK launch pointed at this same directory would corrupt it; refuse.
  SuiteCutProfileLock lock;
  std::string lock_error;
  if (!lock.Acquire(root.string(), lock_error)) {
    std::fprintf(stderr, "SuiteCut: %s\n", lock_error.c_str());
    return 1;
  }
  CefSettings settings;
  CefString(&settings.root_cache_path) = root.string();
  CefString(&settings.cache_path) = root.string();
  settings.persist_session_cookies = true;
  settings.log_severity = LOGSEVERITY_DISABLE;
  Current() = new Browser(root);
  if (!CefInitialize(args, settings, Current(), nullptr)) return 1;
  CefRunMessageLoop();
  Current() = nullptr;
  CefShutdown();
  return 0;
}

std::vector<MenuProfile> Profiles() {
  std::vector<MenuProfile> out;
  if (!Current()) return out;
  for (const auto& profile : Current()->profiles().profiles())
    out.push_back({profile.dir, profile.name});
  return out;
}

std::vector<std::pair<void*, std::string>> Windows() {
  std::vector<std::pair<void*, std::string>> out;
  if (!Current()) return out;
  for (const auto& browser : Current()->client()->browsers())
    out.emplace_back(browser->GetHost()->GetWindowHandle(), Current()->DirOf(browser));
  return out;
}

void Open(const std::string& dir) {
  if (Current()) Current()->Open(dir);
}
void Add(const std::string& name) {
  if (Current()) Current()->Add(name);
}
void Rename(const std::string& dir, const std::string& name) {
  if (Current()) Current()->Rename(dir, name);
}
void Reopen() {
  if (Current() && Current()->client()->browsers().empty()) Current()->Open("");
}
void Quit() {
  if (Current()) Current()->client()->Quit();
}

}  // namespace suitecut::browser::standalone
