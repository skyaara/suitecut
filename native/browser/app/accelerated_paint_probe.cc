// SPDX-License-Identifier: MIT
// Linux accelerated-paint probe: one off-screen browser, with or without
// shared textures, counting OnPaint and OnAcceleratedPaint and describing
// each accelerated frame's planes. No SuiteCut code.
//
// Built with -DSUITECUT_ACCELERATED_PAINT_PROBE=ON (Linux) as
// suitecut-accelerated-paint-probe, beside suitecut-browser.
//
// usage: suitecut-accelerated-paint-probe --probe-profile=/abs/dir [--probe-shared=1]
// [--probe-seconds=N]
//        [--probe-url=URL] [Chromium switches...]
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_render_handler.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/base/cef_callback.h"

namespace {
std::atomic<int> paints{0}, accelerated{0}, described{0};

std::string fd_target(int fd) {
  char link[64], target[256] = {};
  std::snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
  const auto n = ::readlink(link, target, sizeof target - 1);
  return n > 0 ? std::string(target, n) : std::string("(unreadable)");
}

class Handler : public CefClient,
                public CefRenderHandler,
                public CefLifeSpanHandler,
                public CefLoadHandler {
 public:
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
    rect = CefRect(0, 0, 800, 600);
  }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void*, int width,
               int height) override {
    if (paints++ == 0)
      std::fprintf(stderr, "PROBE first OnPaint %dx%d (CPU buffer)\n", width, height);
  }
  void OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&,
                          const CefAcceleratedPaintInfo& info) override {
    ++accelerated;
    if (described++ >= 3) return;
    std::fprintf(stderr,
                 "PROBE OnAcceleratedPaint type=%d planes=%d modifier=0x%llx format=%d coded=%dx%d "
                 "visible=%dx%d\n",
                 type, info.plane_count, static_cast<unsigned long long>(info.modifier),
                 info.format, info.extra.coded_size.width, info.extra.coded_size.height,
                 info.extra.visible_rect.width, info.extra.visible_rect.height);
    for (int i = 0; i < info.plane_count; ++i) {
      const auto& plane = info.planes[i];
      struct stat st{};
      const bool valid = ::fstat(plane.fd, &st) == 0;
      std::fprintf(stderr,
                   "PROBE   plane %d fd=%d stride=%u offset=%llu size=%llu valid=%d target=%s\n", i,
                   plane.fd, plane.stride, static_cast<unsigned long long>(plane.offset),
                   static_cast<unsigned long long>(plane.size), valid, fd_target(plane.fd).c_str());
    }
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { browser_ = browser; }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override { CefQuitMessageLoop(); }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override {
    if (frame->IsMain()) std::fprintf(stderr, "PROBE load end status=%d\n", status);
  }
  void Close() {
    if (browser_)
      browser_->GetHost()->CloseBrowser(true);
    else
      CefQuitMessageLoop();
  }

 private:
  CefRefPtr<CefBrowser> browser_;
  IMPLEMENT_REFCOUNTING(Handler);
};
}  // namespace

int main(int argc, char** argv) {
  CefMainArgs args(argc, argv);
  const int code = CefExecuteProcess(args, nullptr, nullptr);
  if (code >= 0) return code;
  auto command = CefCommandLine::CreateCommandLine();
  command->InitFromArgv(argc, argv);
  const auto profile = command->GetSwitchValue("probe-profile").ToString();
  if (profile.empty()) {
    std::fprintf(stderr, "Pass --probe-profile=/absolute/path\n");
    return 2;
  }
  const bool shared = command->GetSwitchValue("probe-shared") == "1";
  const auto seconds_value = command->GetSwitchValue("probe-seconds").ToString();
  const int seconds = seconds_value.empty() ? 6 : std::stoi(seconds_value);
  auto url = command->GetSwitchValue("probe-url").ToString();
  if (url.empty())
    url =
        "data:text/html,<style>@keyframes "
        "s{to{transform:rotate(360deg)}}div{width:200px;height:200px;"
        "background:linear-gradient(red,blue);animation:s 1s linear infinite}</style><div></div>";

  CefSettings settings;
  CefString(&settings.root_cache_path) = profile;
  settings.windowless_rendering_enabled = true;
  if (!CefInitialize(args, settings, nullptr, nullptr)) return 1;

  CefRefPtr<Handler> handler = new Handler();
  CefWindowInfo window;
  window.SetAsWindowless(0);
  window.shared_texture_enabled = shared;
  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 30;
  std::fprintf(stderr, "PROBE shared_texture_enabled=%d\n", shared);
  CefBrowserHost::CreateBrowser(window, handler, url, browser_settings, nullptr, nullptr);
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Handler> h) { h->Close(); }, handler),
                     seconds * 1000);
  CefRunMessageLoop();
  std::fprintf(stderr, "PROBE result shared=%d OnPaint=%d OnAcceleratedPaint=%d in %ds\n", shared,
               paints.load(), accelerated.load(), seconds);
  handler = nullptr;
  CefShutdown();
  return 0;
}
