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
#include "memory_bounds.h"
#include <map>
#include <set>
#include "media_engine.h"
#include "media_file.h"
#include "media_render.h"
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
#include "include/cef_request_context.h"
#include "include/cef_cookie.h"
#include "include/cef_permission_handler.h"
#include "include/cef_task.h"
#include "include/cef_version.h"
#include "audio_clock.h"
#include "audio_trace.h"
#include "libyuv/convert_from_argb.h"
#include "libyuv/convert.h"

namespace {
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
double EpochNow() {return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();}
double Now() { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); }
using Dict = CefRefPtr<CefDictionaryValue>;
double Number(Dict value, const char* key) {
  if(value->GetType(key)==VTYPE_INT) return value->GetInt(key);
  return value->GetDouble(key);
}
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
      auto text=Json(header);
      if(control_.size()>=4096 || control_bytes_+text.size()+data.size()>16777216) {
        control_.clear();control_bytes_=0;auto error=Header("error");error->SetInt("bytes",0);error->SetString("error","Native control backpressure limit exceeded");text=Json(error);data.clear();
      }
      control_bytes_+=text.size()+data.size();control_.push_back({std::move(text),std::move(data)});
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
        if (!control_.empty()) { packet = std::move(control_.front()); control_bytes_-=packet.header.size()+packet.data.size();control_.pop_front(); }
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
  size_t control_bytes_=0;
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

class CookieVisitor : public CefCookieVisitor {
 public:
  explicit CookieVisitor(std::function<void(CefRefPtr<CefListValue>)> result):result_(std::move(result)),cookies_(CefListValue::Create()) {}
  ~CookieVisitor() override {result_(cookies_);}
  bool Visit(const CefCookie& cookie,int,int,bool&) override {
    auto value=CefDictionaryValue::Create();value->SetString("name",CefString(&cookie.name));value->SetString("value",CefString(&cookie.value));value->SetString("domain",CefString(&cookie.domain));value->SetString("path",CefString(&cookie.path));
    value->SetBool("secure",cookie.secure);value->SetBool("httpOnly",cookie.httponly);value->SetBool("session",!cookie.has_expires);
    value->SetDouble("expires",cookie.has_expires ? (cookie.expires.val-11644473600000000LL)/1000000.0 : -1);
    value->SetString("sameSite",cookie.same_site==CEF_COOKIE_SAME_SITE_STRICT_MODE ? "Strict" : cookie.same_site==CEF_COOKIE_SAME_SITE_NO_RESTRICTION ? "None" : "Lax");
    value->SetInt("size",CefString(&cookie.name).length()+CefString(&cookie.value).length());value->SetString("priority","Medium");value->SetBool("sameParty",false);value->SetString("sourceScheme",cookie.secure ? "Secure" : "NonSecure");value->SetInt("sourcePort",-1);
    cookies_->SetDictionary(cookies_->GetSize(),value);return true;
  }
 private:
  std::function<void(CefRefPtr<CefListValue>)> result_;CefRefPtr<CefListValue> cookies_;
  IMPLEMENT_REFCOUNTING(CookieVisitor);
};
class CookieSetCallback : public CefSetCookieCallback {
 public: explicit CookieSetCallback(std::function<void(bool)> result):result_(std::move(result)) {}
  void OnComplete(bool success) override {result_(success);}
 private: std::function<void(bool)> result_;IMPLEMENT_REFCOUNTING(CookieSetCallback);
};
class CookieDeleteCallback : public CefDeleteCookiesCallback {
 public: explicit CookieDeleteCallback(std::function<void()> result):result_(std::move(result)) {}
  void OnComplete(int) override {result_();}
 private: std::function<void()> result_;IMPLEMENT_REFCOUNTING(CookieDeleteCallback);
};

