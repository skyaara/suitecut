// SPDX-License-Identifier: MIT
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "include/cef_app.h"
#include "include/cef_audio_handler.h"
#include "include/cef_client.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_version.h"
#include "audio_clock.h"
#include "libyuv/convert_from_argb.h"
#include "libyuv/convert.h"

namespace {
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
double Now() { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); }
using Dict = CefRefPtr<CefDictionaryValue>;
Dict Header(const char* type) {
  auto value = CefDictionaryValue::Create();
  value->SetInt("version", 1);
  value->SetString("type", type);
  return value;
}
std::string Json(Dict dict) {
  auto value = CefValue::Create();
  value->SetDictionary(dict);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}
struct Packet { std::string header; std::vector<uint8_t> data; bool video = false, audio = false; double queued = 0; };

// Never block a CEF callback on Node or an encoder. Retain one pending video
// frame and at most 100 audio packets. Control responses have a separate cap.
class Writer {
 public:
  explicit Writer(int output, bool diagnostics = false) : output_(output), diagnostics_(diagnostics) {
    fcntl(output_, F_SETFL, fcntl(output_, F_GETFL) | O_NONBLOCK);
    thread_ = std::thread([this] { Run(); });
    if (diagnostics_) monitor_ = std::thread([this] {
      std::unique_lock lock(monitor_mutex_);
      while (!monitor_cv_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_.load(); })) Report();
      Report();
    });
  }
  ~Writer() { Stop(); close(output_); }
  void Stop() {
    stopping_ = true;
    cv_.notify_all();
    monitor_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (monitor_.joinable()) monitor_.join();
  }
  // Opt-in benchmark counters only: no page content, URL, PCM or pixels.
  void Captured(bool video, double at, double pts = 0) {
    if (!diagnostics_) return;
    std::lock_guard lock(mutex_);
    auto& count = video ? paints_ : audio_callbacks_;
    auto& last = video ? last_paint_ : last_audio_;
    auto& gap = video ? paint_gap_ : audio_gap_;
    if (count) gap = std::max(gap, at - last);
    ++count; last = at;
    if (!video) last_audio_pts_ = pts;
  }
  void Send(Dict header, std::vector<uint8_t> data = {}) {
    header->SetInt("bytes", static_cast<int>(data.size()));
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    const auto type = header->GetString("type").ToString();
    if (type == "frame") {
      if (frame_) ++dropped_;
      header->SetDouble("dropped", static_cast<double>(dropped_));
      frame_ = Packet{Json(header), std::move(data), true, false, diagnostics_ ? Now() : 0};
    } else if (type == "audio") {
      if (audio_.size() >= 100) { audio_.pop_front(); ++audio_dropped_; }
      audio_.push_back({Json(header), std::move(data), false, true, diagnostics_ ? Now() : 0});
      audio_peak_ = std::max(audio_peak_, audio_.size());
    } else {
      if (control_.size() >= 128) std::_Exit(2);
      control_.push_back({Json(header), std::move(data)});
    }
    cv_.notify_one();
  }
  void Reply(int id, bool ok, const std::string& error = "", std::vector<uint8_t> data = {}) {
    auto header = Header("response");
    header->SetInt("id", id);
    header->SetBool("ok", ok);
    if (!error.empty()) header->SetString("error", error.substr(0, 4096));
    Send(header, std::move(data));
  }
  void Error(const char* message) {
    auto header = Header("error");
    header->SetString("error", message);
    Send(header);
  }
 private:
  void Report() {
    char line[4096];
    int length;
    {
    std::lock_guard lock(mutex_);
    const double now = Now();
    length = snprintf(line, sizeof(line), "SUITECUT_DIAGNOSTIC {\"hostMs\":%.3f,\"paints\":%llu,\"audioCallbacks\":%llu,\"lastPaintMs\":%.3f,\"lastAudioMs\":%.3f,\"lastAudioPtsMs\":%.3f,\"maxPaintGapMs\":%.3f,\"maxAudioGapMs\":%.3f,\"videoReplaced\":%llu,\"audioDropped\":%llu,\"audioQueued\":%zu,\"audioQueuePeak\":%zu,\"videoWritten\":%llu,\"audioWritten\":%llu,\"writingVideo\":%s,\"writingAudio\":%s,\"currentWriteMs\":%.3f,\"maxVideoWriteMs\":%.3f,\"maxAudioWriteMs\":%.3f,\"maxVideoQueueMs\":%.3f,\"maxAudioQueueMs\":%.3f}\n",
      now, (unsigned long long)paints_, (unsigned long long)audio_callbacks_, last_paint_, last_audio_, last_audio_pts_, paint_gap_, audio_gap_,
      (unsigned long long)dropped_, (unsigned long long)audio_dropped_, audio_.size(), audio_peak_,
      (unsigned long long)video_written_, (unsigned long long)audio_written_, writing_video_ ? "true" : "false", writing_audio_ ? "true" : "false",
      writing_at_ ? now - writing_at_ : 0, video_write_max_, audio_write_max_, video_queue_max_, audio_queue_max_);
    }
    // Do not hold the media queue lock while writing diagnostics.
    if (length > 0 && static_cast<size_t>(length) < sizeof(line)) {
      const auto ignored = write(STDERR_FILENO, line, static_cast<size_t>(length));
      (void)ignored;
    }
  }
  bool Write(const void* data, size_t size) {
    auto bytes = static_cast<const uint8_t*>(data);
    while (size && !stopping_) {
      pollfd fd{output_, POLLOUT, 0};
      if (poll(&fd, 1, 100) <= 0) continue;
      const auto n = write(output_, bytes, size);
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      if (n <= 0) return false;
      bytes += n;
      size -= static_cast<size_t>(n);
    }
    return size == 0;
  }
  void Run() {
    while (!stopping_) {
      Packet packet;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !control_.empty() || !audio_.empty() || frame_; });
        if (stopping_) break;
        if (!control_.empty()) { packet = std::move(control_.front()); control_.pop_front(); }
        else if (!audio_.empty()) { packet = std::move(audio_.front()); audio_.pop_front(); }
        else { packet = std::move(*frame_); frame_.reset(); }
        if (diagnostics_) {
          writing_at_ = Now(); writing_video_ = packet.video; writing_audio_ = packet.audio;
          if (packet.video) video_queue_max_ = std::max(video_queue_max_, writing_at_ - packet.queued);
          if (packet.audio) audio_queue_max_ = std::max(audio_queue_max_, writing_at_ - packet.queued);
        }
      }
      const uint32_t size = static_cast<uint32_t>(packet.header.size());
      const uint8_t prefix[] = {uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16), uint8_t(size >> 24)};
      const bool ok = Write(prefix, 4) && Write(packet.header.data(), size) && Write(packet.data.data(), packet.data.size());
      if (diagnostics_) {
        std::lock_guard lock(mutex_);
        const double elapsed = Now() - writing_at_;
        if (packet.video) { video_write_max_ = std::max(video_write_max_, elapsed); if (ok) ++video_written_; }
        if (packet.audio) { audio_write_max_ = std::max(audio_write_max_, elapsed); if (ok) ++audio_written_; }
        writing_at_ = 0; writing_video_ = writing_audio_ = false;
      }
      if (!ok) break;
    }
  }
  std::mutex mutex_;
  int output_;
  std::condition_variable cv_;
  std::deque<Packet> control_, audio_;
  std::optional<Packet> frame_;
  uint64_t dropped_ = 0;
  const bool diagnostics_;
  uint64_t audio_dropped_ = 0, paints_ = 0, audio_callbacks_ = 0, video_written_ = 0, audio_written_ = 0;
  size_t audio_peak_ = 0;
  double last_paint_ = 0, last_audio_ = 0, last_audio_pts_ = 0, paint_gap_ = 0, audio_gap_ = 0;
  double writing_at_ = 0, video_write_max_ = 0, audio_write_max_ = 0, video_queue_max_ = 0, audio_queue_max_ = 0;
  bool writing_video_ = false, writing_audio_ = false;
  std::mutex monitor_mutex_;
  std::condition_variable monitor_cv_;
  std::atomic<bool> stopping_{false};
  std::thread thread_;
  std::thread monitor_;
};

class Task : public CefTask {
 public:
  explicit Task(std::function<void()> callback) : callback_(std::move(callback)) {}
  void Execute() override { callback_(); }
 private:
  std::function<void()> callback_;
  IMPLEMENT_REFCOUNTING(Task);
};

class Source : public CefClient, public CefLifeSpanHandler, public CefRenderHandler,
               public CefAudioHandler, public CefLoadHandler, public CefRequestHandler,
               public CefDevToolsMessageObserver, public CefJSDialogHandler,
               public CefDownloadHandler, public CefContextMenuHandler {
 public:
  Source(Writer& writer, int width, int height, double scale, bool i420, bool subscriptions)
      : writer_(writer), width_(width), height_(height), scale_(scale), i420_(i420),
        frame_subscriptions_(subscriptions) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override { return false; }
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType, const CefString&,
                  const CefString&, CefRefPtr<CefJSDialogCallback>, bool& suppress) override {
    suppress = true;
    return false;
  }
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool,
                            CefRefPtr<CefJSDialogCallback> callback) override {
    callback->Continue(true, CefString());
    return true;
  }
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                           CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) override {
    model->Clear();
  }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, width_, height_); }
  bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override {
    info.device_scale_factor = static_cast<float>(scale_);
    info.rect = info.available_rect = CefRect(0, 0, width_, height_);
    return true;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
    observer_ = browser->GetHost()->AddDevToolsMessageObserver(this);
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    stopped_ = true;
    if (reader_.joinable()) reader_.join();
    observer_ = nullptr;
    browser_ = nullptr;
    CefQuitMessageLoop();
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&,
                     const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures&,
                     CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
                     CefRefPtr<CefDictionaryValue>&, bool*) override { return true; }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&,
               const void* buffer, int width, int height) override {
    const double captured = Now();
    writer_.Captured(true, captured);
    if (width <= 0 || height <= 0 || width > 3840 || height > 2160) return;
    const auto bytes = static_cast<const uint8_t*>(buffer);
    if (type == PET_VIEW) {
      frame_width_ = width;
      frame_height_ = height;
      view_.assign(bytes, bytes + static_cast<size_t>(width) * height * 4);
    } else {
      popup_width_ = width;
      popup_height_ = height;
      popup_.assign(bytes, bytes + static_cast<size_t>(width) * height * 4);
    }
    EmitFrame(captured);
  }
  void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override {
    show_popup_ = show;
    if (!show) { popup_.clear(); EmitFrame(); }
  }
  void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override { popup_rect_ = rect; }
  bool GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters& params) override {
    params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
    params.sample_rate = 48000;
    params.frames_per_buffer = 960;
    return true;
  }
  void OnAudioStreamStarted(CefRefPtr<CefBrowser>, const CefAudioParameters& params, int channels) override {
    audio_clock_.Reset();
    audio_valid_ = params.sample_rate == 48000 && channels == 2;
    if (!audio_valid_) writer_.Error("Unsupported native audio format");
  }
  void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** data, int frames, int64_t pts) override {
    if (!audio_valid_ || frames < 1 || frames > 4096) return;
    const double received = Now();
    writer_.Captured(false, received, static_cast<double>(pts));
    const double timestamp = audio_clock_.Map(static_cast<double>(pts), received);
    std::vector<uint8_t> pcm(static_cast<size_t>(frames) * 4);
    for (int frame = 0; frame < frames; ++frame) {
      for (int channel = 0; channel < 2; ++channel) {
        const float value = std::isfinite(data[channel][frame]) ? data[channel][frame] : 0;
        const int sample = static_cast<int>(std::clamp(value, -1.0f, 1.0f) * 32767);
        const auto offset = static_cast<size_t>(frame * 2 + channel) * 2;
        pcm[offset] = static_cast<uint8_t>(sample & 255);
        pcm[offset + 1] = static_cast<uint8_t>((sample >> 8) & 255);
      }
    }
    auto header = Header("audio");
    header->SetDouble("timestampMs", timestamp);
    header->SetInt("frames", frames);
    writer_.Send(header, std::move(pcm));
  }
  void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override { audio_valid_ = false; }
  void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString&) override { writer_.Error("Native audio capture failed"); }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override {
    if (frame->IsMain() && !ready_) {
      // Handshake only after the initial blank document loads, so its load-end
      // cannot accidentally resolve the operator's first navigation command.
      ready_ = true;
      auto header = Header("ready");
      header->SetDouble("timestampMs", Now());
      header->SetString("cefVersion", CEF_VERSION);
      if (frame_subscriptions_) header->SetBool("frameSubscriptions", true);
      writer_.Send(header);
      reader_ = std::thread([this] { ReadCommands(); });
      EmitFrame();
      return;
    }
    if (frame->IsMain() && navigation_) {
      writer_.Reply(navigation_, status < 400, status < 400 ? "" : "HTTP navigation failed");
      navigation_ = 0;
    }
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                   const CefString&, const CefString&) override {
    if (frame->IsMain() && navigation_ && code != ERR_ABORTED) {
      writer_.Reply(navigation_, false, "Native navigation failed");
      navigation_ = 0;
    }
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus, int, const CefString&) override {
    writer_.Error("Browser renderer terminated");
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success, const void* result, size_t size) override {
    if (size > 1048576) { writer_.Reply(id, false, "DevTools response exceeds 1 MiB"); return; }
    const auto bytes = static_cast<const uint8_t*>(result);
    writer_.Reply(id, success, success ? "" : "DevTools command failed", size ? std::vector<uint8_t>(bytes, bytes + size) : std::vector<uint8_t>{});
  }
 private:
  void EmitFrame(double timestamp = Now()) {
    if (!ready_ || !emit_frames_ || view_.empty()) return;
    std::vector<uint8_t> output;
    const uint8_t* pixels = view_.data();
    if (show_popup_ && !popup_.empty()) {
      output = view_;
      const int left = static_cast<int>(std::round(popup_rect_.x * scale_));
      const int top = static_cast<int>(std::round(popup_rect_.y * scale_));
      for (int y = 0; y < popup_height_; ++y) {
        if (top + y < 0 || top + y >= frame_height_) continue;
        const int start = std::max(0, -left);
        const int end = std::min(popup_width_, frame_width_ - left);
        if (end <= start) continue;
        std::memcpy(output.data() + ((top + y) * frame_width_ + left + start) * 4,
                    popup_.data() + (y * popup_width_ + start) * 4, static_cast<size_t>(end - start) * 4);
      }
      pixels = output.data();
    }
    auto header = Header("frame");
    header->SetString("pixelFormat", i420_ ? "i420" : "bgra");
    if (i420_) {
      const size_t y_size = static_cast<size_t>(frame_width_) * frame_height_;
      std::vector<uint8_t> planes(y_size * 3 / 2);
      // libyuv's ARGB name describes registers; in little-endian memory its
      // input is BGRA, matching CEF. Use explicit limited-range BT.709.
      if (libyuv::ARGBToI420Matrix(pixels, frame_width_ * 4,
          planes.data(), frame_width_, planes.data() + y_size, frame_width_ / 2,
          planes.data() + y_size * 5 / 4, frame_width_ / 2,
          &libyuv::kArgbH709Constants, frame_width_, frame_height_) != 0) {
        writer_.Error("Native pixel conversion failed");
        return;
      }
      output = std::move(planes);
    } else if (output.empty()) output = view_;
    header->SetDouble("timestampMs", timestamp);
    header->SetInt("width", frame_width_);
    header->SetInt("height", frame_height_);
    writer_.Send(header, std::move(output));
  }
  void Command(const std::string& line) {
    if (!browser_) return;
    auto value = CefParseJSON(line, JSON_PARSER_RFC);
    auto message = value ? value->GetDictionary() : nullptr;
    if (!message || message->GetInt("version") != 1 || message->GetInt("id") < 1) {
      writer_.Error("Invalid native command");
      return;
    }
    const int id = message->GetInt("id");
    const auto command = message->GetString("command").ToString();
    if (command == "frames") {
      if (message->GetType("enabled") != VTYPE_BOOL) {
        writer_.Reply(id, false, "Frame subscription requires a boolean"); return;
      }
      emit_frames_ = message->GetBool("enabled");
      writer_.Reply(id, true);
      if (emit_frames_) EmitFrame();
    } else if (command == "navigate") {
      const auto url = message->GetString("url").ToString();
      if (!url.starts_with("https://") && !url.starts_with("http://")) {
        writer_.Reply(id, false, "Navigation requires HTTP(S)"); return;
      }
      if (navigation_) { writer_.Reply(id, false, "Navigation already in progress"); return; }
      navigation_ = id;
      browser_->GetMainFrame()->LoadURL(url);
    } else if (command == "devtools") {
      if (!browser_->GetHost()->ExecuteDevToolsMethod(id, message->GetString("method"), message->GetDictionary("params")))
        writer_.Reply(id, false, "Could not dispatch DevTools command");
    } else writer_.Reply(id, false, "Unknown native command");
  }
  void ReadCommands() {
    std::string pending;
    char buffer[4096];
    while (!stopped_) {
      pollfd fd{STDIN_FILENO, POLLIN, 0};
      if (poll(&fd, 1, 100) <= 0) continue;
      const auto size = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (size < 0 && errno == EINTR) continue;
      if (size <= 0) break;
      for (ssize_t i = 0; i < size; ++i) {
        if (buffer[i] == '\n') {
          // Bound both the input bytes and outstanding UI work under a flooding client.
          if (queued_commands_.fetch_add(1) >= 64) { stopped_ = true; break; }
          CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), line = std::move(pending)] {
            self->queued_commands_--;
            self->Command(line);
          }));
          pending.clear();
        } else {
          pending += buffer[i];
          if (pending.size() >= 65536) { stopped_ = true; break; }
        }
      }
    }
    CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
      if (self->browser_) self->browser_->GetHost()->CloseBrowser(true);
    }));
  }
  Writer& writer_;
  int width_, height_;
  double scale_;
  bool i420_;
  bool frame_subscriptions_;
  int frame_width_ = 0, frame_height_ = 0, popup_width_ = 0, popup_height_ = 0, navigation_ = 0;
  bool show_popup_ = false, ready_ = false, emit_frames_ = true;
  CefRect popup_rect_;
  std::vector<uint8_t> view_, popup_;
  std::atomic<bool> stopped_{false}, audio_valid_{false};
  SuiteCutAudioClock audio_clock_;
  std::atomic<int> queued_commands_{0};
  std::thread reader_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> observer_;
  IMPLEMENT_REFCOUNTING(Source);
};

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit App(Writer& writer) : writer_(writer) {}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command) override {
    // Each source uses a fresh profile. Chrome's first-run UI can block
    // initialization before the windowless browser is created on Linux.
    command->AppendSwitch("no-first-run");
    command->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    command->AppendSwitch("disable-background-timer-throttling");
    command->AppendSwitch("disable-renderer-backgrounding");
  }
  void OnContextInitialized() override {
    auto command = CefCommandLine::GetGlobalCommandLine();
    const int width = std::atoi(command->GetSwitchValue("suitecut-width").ToString().c_str());
    const int height = std::atoi(command->GetSwitchValue("suitecut-height").ToString().c_str());
    const int fps = std::atoi(command->GetSwitchValue("suitecut-fps").ToString().c_str());
    const double scale = std::atof(command->GetSwitchValue("suitecut-scale").ToString().c_str());
    const auto pixel_format = command->GetSwitchValue("suitecut-pixel-format").ToString();
    if (width < 2 || height < 2 || (scale != 1 && scale != 2) ||
        width * scale > 3840 || height * scale > 2160 || (fps != 30 && fps != 60)) {
      CefQuitMessageLoop(); return;
    }
    if ((pixel_format != "bgra" && pixel_format != "i420") ||
        (pixel_format == "i420" && ((width % 2) || (height % 2)))) {
      CefQuitMessageLoop(); return;
    }
    CefWindowInfo window;
    window.SetAsWindowless(0);
    CefBrowserSettings settings;
    settings.windowless_frame_rate = fps;
    settings.background_color = CefColorSetARGB(255, 0, 0, 0);
    source_ = new Source(writer_, width, height, scale, pixel_format == "i420",
                         command->HasSwitch("suitecut-frame-subscriptions"));
    if (!CefBrowserHost::CreateBrowser(window, source_, "about:blank", settings, nullptr, nullptr)) CefQuitMessageLoop();
  }
  void ReleaseSource() { source_ = nullptr; }
 private:
  Writer& writer_;
  CefRefPtr<Source> source_;
  IMPLEMENT_REFCOUNTING(App);
};
}  // namespace