class Source : public CefClient, public CefLifeSpanHandler, public CefRenderHandler,
               public CefAudioHandler, public CefLoadHandler, public CefRequestHandler,
               public CefDevToolsMessageObserver, public CefJSDialogHandler,
               public CefDownloadHandler, public CefContextMenuHandler, public CefPermissionHandler {
 public:
  Source(Writer& writer, int width, int height, double scale, bool i420, bool subscriptions, Source* root = nullptr, std::string context = "default", std::string opener = "")
      : root_owner_(root), root_(root ? root : this), context_id_(std::move(context)), opener_id_(std::move(opener)),
        writer_(writer), width_(width), height_(height), scale_(scale), i420_(i420), frame_subscriptions_(subscriptions) {if(root) {emit_frames_=false;emit_audio_=false;}}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {return this;}
  static uint32_t PermissionBit(const std::string& name) {
    const std::map<std::string,uint32_t> bits={
      {"geolocation",CEF_PERMISSION_TYPE_GEOLOCATION},{"notifications",CEF_PERMISSION_TYPE_NOTIFICATIONS},
      {"audioCapture",CEF_PERMISSION_TYPE_MIC_STREAM},{"videoCapture",CEF_PERMISSION_TYPE_CAMERA_STREAM},
      {"midiSysex",CEF_PERMISSION_TYPE_MIDI_SYSEX},{"clipboardReadWrite",CEF_PERMISSION_TYPE_CLIPBOARD},
      {"clipboardSanitizedWrite",CEF_PERMISSION_TYPE_CLIPBOARD},{"sensors",CEF_PERMISSION_TYPE_SENSORS},
      {"idleDetection",CEF_PERMISSION_TYPE_IDLE_DETECTION},{"windowManagement",CEF_PERMISSION_TYPE_WINDOW_MANAGEMENT},
      {"localFonts",CEF_PERMISSION_TYPE_LOCAL_FONTS},{"cameraPanTiltZoom",CEF_PERMISSION_TYPE_CAMERA_PAN_TILT_ZOOM}
    };
    const auto found=bits.find(name);return found==bits.end() ? 0 : found->second;
  }
  uint32_t Granted(const std::string& origin) {
    uint32_t granted=0;auto found=root_->pending_permissions_.find(context_id_);
    if(found!=root_->pending_permissions_.end()) for(auto& policy:found->second) {
      const auto scope=policy->GetString("origin").ToString();if(!scope.empty() && scope!=origin) continue;
      auto list=policy->GetList("permissions");if(list) for(size_t i=0;i<list->GetSize();++i) granted|=PermissionBit(list->GetString(i).ToString());
    }
    return granted;
  }
  static std::string Origin(const std::string& url) {
    CefURLParts parts;if(!CefParseURL(url,parts)) return "";
    const auto scheme=CefString(&parts.scheme).ToString(),host=CefString(&parts.host).ToString(),port=CefString(&parts.port).ToString();
    if(host.empty() || (scheme!="http" && scheme!="https")) return "";
    return scheme+"://"+host+((port.empty() || (scheme=="http" && port=="80") || (scheme=="https" && port=="443")) ? "" : ":"+port);
  }
  void ApplyPermissions(const std::string& origin) {
    if(origin.empty() || !browser_) return;
    const std::map<uint32_t,cef_content_setting_types_t> types={
      {CEF_PERMISSION_TYPE_GEOLOCATION,CEF_CONTENT_SETTING_TYPE_GEOLOCATION},
      {CEF_PERMISSION_TYPE_NOTIFICATIONS,CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS},
      {CEF_PERMISSION_TYPE_MIC_STREAM,CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_MIC},
      {CEF_PERMISSION_TYPE_CAMERA_STREAM,CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA},
      {CEF_PERMISSION_TYPE_MIDI_SYSEX,CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX},
      {CEF_PERMISSION_TYPE_CLIPBOARD,CEF_CONTENT_SETTING_TYPE_CLIPBOARD_READ_WRITE},
      {CEF_PERMISSION_TYPE_SENSORS,CEF_CONTENT_SETTING_TYPE_SENSORS},
      {CEF_PERMISSION_TYPE_IDLE_DETECTION,CEF_CONTENT_SETTING_TYPE_IDLE_DETECTION},
      {CEF_PERMISSION_TYPE_WINDOW_MANAGEMENT,CEF_CONTENT_SETTING_TYPE_WINDOW_MANAGEMENT},
      {CEF_PERMISSION_TYPE_LOCAL_FONTS,CEF_CONTENT_SETTING_TYPE_LOCAL_FONTS},
      {CEF_PERMISSION_TYPE_CAMERA_PAN_TILT_ZOOM,CEF_CONTENT_SETTING_TYPE_CAMERA_PAN_TILT_ZOOM}
    };
    const auto granted=Granted(origin);bool overridden=false;auto found=root_->pending_permissions_.find(context_id_);
    if(found!=root_->pending_permissions_.end()) for(auto& policy:found->second) if(policy->GetString("origin").empty() || policy->GetString("origin").ToString()==origin) overridden=true;
    for(const auto& [bit,type]:types) browser_->GetHost()->GetRequestContext()->SetContentSetting(origin,origin,type,!overridden ? CEF_CONTENT_SETTING_VALUE_DEFAULT : (granted&bit) ? CEF_CONTENT_SETTING_VALUE_ALLOW : CEF_CONTENT_SETTING_VALUE_BLOCK);
  }
  void RefreshPermissions(const std::string& key) {
    for(auto& [target,source]:targets_) if(source->context_id_==key) for(auto& origin:source->permission_origins_) source->ApplyPermissions(origin);
  }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest> request,bool,bool) override {
    const auto origin=Origin(request->GetURL());if(!origin.empty()) {
      if(permission_origins_.size()<128 || permission_origins_.contains(origin)) permission_origins_.insert(origin);
      ApplyPermissions(origin);
    }
    return false;
  }
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>,uint64_t,const CefString& origin,uint32_t requested,CefRefPtr<CefPermissionPromptCallback> callback) override {
    callback->Continue(requested && !(requested & ~Granted(origin.ToString())) ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);return true;
  }
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString& origin,uint32_t requested,CefRefPtr<CefMediaAccessCallback> callback) override {
    const auto allowed=Granted(origin.ToString());uint32_t media=0;
    if(allowed&CEF_PERMISSION_TYPE_MIC_STREAM) media|=CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE;
    if(allowed&CEF_PERMISSION_TYPE_CAMERA_STREAM) media|=CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;
    callback->Continue(requested&media);return true;
  }

  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override {return root_->download_paths_.contains(context_id_);}
  bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,const CefString& name,CefRefPtr<CefBeforeDownloadCallback> callback) override {
    auto found=root_->download_paths_.find(context_id_);if(found==root_->download_paths_.end()) return false;
    const auto guid="cef-"+target_id_+"-"+std::to_string(item->GetId());download_ids_[item->GetId()]=guid;
    auto params=CefDictionaryValue::Create();params->SetString("guid",guid);params->SetString("frameId",target_id_);params->SetString("url",item->GetURL());params->SetString("suggestedFilename",name);
    root_->Event("Browser.downloadWillBegin",params);callback->Continue(found->second+"/"+guid,false);return true;
  }
  void OnDownloadUpdated(CefRefPtr<CefBrowser>,CefRefPtr<CefDownloadItem> item,CefRefPtr<CefDownloadItemCallback>) override {
    auto found=download_ids_.find(item->GetId());if(found==download_ids_.end()) return;
    auto params=CefDictionaryValue::Create();params->SetString("guid",found->second);params->SetDouble("totalBytes",item->GetTotalBytes());params->SetDouble("receivedBytes",item->GetReceivedBytes());
    params->SetString("state",item->IsComplete() ? "completed" : item->IsCanceled() ? "canceled" : "inProgress");
    if(item->IsComplete()) params->SetString("filePath",item->GetFullPath());
    root_->Event("Browser.downloadProgress",params);
    if(item->IsComplete() || item->IsCanceled()) download_ids_.erase(found);
  }
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString& origin, JSDialogType type, const CefString& text,
                  const CefString& prompt, CefRefPtr<CefJSDialogCallback> callback, bool& suppress) override {
    if(!root_->auto_attach_) {suppress=true;return false;}
    dialog_=callback;auto params=CefDictionaryValue::Create();params->SetString("url",origin);params->SetString("message",text);params->SetString("defaultPrompt",prompt);params->SetBool("hasBrowserHandler",true);
    params->SetString("type",type==JSDIALOGTYPE_ALERT ? "alert" : type==JSDIALOGTYPE_CONFIRM ? "confirm" : "prompt");
    auto event=CefDictionaryValue::Create();event->SetString("method","Page.javascriptDialogOpening");event->SetString("sessionId",Session());event->SetDictionary("params",params);root_->CDPMessage(event);return true;
  }
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool,
                            CefRefPtr<CefJSDialogCallback> callback) override {
    if(root_->auto_attach_) {
      dialog_=callback;auto params=CefDictionaryValue::Create();params->SetString("type","beforeunload");params->SetString("message","Leave this page?");params->SetString("url",browser_->GetMainFrame()->GetURL());params->SetBool("hasBrowserHandler",true);
      auto event=CefDictionaryValue::Create();event->SetString("method","Page.javascriptDialogOpening");event->SetString("sessionId",Session());event->SetDictionary("params",params);root_->CDPMessage(event);
    } else callback->Continue(true,CefString());
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
    std::erase_if(root_->pending_popups_,[this](const auto& entry){return entry.second.get()==this;});
    observer_ = browser->GetHost()->AddDevToolsMessageObserver(this);
    { std::lock_guard lock(audio_mutex_); target_id_ = "page-"+std::to_string(browser->GetIdentifier()); }
    root_->targets_[target_id_] = this;
    DispatchCDP(-1,"Target.getTargetInfo",CefDictionaryValue::Create(),"",true);
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    CefRefPtr<Source> keep_alive=this;
    stopped_ = true;
    if (reader_.joinable()) reader_.join();
    for (auto& [key,cancelled] : job_cancels_) *cancelled=true;
    decltype(outputs_) outputs;
    { std::lock_guard lock(outputs_mutex_); outputs.swap(outputs_); }
    // A pending Pause may own an output and wait for the encoder. Stop before
    // joining jobs; never destroy/join an encoder while holding outputs_mutex_.
    for (auto& [key,output] : outputs) output->Stop();
    for (auto& worker : media_workers_) if (worker.first.joinable()) worker.first.join();
    observer_ = nullptr;
    browser_ = nullptr;
    for(auto found=root_->pending_popups_.begin();found!=root_->pending_popups_.end();) {
      if(found->first.first==this) {
        auto pending=found->second;
        std::erase_if(root_->children_,[&](const auto& child){return child==pending;});
        found=root_->pending_popups_.erase(found);
      } else ++found;
    }
    root_->targets_.erase(target_id_);
    for(auto found=root_->sessions_.begin();found!=root_->sessions_.end();) {
      if(found->second==this) {root_->DetachEvent(found->first,root_->session_parents_[found->first]);root_->session_parents_.erase(found->first);found=root_->sessions_.erase(found);} else ++found;
    }
    view_.clear();view_.shrink_to_fit();popup_.clear();popup_.shrink_to_fit();
    if(this!=root_) std::erase_if(root_->children_,[this](const auto& child) {return child.get()==this;});
    if (root_->auto_attach_) {auto params=CefDictionaryValue::Create();params->SetString("sessionId",Session());params->SetString("targetId",target_id_);root_->Event("Target.detachedFromTarget",params);}
    if (root_->targets_.empty()) CefQuitMessageLoop();
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int popup_id, const CefString&,
                     const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures&,
                     CefWindowInfo& window, CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>&, bool*) override {
    if(root_->children_.size()>=64) return true;
    window.SetAsWindowless(0);settings.windowless_frame_rate=browser_->GetHost()->GetWindowlessFrameRate();
    CefRefPtr<Source> child=new Source(writer_,width_,height_,scale_,i420_,true,root_,context_id_,target_id_);
    root_->children_.push_back(child);root_->pending_popups_[{this,popup_id}]=child;client=child;return false;
  }
  void OnBeforePopupAborted(CefRefPtr<CefBrowser>,int popup_id) override {
    const auto found=root_->pending_popups_.find({this,popup_id});
    if(found==root_->pending_popups_.end()) return;
    auto pending=found->second;root_->pending_popups_.erase(found);
    std::erase_if(root_->children_,[&](const auto& child){return child==pending;});
  }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&,
               const void* buffer, int width, int height) override {
    const double captured = Now();
    writer_.Captured(true, captured);
    if (!buffer || width <= 0 || height <= 0 || width > 3840 || height > 2160) return;
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
    std::lock_guard lock(audio_mutex_);
    audio_clock_.Reset();
    audio_lifecycle_trace_.Open("audio-lifecycle-"+target_id_, "at_ms,event,frames_per_buffer,channels,sample_rate");
    audio_lifecycle_trace_.Row("%.6f,started,%d,%d,%d\n", Now(),params.frames_per_buffer,channels,params.sample_rate);
    audio_trace_.Open("cef-"+target_id_, "received_ms,pts_ms,mapped_ms,frames,rms,steady_ticks_ms,raw_mapped_ms", true);
    audio_valid_ = params.sample_rate == 48000 && channels == 2;
    {std::lock_guard lock(outputs_mutex_);for(auto& [key,output]:outputs_) output->AudioState(audio_valid_);}
    if (!audio_valid_) writer_.Error("Unsupported native audio format");
  }
  void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** data, int frames, int64_t pts) override {
    std::lock_guard lock(audio_mutex_);
    if (!audio_valid_ || !data || !data[0] || !data[1] || frames < 1 || frames > 4096) return;
    const auto tick = Clock::now();
    const double received = std::chrono::duration<double,std::milli>(tick-started).count();
    const double steady = std::chrono::duration<double,std::milli>(tick.time_since_epoch()).count();
    writer_.Captured(false, received, static_cast<double>(pts));
    const double raw_timestamp = audio_clock_.Map(static_cast<double>(pts), received, EpochNow(), steady);
    const double timestamp = audio_clock_.NormalizePacket(raw_timestamp,frames);
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
    auto samples = std::make_shared<std::vector<int16_t>>(static_cast<size_t>(frames)*2);
    std::memcpy(samples->data(), pcm.data(), pcm.size());
    if (audio_trace_.Enabled()) {
      double energy = 0;
      for (auto sample : *samples) energy += static_cast<double>(sample)*sample;
      audio_trace_.Row("%.6f,%lld,%.6f,%d,%.9f,%.6f,%.6f\n", received, static_cast<long long>(pts), timestamp, frames,
                       std::sqrt(energy/samples->size())/32768, steady, raw_timestamp);
      audio_trace_.PCM(samples->data(), samples->size()*sizeof(int16_t));
    }
    { std::lock_guard lock(outputs_mutex_); for (auto& [key, output] : outputs_) output->Audio(samples, timestamp); }
    if (!emit_audio_) return;
    auto header = Header("audio");
    header->SetDouble("timestampMs", timestamp);
    header->SetInt("frames", frames);
    writer_.Send(header, std::move(pcm));
  }
  void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override {
    std::lock_guard lock(audio_mutex_);
    audio_valid_ = false;
    {std::lock_guard lock(outputs_mutex_);for(auto& [key,output]:outputs_) output->AudioState(false);}
    audio_lifecycle_trace_.Row("%.6f,stopped,0,0,0\n",Now());
  }
  void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString&) override {
    std::lock_guard audio_lock(audio_mutex_);
    audio_lifecycle_trace_.Row("%.6f,error,0,0,0\n",Now());
    audio_valid_=false;if(closing_ || stopped_) return;
    // A closing/inactive page must not terminate other browser contexts. Active
    // outputs receive an explicit failure instead of silently filling with PCM.
    std::lock_guard lock(outputs_mutex_);for(auto& [key,output]:outputs_) output->Fail("Native page audio capture failed");
    if(emit_audio_) writer_.Error("Native audio capture failed");
  }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override {
    if (frame->IsMain() && !ready_) {
      // Handshake only after the initial blank document loads, so its load-end
      // cannot accidentally resolve the operator's first navigation command.
      ready_ = true;
      if (this != root_) {EmitFrame();return;}
      auto header = Header("ready");
      header->SetDouble("timestampMs", Now());
      header->SetString("cefVersion", CEF_VERSION);
      header->SetBool("nativeMedia", true);
      header->SetBool("audioSubscriptions", true);
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
  bool OnDevToolsMessage(CefRefPtr<CefBrowser>, const void* data, size_t size) override {
    if(size>16777216) return false;
    auto value=CefParseJSON(static_cast<const char*>(data),size,JSON_PARSER_RFC);
    auto message=value ? value->GetDictionary() : nullptr;
    if(!message) return false;
    if(message->HasKey("id")) {
      auto found=devtools_pending_.find(message->GetInt("id"));
      if(found==devtools_pending_.end()) return false;
      const auto pending=found->second;devtools_pending_.erase(found);
      if(pending.id==-1) {
        auto result=message->GetDictionary("result");auto info=result ? result->GetDictionary("targetInfo") : nullptr;
        if(!info) {writer_.Error("CEF target discovery failed");return true;}
        root_->targets_.erase(target_id_);
        { std::lock_guard lock(audio_mutex_); target_id_=info->GetString("targetId").ToString(); }
        root_->targets_[target_id_]=this;
        if(root_->auto_attach_) Attach();
        if(create_reply_) {auto response=CefDictionaryValue::Create();response->SetInt("id",create_reply_);auto value=CefDictionaryValue::Create();value->SetString("targetId",target_id_);response->SetDictionary("result",value);root_->CDPMessage(response);create_reply_=0;}
      } else if(pending.id==-2) { if(auto error=message->GetDictionary("error")) writer_.Error(("CEF context policy failed: "+error->GetString("message").ToString()).c_str());
      } else if(pending.native) {
        auto result=message->GetDictionary("result");auto error=message->GetDictionary("error");
        writer_.Reply(pending.id,!error,error ? error->GetString("message").ToString() : "",result ? Bytes(Json(result)) : std::vector<uint8_t>{});
      } else {message->SetInt("id",pending.id);message->SetString("sessionId",pending.session);root_->CDPMessage(message);}
    } else {
      // CEF exposes one page agent. Explicit Playwright CDP sessions attach to
      // that same agent; their commands already receive replies on their alias.
      // Deliver agent notifications on those aliases too, including nested
      // worker session IDs. Otherwise raw sessions can never observe console,
      // network, or tracing completion events despite successful commands.
      std::vector<std::string> recipients{Session()};
      for(const auto& [alias,source]:root_->sessions_) if(source==this) recipients.push_back(alias);
      for(const auto& recipient:recipients) {
        auto event=message->Copy(false);
        if(event->HasKey("sessionId")) event->SetString("sessionId",recipient+"::"+event->GetString("sessionId").ToString());
        else event->SetString("sessionId",recipient);
        auto params=event->GetDictionary("params");
        const auto method=event->GetString("method").ToString();
        if(params && (method=="Target.attachedToTarget" || method=="Target.detachedFromTarget"))
          params->SetString("sessionId",recipient+"::"+params->GetString("sessionId").ToString());
        root_->CDPMessage(event);
      }
    }
    return true;
  }
 private:
  void CloseTargets(const std::string& context="") {
    std::vector<CefRefPtr<CefBrowser>> browsers;
    for(auto& [key,source]:targets_) if(context.empty() || source->context_id_==context) {source->closing_=true;browsers.push_back(source->browser_);}
    for(auto& browser:browsers) browser->GetHost()->CloseBrowser(true);
  }
  struct PendingDevTools {int id;bool native;std::string session;};
  std::string Session() const {return "session-"+target_id_;}
  Dict TargetInfo() {
    auto result=CefDictionaryValue::Create();result->SetString("targetId",target_id_);result->SetString("type","page");
    result->SetString("title","");result->SetString("url",browser_->GetMainFrame()->GetURL());
    result->SetString("browserContextId",context_id_);result->SetBool("attached",true);
    if(!opener_id_.empty()) result->SetString("openerId",opener_id_);
    return result;
  }
  void CDPMessage(Dict message) {auto header=Header("event");header->SetString("topic","cdp");writer_.Send(header,Bytes(Json(message)));}
  void Event(const char* method,Dict params) {auto message=CefDictionaryValue::Create();message->SetString("method",method);message->SetDictionary("params",params);CDPMessage(message);}
  void DetachEvent(const std::string& key,const std::string& parent) {
    auto message=CefDictionaryValue::Create();message->SetString("method","Target.detachedFromTarget");if(!parent.empty())message->SetString("sessionId",parent);
    auto params=CefDictionaryValue::Create();params->SetString("sessionId",key);message->SetDictionary("params",params);CDPMessage(message);
  }
  void Attach() {auto params=CefDictionaryValue::Create();params->SetString("sessionId",Session());params->SetDictionary("targetInfo",TargetInfo());params->SetBool("waitingForDebugger",false);root_->Event("Target.attachedToTarget",params);}
  void CookieCommand(int id,const std::string& session,const std::string& method,Dict params,CefRefPtr<CefRequestContext> context) {
    auto reply=[self=CefRefPtr<Source>(this),id,session](Dict result,std::string error="") {
      auto message=CefDictionaryValue::Create();message->SetInt("id",id);if(!session.empty())message->SetString("sessionId",session);
      if(error.empty()) message->SetDictionary("result",result ? result : CefDictionaryValue::Create());
      else {auto detail=CefDictionaryValue::Create();detail->SetInt("code",-32000);detail->SetString("message",error);message->SetDictionary("error",detail);}self->CDPMessage(message);
    };
    auto manager=context->GetCookieManager(nullptr);
    if(!manager) {reply(nullptr,"CEF cookie manager unavailable");return;}
    if(method=="Storage.getCookies") {
      manager->VisitAllCookies(new CookieVisitor([reply](CefRefPtr<CefListValue> cookies) {auto result=CefDictionaryValue::Create();result->SetList("cookies",cookies);reply(result);}));return;
    }
    if(method=="Storage.clearCookies") {if(!manager->DeleteCookies("","",new CookieDeleteCallback([reply] {reply(nullptr);})))reply(nullptr,"CEF could not clear cookies");return;}
    auto cookies=params->GetList("cookies");if(!cookies || cookies->GetSize()>500) {reply(nullptr,"Invalid cookie batch");return;}
    struct Batch {size_t pending;bool success=true;};auto batch=std::make_shared<Batch>();batch->pending=cookies->GetSize();
    if(!batch->pending) {reply(nullptr);return;}
    auto complete=[batch,reply](bool success) {batch->success &= success;if(--batch->pending==0)reply(nullptr,batch->success ? "" : "CEF rejected a cookie");};
    for(size_t i=0;i<cookies->GetSize();++i) {
      auto item=cookies->GetDictionary(i);if(!item || item->HasKey("partitionKey")) {complete(false);continue;}
      CefCookie cookie;CefString(&cookie.name)=item->GetString("name");CefString(&cookie.value)=item->GetString("value");CefString(&cookie.domain)=item->GetString("domain");CefString(&cookie.path)=item->HasKey("path") ? item->GetString("path") : "/";
      cookie.secure=item->GetBool("secure");cookie.httponly=item->GetBool("httpOnly");cookie.priority=CEF_COOKIE_PRIORITY_MEDIUM;
      const auto site=item->GetString("sameSite").ToString();cookie.same_site=site=="Strict" ? CEF_COOKIE_SAME_SITE_STRICT_MODE : site=="None" ? CEF_COOKIE_SAME_SITE_NO_RESTRICTION : CEF_COOKIE_SAME_SITE_LAX_MODE;
      if(item->HasKey("expires") && Number(item,"expires")>=0) {cookie.has_expires=true;cookie.expires.val=static_cast<int64_t>(Number(item,"expires")*1000000)+11644473600000000LL;}
      auto url=item->GetString("url").ToString();if(url.empty()) {auto domain=item->GetString("domain").ToString();if(domain.starts_with("."))domain.erase(0,1);url=std::string(cookie.secure ? "https://" : "http://")+domain+item->GetString("path").ToString();}
      if(!manager->SetCookie(url,cookie,new CookieSetCallback(complete))) complete(false);
    }
  }
  void CDP(Dict message) {
    if(!message || message->GetInt("id")<1 || message->GetString("method").empty()) {writer_.Error("Invalid CEF protocol command");return;}
    const int id=message->GetInt("id");const auto method=message->GetString("method").ToString();
    auto params=message->GetDictionary("params");if(!params) params=CefDictionaryValue::Create();
    const auto session=message->GetString("sessionId").ToString();
    auto reply=[&](Dict result,std::string error="") {
      auto response=CefDictionaryValue::Create();response->SetInt("id",id);if(!session.empty()) response->SetString("sessionId",session);
      if(error.empty()) response->SetDictionary("result",result ? result : CefDictionaryValue::Create());
      else {auto detail=CefDictionaryValue::Create();detail->SetInt("code",-32000);detail->SetString("message",error);response->SetDictionary("error",detail);}
      CDPMessage(response);
    };
    auto result=CefDictionaryValue::Create();
    if(method=="Browser.getWindowForTarget") {result->SetInt("windowId",1);auto bounds=CefDictionaryValue::Create();bounds->SetInt("width",width_);bounds->SetInt("height",height_);bounds->SetString("windowState","normal");result->SetDictionary("bounds",bounds);reply(result);return;}
    if(method=="Browser.setWindowBounds") {reply(result);return;}
    if(session.empty() || browser_sessions_.contains(session)) {
      if(method=="Browser.getVersion") {
        result->SetString("protocolVersion","1.3");result->SetString("product","Chrome/"+std::to_string(CHROME_VERSION_MAJOR)+"."+std::to_string(CHROME_VERSION_MINOR)+"."+std::to_string(CHROME_VERSION_BUILD)+"."+std::to_string(CHROME_VERSION_PATCH));
        result->SetString("revision",CEF_VERSION);result->SetString("userAgent","Mozilla/5.0 Chrome/"+std::to_string(CHROME_VERSION_MAJOR)+"."+std::to_string(CHROME_VERSION_MINOR)+"."+std::to_string(CHROME_VERSION_BUILD)+"."+std::to_string(CHROME_VERSION_PATCH));result->SetString("jsVersion","");reply(result);return;
      }
      if(method=="Target.setAutoAttach") {auto_attach_=params->GetBool("autoAttach");reply(result);if(auto_attach_) for(auto& [key,source]:targets_) source->Attach();return;}
      if(method=="Target.getTargetInfo") {auto found=targets_.find(params->GetString("targetId").ToString());auto* source=found==targets_.end() ? this : found->second;result->SetDictionary("targetInfo",source->TargetInfo());reply(result);return;}
      if(method=="Target.getTargets") {auto list=CefListValue::Create();for(auto& [key,source]:targets_) list->SetDictionary(list->GetSize(),source->TargetInfo());result->SetList("targetInfos",list);reply(result);return;}
      if(method=="Target.attachToBrowserTarget") {if(browser_sessions_.size()>=256){reply(nullptr,"CEF session limit exceeded");return;}const auto key="browser-"+std::to_string(++context_index_);browser_sessions_.insert(key);result->SetString("sessionId",key);reply(result);return;}
      if(method=="Target.attachToTarget") {
        if(sessions_.size()>=256) {reply(nullptr,"CEF session limit exceeded");return;}
        auto found=targets_.find(params->GetString("targetId").ToString());if(found==targets_.end()){reply(nullptr,"Unknown CEF target");return;}
        const auto alias="extra-"+std::to_string(++context_index_);sessions_[alias]=found->second;session_parents_[alias]=session;result->SetString("sessionId",alias);reply(result);return;
      }
      if(method=="Target.detachFromTarget") {const auto key=params->GetString("sessionId").ToString();sessions_.erase(key);browser_sessions_.erase(key);session_parents_.erase(key);reply(result);DetachEvent(key,session);return;}
      if(method=="Target.createBrowserContext") {
        if(contexts_.size()>=64) {reply(nullptr,"CEF context limit exceeded");return;}
        const auto key="context-"+std::to_string(++context_index_);CefRequestContextSettings settings;
        contexts_[key]=CefRequestContext::CreateContext(settings,nullptr);
        if(params->HasKey("proxyServer")) {auto proxy=CefDictionaryValue::Create();proxy->SetString("mode","fixed_servers");proxy->SetString("server",params->GetString("proxyServer"));proxy->SetString("bypass_list",params->GetString("proxyBypassList"));auto value=CefValue::Create();value->SetDictionary(proxy);CefString error;if(!contexts_[key]->SetPreference("proxy",value,error)) {contexts_.erase(key);reply(nullptr,"CEF proxy configuration failed: "+error.ToString());return;}}
        result->SetString("browserContextId",key);reply(result);return;
      }
      if(method=="Target.createTarget") {
        if(children_.size()>=64) {reply(nullptr,"CEF target limit exceeded");return;}
        const auto key=params->GetString("browserContextId").ToString();auto context=contexts_.find(key);
        if(!key.empty() && context==contexts_.end()) {reply(nullptr,"Unknown browser context");return;}
        CefWindowInfo window;window.SetAsWindowless(0);CefBrowserSettings settings;settings.windowless_frame_rate=browser_->GetHost()->GetWindowlessFrameRate();
        CefRefPtr<Source> child=new Source(writer_,width_,height_,scale_,i420_,true,this,key.empty() ? "default" : key);
        child->create_reply_=id;children_.push_back(child);
        auto created=CefBrowserHost::CreateBrowserSync(window,child,params->GetString("url"),settings,nullptr,context==contexts_.end() ? browser_->GetHost()->GetRequestContext() : context->second);
        if(!created) {child->create_reply_=0;std::erase_if(children_,[&](const auto& item){return item==child;});reply(nullptr,"CEF could not create target");return;}return;
      }
      if(method=="Target.disposeBrowserContext") {const auto key=params->GetString("browserContextId").ToString();CloseTargets(key);contexts_.erase(key);pending_permissions_.erase(key);download_paths_.erase(key);reply(result);return;}
      if(method=="Target.closeTarget") {auto found=targets_.find(params->GetString("targetId").ToString());const bool exists=found!=targets_.end();if(exists) {found->second->closing_=true;found->second->browser_->GetHost()->CloseBrowser(true);}result->SetBool("success",exists);reply(result);return;}
      if(method=="Browser.getWindowForTarget") {result->SetInt("windowId",1);auto bounds=CefDictionaryValue::Create();bounds->SetInt("width",width_);bounds->SetInt("height",height_);bounds->SetString("windowState","normal");result->SetDictionary("bounds",bounds);reply(result);return;}
      if(method=="Browser.setDownloadBehavior") {
        const auto key=params->GetString("browserContextId").ToString();const auto behavior=params->GetString("behavior").ToString();
        if(behavior=="deny") download_paths_.erase(key.empty() ? "default" : key);
        else if(behavior=="allowAndName" && params->GetString("downloadPath").ToString().starts_with("/")) download_paths_[key.empty() ? "default" : key]=params->GetString("downloadPath").ToString();
        else {reply(nullptr,"Unsupported CEF download behavior");return;}reply(nullptr);return;
      }
      if(method=="Browser.close") {reply(nullptr);CloseTargets();return;}
      if(method=="Storage.getCookies" || method=="Storage.setCookies" || method=="Storage.clearCookies") {
        const auto key=params->GetString("browserContextId").ToString();auto found=contexts_.find(key);auto context=key.empty() || key=="default" ? browser_->GetHost()->GetRequestContext() : found==contexts_.end() ? nullptr : found->second;
        if(!context) {reply(nullptr,"Unknown browser context");return;}CookieCommand(id,session,method,params,context);return;
      }
      if(method=="Browser.grantPermissions" || method=="Browser.resetPermissions") {
        const auto supplied=params->GetString("browserContextId").ToString();const auto key=supplied.empty() ? "default" : supplied;
        if(key!="default" && !contexts_.contains(key)) {reply(nullptr,"Unknown browser context");return;}
        if(method=="Browser.resetPermissions") {pending_permissions_.erase(key);RefreshPermissions(key);reply(result);return;}
        auto permissions=params->GetList("permissions");if(!permissions) {reply(nullptr,"Permission list is required");return;}
        for(size_t i=0;i<permissions->GetSize();++i) if(!PermissionBit(permissions->GetString(i).ToString())) {reply(nullptr,"Unsupported native permission: "+permissions->GetString(i).ToString());return;}
        if(pending_permissions_[key].size()>=128) {reply(nullptr,"Context permission limit exceeded");return;}
        const auto origin=params->GetString("origin").ToString();auto& policies=pending_permissions_[key];
        policies.erase(std::remove_if(policies.begin(),policies.end(),[&](const auto& policy){return policy->GetString("origin").ToString()==origin;}),policies.end());
        policies.push_back(params->Copy(false));RefreshPermissions(key);reply(result);return;
      }
      if(method.starts_with("Storage.")) {reply(nullptr,"Unsupported native storage command: "+method);return;}
      reply(nullptr,"Unsupported CEF browser command: "+method);return;
    }
    const auto separator=session.find("::");const auto main=session.substr(0,separator);
    Source* selected=sessions_.contains(main) ? sessions_[main] : nullptr;for(auto& [key,source]:targets_) if(source->Session()==main) {selected=source;break;}
    if(!selected) {reply(nullptr,"Unknown CEF session");return;}
    if(method=="Page.handleJavaScriptDialog" && selected->dialog_) {selected->dialog_->Continue(params->GetBool("accept"),params->GetString("promptText"));selected->dialog_=nullptr;reply(result);return;}
    if(method=="Target.getTargetInfo") {result->SetDictionary("targetInfo",selected->TargetInfo());reply(result);return;}
    if(method=="Emulation.setDeviceMetricsOverride") {const int width=params->GetInt("width"),height=params->GetInt("height");if(width>0 && height>0 && width<=3840 && height<=2160) {selected->width_=width;selected->height_=height;selected->browser_->GetHost()->WasResized();}}
    selected->DispatchCDP(id,method,params,session,false);
  }
  void DispatchCDP(int id,const std::string& method,Dict params,const std::string& session,bool native) {
    if(devtools_pending_.size()>=256) {if(native) writer_.Reply(id,false,"CEF command capacity exceeded");else {auto response=CefDictionaryValue::Create();response->SetInt("id",id);if(!session.empty())response->SetString("sessionId",session);auto error=CefDictionaryValue::Create();error->SetInt("code",-32000);error->SetString("message","CEF command capacity exceeded");response->SetDictionary("error",error);root_->CDPMessage(response);}return;}
    do {devtools_index_=suitecut::NextCommandID(devtools_index_);} while(devtools_pending_.contains(devtools_index_));
    const int internal=devtools_index_;devtools_pending_[internal]={id,native,session};
    auto message=CefDictionaryValue::Create();message->SetInt("id",internal);message->SetString("method",method);message->SetDictionary("params",params);
    const auto separator=session.find("::");if(separator!=std::string::npos) message->SetString("sessionId",session.substr(separator+2));
    const auto data=Json(message);
    if(!browser_->GetHost()->SendDevToolsMessage(data.data(),data.size())) {devtools_pending_.erase(internal);writer_.Reply(id,false,"CEF DevTools dispatch failed");}
  }
  static std::vector<uint8_t> Bytes(const std::string& text) {return {text.begin(),text.end()};}
  static std::string StatusJson(const suitecut::MediaStatus& status) {
    auto dict=CefDictionaryValue::Create();
    dict->SetBool("ready",status.ready);dict->SetBool("stopped",status.stopped);dict->SetBool("paused",status.paused);
    dict->SetDouble("videoFrames",status.video_frames);dict->SetDouble("audioSamples",status.audio_frames);
    dict->SetDouble("repeatedFrames",status.repeated_frames);dict->SetDouble("droppedAudioSamples",status.audio_dropped);
    dict->SetDouble("missingAudioSamples",status.audio_missing);dict->SetDouble("lateAudioSamples",status.audio_late);
    dict->SetDouble("queuedVideoBytes",status.queued_video_bytes);dict->SetDouble("queuedAudioBytes",status.queued_audio_bytes);
    dict->SetDouble("startedMs",status.started_ms);dict->SetDouble("durationMs",status.duration_ms);
    dict->SetDouble("maximumEncodeMs",status.maximum_encode_ms);dict->SetString("error",status.error);
    dict->SetDouble("bufferedDelayMs",status.buffered_delay_ms);
    return Json(dict);
  }
  void EmitFrame(double timestamp = Now()) {
    if (!ready_ || view_.empty()) return;
    bool has_outputs; { std::lock_guard lock(outputs_mutex_); has_outputs = !outputs_.empty(); }
    if (!emit_frames_ && !has_outputs) return;
    std::vector<uint8_t> output;
    const uint8_t* pixels = view_.data();
    if (show_popup_ && !popup_.empty()) {
      output = view_;
      suitecut::CompositeBGRA(output,frame_width_,frame_height_,popup_,popup_width_,popup_height_,
                             popup_rect_.x*scale_,popup_rect_.y*scale_);
      pixels = output.data();
    }
    auto header = Header("frame");
    header->SetString("pixelFormat", i420_ ? "i420" : "bgra");
    if (i420_ || has_outputs) {
      const int yuv_width=(frame_width_+1)&~1,yuv_height=(frame_height_+1)&~1;
      std::vector<uint8_t> padded;
      if(yuv_width!=frame_width_ || yuv_height!=frame_height_) {
        padded.resize(static_cast<size_t>(yuv_width)*yuv_height*4);
        for(int y=0;y<yuv_height;++y) {auto* row=padded.data()+static_cast<size_t>(y)*yuv_width*4;const auto* from=pixels+static_cast<size_t>(std::min(y,frame_height_-1))*frame_width_*4;std::memcpy(row,from,static_cast<size_t>(frame_width_)*4);if(yuv_width!=frame_width_) std::memcpy(row+frame_width_*4,from+(frame_width_-1)*4,4);}
        pixels=padded.data();
      }
      const size_t y_size = static_cast<size_t>(yuv_width) * yuv_height;
      std::vector<uint8_t> planes(y_size * 3 / 2);
      // libyuv's ARGB name describes registers; in little-endian memory its
      // input is BGRA, matching CEF. Use explicit limited-range BT.709.
      if (libyuv::ARGBToI420Matrix(pixels, yuv_width * 4,
          planes.data(), yuv_width, planes.data() + y_size, yuv_width / 2,
          planes.data() + y_size * 5 / 4, yuv_width / 2,
          &libyuv::kArgbH709Constants, yuv_width, yuv_height) != 0) {
        writer_.Error("Native pixel conversion failed");
        return;
      }
      if (has_outputs) {
        auto shared = std::make_shared<const std::vector<uint8_t>>(std::move(planes));
        std::lock_guard lock(outputs_mutex_);
        for (auto& [key, encoder] : outputs_) encoder->Video(shared, yuv_width, yuv_height, timestamp);
        if (i420_ && emit_frames_ && (yuv_width!=frame_width_ || yuv_height!=frame_height_)) {writer_.Error("Raw I420 subscriptions require an even viewport");return;}
        if (i420_ && emit_frames_) output = *shared;
      } else output = std::move(planes);
    } else if (output.empty()) output = view_;
    if (!emit_frames_) return;
    if (!i420_ && output.empty()) output = view_;
    header->SetDouble("timestampMs", timestamp);
    header->SetInt("width", frame_width_);
    header->SetInt("height", frame_height_);
    writer_.Send(header, std::move(output));
  }
  void Command(const std::string& line) {
    if (!browser_) return;
    std::erase_if(job_cancels_,[](const auto& entry) {return entry.second.use_count()==1;});
    for(auto found=media_workers_.begin();found!=media_workers_.end();) {
      if(found->second->load()) {found->first.join();found=media_workers_.erase(found);} else ++found;
    }
    auto value = CefParseJSON(line, JSON_PARSER_RFC);
    auto message = value ? value->GetDictionary() : nullptr;
    if (!message || message->GetInt("version") != 1 || message->GetInt("id") < 1) {
      writer_.Error("Invalid native command");
      return;
    }
    const int id = message->GetInt("id");
    const auto command = message->GetString("command").ToString();
    if (command == "cdp") {writer_.Reply(id,true);root_->CDP(message->GetDictionary("message"));return;}
    if(this==root_ && (command=="media.status" || command=="media.pause" || command=="media.stop" || command=="media.select")) {
      Source* owner=nullptr;const int output_id=message->GetInt("outputId");
      for(auto& [key,source]:targets_) {std::lock_guard lock(source->outputs_mutex_);if(source->outputs_.contains(output_id)) {owner=source;break;}}
      if(!owner) {writer_.Reply(id,false,"Unknown native output");return;}
      if(command=="media.select") {
        auto selected=targets_.find(message->GetString("targetId").ToString());if(selected==targets_.end()) {writer_.Reply(id,false,"Unknown CEF target");return;}
        if(owner!=selected->second) {
          std::scoped_lock lock(owner->outputs_mutex_,selected->second->outputs_mutex_);
          if(owner->output_jobs_.contains(output_id)) {writer_.Reply(id,false,"Native output lifecycle operation is pending");return;}
          selected->second->outputs_[output_id]=owner->outputs_[output_id];owner->outputs_.erase(output_id);selected->second->outputs_[output_id]->AudioState(selected->second->audio_valid_);
        }
        selected->second->EmitFrame();writer_.Reply(id,true);return;
      }
      if(owner!=this) {message->Remove("targetId");owner->Command(Json(message));return;}
      message->Remove("targetId");
    }
    if(message->HasKey("targetId") && this==root_) {
      auto found=targets_.find(message->GetString("targetId").ToString());
      if(found==targets_.end()) {writer_.Reply(id,false,"Unknown CEF target");return;}
      if(found->second!=this) {message->Remove("targetId");found->second->Command(Json(message));return;}
    }
    if(command=="media.cancel") {auto found=job_cancels_.find(message->GetString("jobKey").ToString());if(found!=job_cancels_.end()) *found->second=true;writer_.Reply(id,true);return;}
    if(command=="media.render") {
      if(media_workers_.size()>=8) {writer_.Reply(id,false,"Native file job limit exceeded");return;}
      const auto job_key=message->GetString("jobKey").ToString();auto cancelled=std::make_shared<std::atomic<bool>>(false);job_cancels_[job_key]=cancelled;
      auto done=std::make_shared<std::atomic<bool>>(false);
      media_workers_.emplace_back(std::thread([this,message,id,done,cancelled] {
        try {
          suitecut::RenderOptions config;auto inputs=message->GetList("inputs");if(!inputs) throw std::runtime_error("Native render inputs are required");
          for(size_t i=0;i<inputs->GetSize();++i) config.inputs.push_back(inputs->GetString(i).ToString());
          config.destination=message->GetString("destination").ToString();config.graph=message->GetString("graph").ToString();
          config.fps=message->GetInt("fps");config.duration_ms=Number(message,"durationMs");config.audio=message->GetBool("audio");
          config.container=message->GetString("container").ToString();config.video_codec=message->GetString("videoCodec").ToString();
          config.audio_codec=message->GetString("audioCodec").ToString();config.pixel_format=message->GetString("pixelFormat").ToString();
          config.full_range=message->GetBool("fullRange");config.audio_bitrate=message->GetInt("audioBitrate");
          auto encoder_options=message->GetDictionary("videoOptions");if(encoder_options) {CefDictionaryValue::KeyList keys;encoder_options->GetKeys(keys);for(const auto& key:keys) config.video_options[key.ToString()]=encoder_options->GetString(key).ToString();}
          const auto status=suitecut::RenderMedia(config,[this,cancelled] {return stopped_.load() || cancelled->load();});auto result=CefDictionaryValue::Create();result->SetDouble("videoFrames",status.video_frames);result->SetDouble("audioSamples",status.audio_samples);writer_.Reply(id,true,"",Bytes(Json(result)));
        } catch(const std::exception& error) {writer_.Reply(id,false,error.what());}
        *done=true;
      }),done);return;
    }
    if(command=="media.encoder") {
      auto value=CefDictionaryValue::Create();value->SetBool("available",suitecut::EncoderAvailable(message->GetString("name").ToString()));
      writer_.Reply(id,true,"",Bytes(Json(value)));return;
    }
    if(command=="media.encoders") {
      auto value=CefListValue::Create();
      for(const auto& encoder:suitecut::ListMediaEncoders()) {
        auto item=CefDictionaryValue::Create();item->SetString("name",encoder.name);item->SetString("kind",encoder.kind);item->SetString("codec",encoder.codec);
        item->SetBool("linked",encoder.linked);item->SetBool("hardware",encoder.hardware);item->SetBool("lossless",encoder.lossless);
        auto containers=CefListValue::Create();for(const auto& name:encoder.containers) containers->SetString(containers->GetSize(),name);
        item->SetList("containers",containers);value->SetDictionary(value->GetSize(),item);
      }
      auto result=CefDictionaryValue::Create();result->SetList("encoders",value);
      writer_.Reply(id,true,"",Bytes(Json(result)));return;
    }
    if(command=="media.probe" || command=="media.cut") {
      if(media_workers_.size()>=8) {writer_.Reply(id,false,"Native file job limit exceeded");return;}
      const auto input=message->GetString("input").ToString();auto done=std::make_shared<std::atomic<bool>>(false);
      media_workers_.emplace_back(std::thread([this,message,input,id,command,done] {
        try {
          if(command=="media.probe") {auto probe=suitecut::ProbeMedia(input);auto value=CefDictionaryValue::Create();value->SetDouble("durationMs",probe.duration_ms);value->SetInt("width",probe.width);value->SetInt("height",probe.height);value->SetBool("audio",probe.audio);value->SetString("formatName",probe.format);auto streams=CefListValue::Create();
            for(const auto& stream:probe.streams) {auto item=CefDictionaryValue::Create();item->SetString("kind",stream.kind);item->SetString("codec",stream.codec);item->SetDouble("durationMs",stream.duration_ms);
              if(stream.kind=="audio") {item->SetInt("channels",stream.channels);item->SetInt("sampleRate",stream.sample_rate);}
              else {item->SetInt("width",stream.width);item->SetInt("height",stream.height);item->SetDouble("frameRate",stream.frame_rate);item->SetBool("hasVariableFrameRate",stream.variable);item->SetString("timeBase",stream.time_base);
                if(!stream.pixel_format.empty()) item->SetString("pixelFormat",stream.pixel_format);
                if(!stream.color_range.empty()) item->SetString("colorRange",stream.color_range);
                if(!stream.color_space.empty()) item->SetString("colorSpace",stream.color_space);
                if(!stream.color_transfer.empty()) item->SetString("colorTransfer",stream.color_transfer);
                if(!stream.color_primaries.empty()) item->SetString("colorPrimaries",stream.color_primaries);
              }streams->SetDictionary(streams->GetSize(),item);
            }value->SetList("streams",streams);writer_.Reply(id,true,"",Bytes(Json(value)));}
          else {suitecut::MediaOptions config;config.destination=message->GetString("destination").ToString();config.width=message->GetInt("width");config.height=message->GetInt("height");config.fps=message->GetInt("fps");config.video_codec="libvpx-vp9";
            auto status=suitecut::CutVideo(input,config,Number(message,"startMs"),Number(message,"durationMs"));writer_.Reply(id,true,"",Bytes(StatusJson(status)));}
        } catch(const std::exception& error) {writer_.Reply(id,false,error.what());}
        *done=true;
      }),done);return;
    }
    if (command == "media.start") {
      try {
        suitecut::MediaOptions config;
        config.destination = message->GetString("destination").ToString();
        config.width = message->GetInt("width"); config.height = message->GetInt("height");
        config.fps = message->GetInt("fps"); config.streaming = message->GetBool("streaming");
        if (message->HasKey("audio") && message->GetType("audio")!=VTYPE_BOOL)
          throw std::runtime_error("Native audio enabled must be a boolean");
        config.audio = !message->HasKey("audio") || message->GetBool("audio");
        if (message->HasKey("audioStartup")) {
          throw std::runtime_error("Audio startup policies were removed; realtime audio readiness is mandatory");
        }
        if (message->HasKey("audioStartupTimeoutMs")) {
          if(message->GetType("audioStartupTimeoutMs")!=VTYPE_INT) throw std::runtime_error("Native audio startup timeout must be an integer");
          config.audio_startup_timeout_ms = message->GetInt("audioStartupTimeoutMs");
        }
        if (message->HasKey("videoCodec")) config.video_codec = message->GetString("videoCodec").ToString();
        if (message->HasKey("audioCodec")) config.audio_codec = message->GetString("audioCodec").ToString();
        if (message->HasKey("container")) {
          if(message->GetType("container")!=VTYPE_STRING) throw std::runtime_error("Native container must be a string");
          config.container = message->GetString("container").ToString();
        }
        if (message->HasKey("crf")) {
          if(message->GetType("crf")!=VTYPE_INT && message->GetType("crf")!=VTYPE_DOUBLE)
            throw std::runtime_error("Native CRF must be a number");
          config.crf = Number(message,"crf");
        }
        if (message->HasKey("bitrateKbps")) config.bitrate_kbps = message->GetInt("bitrateKbps");
        if (message->HasKey("audioBitrateKbps")) config.audio_bitrate_kbps = message->GetInt("audioBitrateKbps");
        {std::lock_guard lock(outputs_mutex_);if(outputs_.size()>=8) throw std::runtime_error("Native output limit exceeded");}
        auto output = std::make_shared<suitecut::MediaOutput>(config, Now);
        output->AudioState(audio_valid_);
        { std::lock_guard lock(outputs_mutex_);
          outputs_.emplace(id, output); }
        auto started = CefDictionaryValue::Create();
        started->SetInt("outputId",id);started->SetBool("audioReadinessRequired",config.audio);started->SetString("audioReadiness","engine");
        writer_.Reply(id,true,"",Bytes(Json(started)));
        EmitFrame();
      } catch (const std::exception& error) { writer_.Reply(id, false, error.what()); }
    } else if (command == "media.status" || command == "media.pause" || command == "media.stop") {
      std::shared_ptr<suitecut::MediaOutput> output;
      { std::lock_guard lock(outputs_mutex_); auto found=outputs_.find(message->GetInt("outputId"));
        if (found!=outputs_.end()) output=found->second; }
      if (!output) {writer_.Reply(id,false,"Unknown native output");return;}
      if (command == "media.stop" || command == "media.pause") {
        if(media_workers_.size()>=8) {writer_.Reply(id,false,"Native file job limit exceeded");return;}
        auto done=std::make_shared<std::atomic<bool>>(false);
        const int output_id=message->GetInt("outputId");
        const bool stop=command=="media.stop",paused=message->GetBool("paused");
        {std::lock_guard lock(outputs_mutex_);++output_jobs_[output_id];}
        media_workers_.emplace_back(std::thread([self=CefRefPtr<Source>(this), output, output_id, id, done, stop, paused] {
          try {
          suitecut::MediaStatus status;
          if(stop) {
            status=output->Stop();
            std::lock_guard lock(self->outputs_mutex_);self->outputs_.erase(output_id);
          } else {
            output->Pause(paused);status=output->Status();
            if (!paused) CefPostTask(TID_UI,new Task([self] {if(!self->stopped_) self->EmitFrame();}));
          }
          self->writer_.Reply(id,status.error.empty(),status.error,Bytes(StatusJson(status)));
          } catch(const std::exception& error) {self->writer_.Reply(id,false,error.what());}
          {std::lock_guard lock(self->outputs_mutex_);if(--self->output_jobs_[output_id]==0) self->output_jobs_.erase(output_id);}
          *done=true;
        }),done);
      } else writer_.Reply(id,true,"",Bytes(StatusJson(output->Status())));
    } else if (command == "audio") {
      if (message->GetType("enabled") != VTYPE_BOOL) {writer_.Reply(id,false,"Audio subscription requires a boolean");return;}
      emit_audio_=message->GetBool("enabled");writer_.Reply(id,true);
    } else if (command == "frames") {
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
      DispatchCDP(id,message->GetString("method").ToString(),message->GetDictionary("params"),"",true);
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
          if (queued_commands_.fetch_add(1) >= 64 || queued_command_bytes_.fetch_add(pending.size()) + pending.size() > 16777216) {stopped_=true;break;}
          CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), line = std::move(pending)] {
            self->queued_commands_--;self->queued_command_bytes_-=line.size();
            self->Command(line);
          }));
          pending.clear();
        } else {
          pending += buffer[i];
          if (pending.size() >= 4194304) { stopped_ = true; break; }
        }
      }
    }
    CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
      self->CloseTargets();
    }));
  }
  CefRefPtr<CefJSDialogCallback> dialog_;
  std::map<std::string,std::string> download_paths_;
  std::map<std::string,std::vector<Dict>> pending_permissions_;
  std::map<uint32_t,std::string> download_ids_;
  // Child CEF callbacks may outlive the opener's OnBeforeClose. Hold the
  // protocol owner until this client's last callback/reference is released.
  // children_/pending_popups_ remove their child references on close/abort.
  CefRefPtr<Source> root_owner_;
  Source* root_;
  std::string context_id_,opener_id_,target_id_;
  std::set<std::string> permission_origins_;
  std::map<std::string,Source*> targets_, sessions_;
  std::map<std::string,std::string> session_parents_;
  std::set<std::string> browser_sessions_;
  std::map<std::string,CefRefPtr<CefRequestContext>> contexts_;
  std::vector<CefRefPtr<Source>> children_;
  std::map<std::pair<Source*,int>,CefRefPtr<Source>> pending_popups_;
  std::map<int,PendingDevTools> devtools_pending_;
  int devtools_index_=0,create_reply_=0;
  uint64_t context_index_=0;
  bool auto_attach_=false;
  std::map<std::string,std::shared_ptr<std::atomic<bool>>> job_cancels_;
  std::mutex outputs_mutex_;
  std::map<int, std::shared_ptr<suitecut::MediaOutput>> outputs_;
  std::map<int,size_t> output_jobs_;
  std::vector<std::pair<std::thread,std::shared_ptr<std::atomic<bool>>>> media_workers_;
  std::atomic<bool> emit_audio_{true};
  Writer& writer_;
  int width_, height_;
  double scale_;
  bool i420_;
  bool frame_subscriptions_;
  int frame_width_ = 0, frame_height_ = 0, popup_width_ = 0, popup_height_ = 0, navigation_ = 0;
  bool show_popup_ = false, ready_ = false, emit_frames_ = true;
  CefRect popup_rect_;
  std::vector<uint8_t> view_, popup_;
  std::atomic<bool> stopped_{false}, audio_valid_{false}, closing_{false};
  SuiteCutAudioClock audio_clock_;
  std::mutex audio_mutex_;
  SuiteCutAudioTrace audio_trace_, audio_lifecycle_trace_;
  std::atomic<int> queued_commands_{0};
  std::atomic<size_t> queued_command_bytes_{0};
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