int RunSuiteCutBrowser(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  CefMainArgs args(argc, argv);
#if !defined(__APPLE__)
  const int code = CefExecuteProcess(args, nullptr, nullptr);
  if (code >= 0) return code;
#endif
  // CEF and subprocess libraries may print diagnostics to stdout. Reserve a
  // close-on-exec descriptor for media, then redirect ordinary stdout to stderr.
  const int output = dup(STDOUT_FILENO);
  if (output < 0 || fcntl(output, F_SETFD, FD_CLOEXEC) < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) return 1;
  auto command = CefCommandLine::CreateCommandLine();
  command->InitFromArgv(argc, argv);
  Writer writer(output, command->HasSwitch("suitecut-diagnostics"));
  CefRefPtr<App> app = new App(writer);
  CefSettings settings;
  const auto profile = command->GetSwitchValue("suitecut-profile").ToString();
  if (profile.empty() || profile.front() != '/') return 1;
  CefString(&settings.root_cache_path) = profile;
  settings.windowless_rendering_enabled = true;
  settings.log_severity = LOGSEVERITY_DISABLE;
  settings.persist_session_cookies = false;
  // An isolated temporary root; the cache path stays empty (incognito). No
  // remote debugging listener, certificate bypass, or sandbox bypass.
  if (!CefInitialize(args, settings, app, nullptr)) return 1;
  CefRunMessageLoop();
  app->ReleaseSource();
  app = nullptr;
  CefShutdown();
  writer.Stop();
  return 0;
}
