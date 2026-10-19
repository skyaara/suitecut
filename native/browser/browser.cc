// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <charconv>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <memory>
#include <map>
#include <limits>
#include <set>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#if defined(__linux__)
#include "../runner/retained_source_endpoint.h"
#endif

#include "include/cef_app.h"
#include "include/cef_audio_handler.h"
#include "include/cef_client.h"
#include "include/cef_parser.h"
#include "include/cef_permission_handler.h"
#include "include/cef_dialog_handler.h"
#include "context_controls.h"
#include "context_workers.h"
#include "accepted_inspector_work.h"
#include "accepted_packet_receipt.h"
#include "packet_write.h"
#include "accepted_task_work.h"
#include "locator_handler_callback_commands.h"
#include "locator_handler_async_work.h"
#include "creation_watchdog.h"
#include "accepted_publication_audit.h"
#if defined(__linux__)
#include "accepted_engine_work_wake.h"
#endif
#include "trace_controls.h"
#include "coverage_targets.h"
#include "coverage_script_inventory.h"
#include "pristine_dispatch_factories.h"
#include "pristine_dispatch_provenance.h"
#include "pristine_creation_identities.h"
#include "pristine_child_creation.h"
#include "debugger_domains.h"
#include "developer_debugger_controls.h"
#include "developer_debugger_frame_targets.h"
#include "developer_debugger_frame_pause.h"
#include "developer_debugger_documents.h"
#include "developer_debugger_worker_documents.h"
#include "developer_debugger_worker_targets.h"
#include "developer_debugger_worker_pause.h"
#include "developer_debugger_initial_trees.h"
#include "trace_background.h"
#include "trace_preview.h"
#include "trace_dom_tree.h"
#include "trace_phase_controls.h"
#include "trace_sources.h"
#include "trace_action_metadata.h"
#include "trace_context_actions.h"
#include "credential_page.h"
#include "context_console_controls.h"
#include "context_network_controls.h"
#include "context_worker_network.h"
#include "context_network_policy.h"
#include "network_archives.h"
#include "context_network_headers.h"
#include "context_settings.h"
#include "idle_controls.h"
#include "context_authentication_clear.h"
#include "context_geolocation.h"
#include "context_auth_policy.h"
#include "network_fatal_teardown.h"
#include "dictionary_number.h"
#include "element_bounds_geometry.h"
#include "element_bounds_reply.h"
#include "element_state_controls.h"
#include "scroll_readiness.h"
#include "select_text.h"
#include "blur.h"
#include "dispatch_event.h"
#include "selector_wait_controls.h"
#include "visibility_snapshot_controls.h"
#include "visibility_snapshot_reply.h"
#include "handle_disposal_controls.h"
#include "page_error_controls.h"
#include "utility_browser.h"
#include "renderer_app.h"
#include "include/cef_frame_handler.h"
#include "http_controls.h"
#include "har_recording.h"
#include "context_har_routes.h"
#include "context_unroute_all.h"
#include "context_routes.h"
#include "websocket_routes.h"
#include "har_replay_files.h"
#include "download_controls.h"
#include "recording_controls.h"
#include "context_video.h"
#include "launch_video_options.h"
#include "native_backend_capabilities.h"
#include "widget_mouse_controls.h"
#if defined(OS_LINUX)
#include "streaming_controls.h"
#endif
#include "network_controls.h"
#include "network_publication_results.h"
#include "response_body_reads.h"
#include "response_streams.h"
#include "private_response_session.h"
#include "request_body_reads.h"
#include "websocket_controls.h"
#include "binding_controls.h"
#include "include/cef_task.h"
#include "include/cef_version.h"
#include "audio_clock.h"
#include "frame_rate.h"
#include "command_framer.h"
#include "command_credits.h"
#include "operation_registry.h"
#include "input_keys.h"
#include "drag_completions.h"
#include "virtual_time.h"
#include "virtual_time_document.h"
#include "virtual_time_close_fence.h"
#include "send_keys_sequence.h"
#include "dom_query.h"
#include "locator_normalization.h"
#include "locator_suggestion.h"
#include "locator_suggestion_dom.h"
#include "locator_normalization_metadata.h"
#include "dom_domain_leases.h"
#include "locator_highlight_controls.h"
#include "query_diagnostics.h"
#include "ax_forest.h"
#include "devtools_utf16.h"
#include "dom_handle_preview_cef.h"
#include "inbound_string.h"
#include "frame_metadata.h"
#include "dom_semantic_snapshot.h"
#include "snapshot_reference_controls.h"
#include "assertion_deadline_controls.h"
#include "snapshot_reference_capture.h"
#include "snapshot_reference_dispatch.h"
#include "snapshot_reference_metadata.h"
#include "snapshot_reference_projection.h"
#include "snapshot_reference_publish.h"
#include "snapshot_reference_frames.h"
#include "native_aria_formatter.h"
#include "native_aria_json.h"
#include "storage_utf16.h"
#include "query_node_transfer.h"
#include "owner_frame_controls.h"
#include "owner_frame_document.h"
#include "owner_frame_origin.h"
#include "query_realms.h"
#include "query_world_plan.h"
#include "selector_registry.h"
#include "libyuv/convert_from_argb.h"
#include "libyuv/convert.h"
#include "capture_canvas.h"
#include "capture_buffer.h"
#include "form_controls.h"
#include "artifact_controls.h"
#include "screenshot_compare.h"
#include "screenshot_assertion_retirement.h"
#include "screenshot_assertion_response.h"
#include "artifact_config_receipts.h"
#include "action_pauses.h"
#include "url_predicates.h"
#include "event_predicates.h"
#include "console_journal.h"
#include "console_array_preview.h"
#include "dialog_controls.h"
#include "touch_controls.h"
#include "profile_lock.h"
#include "favicon_policy.h"
#include "clear_dom.h"
#include "editable_target.h"
#include "editable_focus.h"
#include "send_keys_dom.h"
#include "type_dom.h"
#include "input_value.h"
#include "native_memory_diagnostics.h"

namespace {
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
double Now() { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); }
using Dict = CefRefPtr<CefDictionaryValue>;
constexpr size_t kMaxResponseBytes = 192 * 1048576;
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
std::string Literal(const std::string& text) {
  auto value = CefValue::Create();
  value->SetString(text);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}
std::string HandleNamespace() {
  std::random_device random;
  const char* hex = "0123456789abcdef";
  std::string value;
  for (int word = 0; word < 4; ++word) {
    const uint32_t bits = random();
    for (int shift = 28; shift >= 0; shift -= 4) value.push_back(hex[(bits >> shift) & 15]);
  }
  return value;
}
struct Packet { std::string header; std::vector<uint8_t> data; bool reply_credit = false; bool event = false; suitecut::AcceptedPacketReceipt native_work;uint64_t native_watermark=0; };

// Never block a CEF callback on Node or an encoder. Pending frames are replaced
// per page and served in arrival order within a 256 MiB aggregate budget.
class Writer {
 public:
  explicit Writer(int output) : output_(output) {
    fcntl(output_, F_SETFL, fcntl(output_, F_GETFL) | O_NONBLOCK);
    thread_ = std::thread([this] { Run(); });
  }
  ~Writer() { Stop(); close(output_); }
  void Stop() {
    publication_->Unsupported();
    stopping_ = true;
    reply_credits_.Stop();
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
  }
  std::shared_ptr<suitecut::AcceptedWriterWork> NativePublication()const noexcept{return publication_;}
  void WorkChanged()noexcept{cv_.notify_one();}
  suitecut::AcceptedPublicationAudit& PublicationAudit()noexcept{return publication_audit_;}
  // Source must authenticate one actual Engine barrier before invoking this.
  // This only queues a cutoff packet on original stdout; it grants no idle.
  bool QueueWorkWatermark(std::shared_ptr<suitecut::AcceptedReaderWork> reader,suitecut::NativeStreamCut reader_cut,uint64_t barrier){
    std::lock_guard lock(mutex_);
    if(stopping_||!reader||!reader->KnownCut(reader_cut)||!barrier){publication_->Unsupported();return false;}
    for(const auto& value:watermarks_)if(value&&value->barrier==barrier){publication_->Unsupported();return false;}
    for(auto& value:watermarks_)if(!value){
      const auto cut=publication_->Cut();if(!cut.nonce)return false;
      try{
        auto header=Header("workwatermark");header->SetInt("bytes",0);header->SetString("barrier",std::to_string(barrier));
        header->SetString("readerInstance",std::to_string(reader_cut.instance));header->SetString("readerThrough",std::to_string(reader_cut.sequence));header->SetString("readerCut",std::to_string(reader_cut.nonce));
        header->SetString("writerInstance",std::to_string(cut.instance));header->SetString("writerThrough",std::to_string(cut.sequence));header->SetString("writerCut",std::to_string(cut.nonce));
        value=WorkWatermark{barrier,std::move(reader),reader_cut,cut,Json(header),false,false};cv_.notify_one();return true;
      }catch(...){publication_->Unsupported();publication_->ReleaseCut(cut);throw;}
    }
    publication_->Unsupported();return false;
  }
  std::optional<std::pair<suitecut::NativeStreamCut,suitecut::NativeStreamCut>> WorkWatermarkIdentity(uint64_t barrier){
    std::lock_guard lock(mutex_);
    for(const auto& value:watermarks_)if(value&&value->barrier==barrier)return std::pair{value->reader_cut,value->writer_cut};
    return std::nullopt;
  }
  bool WorkWatermarkWritten(uint64_t barrier,suitecut::NativeStreamCut reader,suitecut::NativeStreamCut writer){
    std::lock_guard lock(mutex_);
    for(const auto& value:watermarks_)if(value&&value->barrier==barrier&&value->reader_cut==reader&&value->writer_cut==writer)
      return !watermark_failure_&&value->written&&value->reader->Through(reader)&&publication_->Through(writer);
    return false;
  }
  std::exception_ptr WorkWatermarkFailure(){std::lock_guard lock(mutex_);return watermark_failure_;}
  // Call only after the Engine receives the matching authenticated SDK receipt
  // for this original stdout packet; this is not a generic close/idle ACK.
  bool RetireWorkWatermark(uint64_t barrier,suitecut::NativeStreamCut reader,suitecut::NativeStreamCut writer){
    std::lock_guard lock(mutex_);
    for(auto& value:watermarks_)if(value&&value->barrier==barrier&&value->reader_cut==reader&&value->writer_cut==writer){
      if(watermark_failure_||!value->written||!value->reader->Through(reader)||!publication_->Through(writer))return false;
      const bool readers=value->reader->ReleaseCut(reader),writers=publication_->ReleaseCut(writer);
      if(!readers||!writers){publication_->Unsupported();return false;}value.reset();return true;
    }
    return false;
  }
  bool ReserveReply(const std::atomic<bool>& canceled) { return reply_credits_.Reserve(canceled); }
  void ReleaseReply() { reply_credits_.Release(); }
  void WakeReaders() { reply_credits_.Wake(); }
  void DropPage(int page) {
    std::lock_guard lock(mutex_);
    const auto found = frames_.find(page);
    if (found != frames_.end()) { frame_bytes_ -= found->second.data.size();found->second.native_work.Dropped(suitecut::AcceptedWriterWork::Discard::PageDropped); frames_.erase(found); }
    std::erase(frame_order_, page); dropped_.erase(page);
    std::erase_if(audio_, [page](Packet& packet) {
      auto parsed = CefParseJSON(packet.header, JSON_PARSER_RFC);
      const bool discard=parsed&&parsed->GetDictionary()->GetInt("pageId")==page;if(discard)packet.native_work.Dropped(suitecut::AcceptedWriterWork::Discard::PageDropped);return discard;
    });
  }
  void Send(Dict header, std::vector<uint8_t> data = {}, bool reply_credit = false) {
    header->SetInt("bytes", static_cast<int>(data.size()));
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    const auto type = header->GetString("type").ToString();
    if (type == "frame") {
      const int page = header->GetInt("pageId");
      if (const auto found = frames_.find(page); found != frames_.end()) {
        ++dropped_[page]; frame_bytes_ -= found->second.data.size();found->second.native_work.Dropped(suitecut::AcceptedWriterWork::Discard::FrameReplaced); frames_.erase(found);
      } else frame_order_.push_back(page);
      while (!frames_.empty() && data.size() > 256 * 1048576 - frame_bytes_) {
        const int oldest = frame_order_.front(); frame_order_.pop_front();
        if (oldest == page) { frame_order_.push_back(page); continue; }
        const auto found = frames_.find(oldest);
        if (found != frames_.end()) { ++dropped_[oldest]; frame_bytes_ -= found->second.data.size();found->second.native_work.Dropped(suitecut::AcceptedWriterWork::Discard::FrameBudget); frames_.erase(found); }
      }
      header->SetDouble("dropped", static_cast<double>(dropped_[page]));
      frame_bytes_ += data.size(); frames_[page] = TrackedPacket(Json(header),std::move(data),false,false,suitecut::AcceptedWriterWork::Kind::Frame);
    } else if (type == "audio") {
      if(audio_.size()>=100){audio_.front().native_work.Dropped(suitecut::AcceptedWriterWork::Discard::AudioBudget);audio_.pop_front();}
      audio_.push_back(TrackedPacket(Json(header),std::move(data),false,false,suitecut::AcceptedWriterWork::Kind::Audio));
    } else {
      if (reply_credit && (data.size() > kMaxResponseBytes || data.size() > 256 * 1048576 - reply_bytes_)) {
        header->SetBool("ok", false); header->SetString("error", "Native response exceeded its bounded output budget");
        std::vector<uint8_t>().swap(data); header->SetInt("bytes", 0);
      }
      const auto json=Json(header);
      if (!reply_credit) {
        const size_t bytes = json.size()+data.size();
        if (event_failed_) return;
        if (event_count_ >= 4096 || bytes > 8 * 1048576 - event_bytes_) {
          // An unread event stream cannot be losslessly retained forever. Fail
          // explicitly without dropping command replies or blocking CEF's UI.
          event_failed_ = true;
          auto error = Header("error"); error->SetInt("bytes", 0);
          error->SetString("error", "Native event stream exceeded its bounded output budget");
          control_.push_front(TrackedPacket(Json(error),{},false,false,suitecut::AcceptedWriterWork::Kind::Control));
          cv_.notify_one(); return;
        }
        ++event_count_; event_bytes_ += bytes;
      }
      publication_audit_.Published(type);
      auto packet=TrackedPacket(json,std::move(data),reply_credit,!reply_credit,reply_credit?suitecut::AcceptedWriterWork::Kind::Control:suitecut::AcceptedWriterWork::Kind::Event);
      if(reply_credit)reply_bytes_+=packet.data.size();
      control_.push_back(std::move(packet));
    }
    cv_.notify_one();
  }
  // Network binary publication applies backpressure without discarding data.
  bool TryEvent(Dict header,std::vector<uint8_t> data) {
    header->SetInt("bytes",static_cast<int>(data.size()));const auto json=Json(header);
    std::lock_guard lock(mutex_);const auto bytes=json.size()+data.size();
    if(stopping_ || event_failed_ || event_count_>=4096 || bytes>8*1048576-event_bytes_)return false;
    publication_audit_.Published(header->GetString("type").ToString());
    ++event_count_;event_bytes_+=bytes;control_.push_back(TrackedPacket(json,std::move(data),false,true,suitecut::AcceptedWriterWork::Kind::Event));cv_.notify_one();return true;
  }
  void Reply(int id, bool ok, const std::string& error = "", std::vector<uint8_t> data = {}, Dict navigation = nullptr) {
    auto header = Header("response");
    header->SetInt("id", id);
    header->SetBool("ok", ok);
    if (navigation) header->SetDictionary("navigation", navigation);
    if (!error.empty()) header->SetString("error", error.substr(0, 4096));
    Send(header, std::move(data), true);
  }
  void Error(const char* message) {
    auto header = Header("error");
    header->SetString("error", message);
    Send(header);
  }
 private:
  struct WorkWatermark {uint64_t barrier;std::shared_ptr<suitecut::AcceptedReaderWork> reader;suitecut::NativeStreamCut reader_cut,writer_cut;std::string header;bool queued,written;};
  Packet TrackedPacket(std::string header,std::vector<uint8_t> data,bool reply_credit,bool event,suitecut::AcceptedWriterWork::Kind kind){
    if(header.size()>SIZE_MAX-4||data.size()>SIZE_MAX-4-header.size()){publication_->Unsupported();return {std::move(header),std::move(data),reply_credit,event,{}};}
    const auto bytes=4+header.size()+data.size();return {std::move(header),std::move(data),reply_credit,event,suitecut::AcceptedPacketReceipt(publication_,kind,bytes)};
  }
  bool WorkWatermarkReady()const{
    if(watermark_failure_)return false;
    for(const auto& value:watermarks_)if(value&&!value->queued&&value->reader->Through(value->reader_cut)&&publication_->Through(value->writer_cut))return true;
    return false;
  }
  void EnqueueReadyWorkWatermarks(){
    if(watermark_failure_)return;
    for(auto& value:watermarks_)if(value&&!value->queued&&value->reader->Through(value->reader_cut)&&publication_->Through(value->writer_cut)){
      try{auto packet=TrackedPacket(value->header,{},false,false,suitecut::AcceptedWriterWork::Kind::Watermark);packet.native_watermark=value->barrier;control_.push_front(std::move(packet));value->queued=true;}
      catch(...){publication_->Unsupported();watermark_failure_=std::current_exception();return;}
    }
  }
  void Run() {
    while (!stopping_) {
      Packet packet;
      {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !control_.empty() || !audio_.empty() || !frames_.empty()||WorkWatermarkReady(); });
        if (stopping_) break;
        EnqueueReadyWorkWatermarks();
        if (!control_.empty()) {
          packet = std::move(control_.front()); control_.pop_front();
          if (packet.event) { --event_count_; event_bytes_ -= packet.header.size() + packet.data.size(); }
        }
        else if (!audio_.empty()) { packet = std::move(audio_.front()); audio_.pop_front(); }
        else {
          const int page = frame_order_.front(); frame_order_.pop_front();
          packet = std::move(frames_.at(page)); frame_bytes_ -= packet.data.size(); frames_.erase(page);
        }
        packet.native_work.Dequeued();
      }
      if(!suitecut::WriteNativePacket(output_, packet.header, packet.data, stopping_, packet.native_work))break;
      const bool published=packet.native_work.FullWrite();
      if(packet.native_watermark){std::lock_guard lock(mutex_);for(auto& value:watermarks_)if(value&&value->barrier==packet.native_watermark){if(published)value->written=true;else publication_->Unsupported();}}
      if (packet.reply_credit) {
        { std::lock_guard lock(mutex_); reply_bytes_ -= packet.data.size(); }
        reply_credits_.Release();
      }
    }
  }
  std::mutex mutex_;
  suitecut::AcceptedPublicationAudit publication_audit_;
  std::shared_ptr<suitecut::AcceptedWriterWork> publication_=std::make_shared<suitecut::AcceptedWriterWork>();
  std::array<std::optional<WorkWatermark>,8> watermarks_{};std::exception_ptr watermark_failure_;
  int output_;
  std::condition_variable cv_;
  std::deque<Packet> control_, audio_;
  std::map<int, Packet> frames_;
  std::deque<int> frame_order_;
  std::map<int, uint64_t> dropped_;
  size_t frame_bytes_ = 0;
  size_t event_count_ = 0, event_bytes_ = 0, reply_bytes_ = 0;
  bool event_failed_ = false;
  std::atomic<bool> stopping_{false};
  SuiteCutCommandCredits reply_credits_{128};
  std::thread thread_;
};

// Shared owner-only watchdogs carry no child payload. Keep their original
// reader/fixture provenance for public cuts, without borrowing one callback's
// private invocation when they service other operations on later turns.
suitecut::NativeWorkOrigin LocatorHandlerMonitorOrigin()noexcept {
  auto origin=suitecut::CurrentNativeWorkOrigin();origin.locator_handler_invocation=0;return origin;
}

class Task : public CefTask {
 public:
  explicit Task(std::function<void()> callback,suitecut::AcceptedTaskWork::Kind kind=suitecut::AcceptedTaskWork::Kind::Work,suitecut::NativeWorkOrigin origin=suitecut::CurrentNativeWorkOrigin()) : native_work_(kind,suitecut::NativeProcessTaskWork(),origin),callback_(std::move(callback)) {}
  void Execute() override {const suitecut::AcceptedTaskFrame native_frame(native_work_);callback_();}
 private:
  suitecut::AcceptedTaskReceipt native_work_; // Captures below destroy first.
  std::function<void()> callback_;
  IMPLEMENT_REFCOUNTING(Task);
};

// This dedicated wrapper owns no Source captures. Its UI-owned payload is
// genuinely destroyed on completed creation, while CEF may retain this inert
// delayed object until the unchanged original 30-second due time.
class CreationWatchdogTask : public CefTask {
 public:
  explicit CreationWatchdogTask(std::shared_ptr<suitecut::NativeCreationWatchdog> state):state_(std::move(state)){}
  void Execute() override {
    if(!CefCurrentlyOn(TID_UI))throw std::runtime_error("Native creation watchdog executed outside UI");
    state_->Execute();
  }
 private:
  std::shared_ptr<suitecut::NativeCreationWatchdog> state_;
  IMPLEMENT_REFCOUNTING(CreationWatchdogTask);
};

class Source;
class Engine {
 public:
  Engine(Writer& writer, int width, int height, double scale, bool i420, int fps);
  ~Engine();
  bool Initialize(Dict launch_video = nullptr,double startup_deadline = 0);
  bool StartupReady(int page);
  void StartupFailed(const std::string& error);
  Dict VideoMetadata(int page) const {const auto video=context_video_->Metadata(page);if(!video)return nullptr;auto result=CefDictionaryValue::Create();result->SetString("id",video->id);return result;}
  bool CreatePage(int page, int context, CefRefPtr<CefRequestContext> request_context);
  CefRefPtr<Source> PopupClient(int opener, int popup_id, const std::string& initial_url);
  bool PageCreated(int page, CefRefPtr<CefBrowser> browser);
  void PageClosed(int page);
  void AbortPopup(int opener, int popup_id);
  void Route(const std::string& line);
  void CloseAll();
  void Idle();
  void Deadlines();
  void CompleteContext(int id, bool ok, Dict result = nullptr, const std::string& error = "", bool lossless_console = false);
  void ContextCommand(int id, uint64_t generation, const std::string& action, Dict params);
  suitecut::NativeContextWorkerNetwork& worker_network(){return *worker_network_;}
  void ContextWorkerNetwork(int context,const std::string&owner,Dict worker,Dict info){if(!worker_network_->Observe(context,owner,worker,info))writer_.Error("Native context worker network observation rejected");}
  void ContextNetwork(int page,Dict info) { if(!context_network_->Observe(contexts_->ContextId(page),page,info))writer_.Error("Native context network journal rejected payload"); }
  void ContextConsole(int page,Dict info) { if(!context_console_->Observe(contexts_->ContextId(page),page,info))writer_.Error("Native context console journal rejected payload"); }
  std::shared_ptr<suitecut::AcceptedInspectorWork> AcceptedInspector()const noexcept{return inspector_work_;}
  std::shared_ptr<suitecut::AcceptedReaderWork> AcceptedReader()const noexcept{return reader_work_;}
#if defined(__linux__)
  void BindAcceptedWork(suitecut::runner::RetainedSourceEndpoint* endpoint);
  uint64_t AcceptedSourceTicket()const noexcept;
  static double AcceptedWorkNow();
  void PumpAcceptedWork();
  void PostAcceptedWork();
  void OriginalAcceptedWorkBarrier(int id,Dict message);
#else
  uint64_t AcceptedSourceTicket()const noexcept{return 0;}
#endif
  bool AcceptedWorkReady(bool viewer = false)const;
  uint64_t RetainLocatorHandler(int page,CefRefPtr<Source> source);
  void WakeLocatorHandlers();
  void TickLocatorHandlers();
  bool LocatorHandlerControl(int id,const std::string& command,Dict message);
  bool LocatorHandlerAdmission(Dict message,double& deadline)const;
  void LocatorHandlerChannelClosed();
  bool TrackLocatorHandlerCommand(int id,int page,uint64_t generation);
  void ForgetLocatorHandlerCommand(int id);
  suitecut::NativeWorkOrigin LocatorHandlerCommandOrigin(int id,int page,uint64_t generation)const;
  bool LocatorHandlerDescendantsReady(uint64_t invocation)const;
  std::string http_user_agent_;
  suitecut::ContextSettingsRegistry& settings(){return settings_;}
  suitecut::NativeContextGeolocation& geolocation(){return geolocation_;}
  suitecut::NativeContextAuthenticationControls& authentication(){return authentication_;}
  void AuthenticationFatal(int context,const std::string&error);
  Dict BrowserInfo(int page)const{auto info=contexts_->BrowserExtraInfo(page);info->SetBool("suitecutInitialEmptyDocument",!contexts_->IsPopup(page));if(!contexts_->IsPopup(page))info->SetBool("suitecutContextSettingsBootstrap",true);return info;}
  suitecut::NativeContextNetworkPolicy& network_policy(){return network_policy_;}
  suitecut::NativeNetworkArchives& network_archive(){return *network_archive_;}
  #include "context_route_engine.inc"
  suitecut::NativeWebSocketRoutes& websocket_routes(){return *websocket_routes_;}
  suitecut::NativeContextHARRoutes& har_routes(){return *context_har_;}
  void HARPolicyFailure(const std::string&owner,const std::string&error);
  suitecut::NativeHARRecording& har_recording(){return *har_recording_;}
  suitecut::NativeHARReplayFiles& har_files(){return *har_files_;}
  suitecut::NativeContextNetworkHeaders& network_headers(){return network_headers_;}
  std::shared_ptr<suitecut::TraceControls> traces() const{return traces_;}
  std::shared_ptr<suitecut::ScreenshotComparisons> screenshot_comparisons() const{return screenshot_comparisons_;}
  void ScreenshotInputControl(int id,Dict params,uint64_t generation);
  std::shared_ptr<suitecut::ScreenshotInputs> screenshot_inputs()const{return screenshot_inputs_;}
  size_t ArtifactInputReceipts()const;
  bool TraceClaim(bool dom=true){if(trace_physical_>=16)return false;++trace_physical_;if(dom)++trace_dom_physical_;return true;}
  void TraceRelease(bool dom=true){if(trace_physical_&&(!dom||trace_dom_physical_)){--trace_physical_;if(dom)--trace_dom_physical_;}else writer_.Error("Native trace physical ownership underflow");}
  std::vector<CefRefPtr<Source>> OwnerFrameSources(int page) const;
  bool TracePending(int context) const;
  void TracePaint(int page,int css_width,int css_height,double now,const void* pixels,int width,int height);
  void WakeTrace();
  std::shared_ptr<SuiteCutContextControls> contexts() const { return contexts_; }
  std::shared_ptr<SuiteCutHTTPControls> http() const { return http_; }
  std::shared_ptr<suitecut::DownloadControls> downloads() const { return downloads_; }
  std::shared_ptr<suitecut::RecordingControls> recordings() const { return recordings_; }
#if defined(OS_LINUX)
  std::shared_ptr<suitecut::StreamingControls> streaming() const { return streaming_; }
  bool SelectStreamingSource(const std::string&, int page, std::string& error);
#endif
  const SuiteCutSelectorRegistry& selectors() const { return selectors_; }
  void HTTPDiagnostics(Dict output) const {
    const auto source_lifetimes=suitecut::NativeSourceLifetimes().Inspect();
    output->SetDouble("nativeSourcesConstructed",double(source_lifetimes.constructed));output->SetDouble("nativeSourcesDestroyed",double(source_lifetimes.destroyed));output->SetDouble("nativeSourcesLive",double(source_lifetimes.live));
    output->SetDouble("nativeSourceRegistryEntries",double(sources_.size()));output->SetDouble("nativeContextRegistryEntries",double(contexts_->ContextCount()));output->SetDouble("nativePageRegistryEntries",double(contexts_->PageCount()));
    const auto tasks=suitecut::NativeProcessTaskWork().Inspect();
    output->SetDouble("nativeTasksQueued",double(tasks.queued));output->SetDouble("nativeTasksExecuting",double(tasks.executing));output->SetDouble("nativeTasksReturnedHeld",double(tasks.returned_held));output->SetDouble("nativeTasksAdmitted",double(tasks.admitted));output->SetDouble("nativeTasksReturned",double(tasks.returned));output->SetDouble("nativeTasksDestroyed",double(tasks.destroyed));output->SetDouble("nativeTasksNeverExecuted",double(tasks.never_executed));output->SetBool("nativeTasksPoisoned",tasks.poisoned);
    const auto allocator=suitecut::InspectNativeGlibcMemory();
    output->SetBool("nativeGlibcAllocatorStatsAvailable",allocator.available);
    if(allocator.available){output->SetDouble("nativeGlibcArenaBytes",double(allocator.arena_bytes));output->SetDouble("nativeGlibcAllocatedBytes",double(allocator.allocated_bytes));output->SetDouble("nativeGlibcFreeBytes",double(allocator.free_bytes));output->SetDouble("nativeGlibcMmapBytes",double(allocator.mmap_bytes));output->SetDouble("nativeGlibcTopReleasableBytes",double(allocator.top_releasable_bytes));}
    const auto receipts=inspector_work_->Inspect();
    output->SetInt("nativeAcceptedInspectorChannels",int(receipts.channels));output->SetInt("nativeAcceptedInspectorQueued",int(receipts.queued));output->SetInt("nativeAcceptedInspectorSubmitted",int(receipts.submitted));output->SetInt("nativeAcceptedInspectorCallbacks",int(receipts.callbacks));output->SetBool("nativeAcceptedInspectorPoisoned",receipts.poisoned);output->SetBool("nativeAcceptedInspectorReceiptLaneReady",inspector_work_->ReceiptLaneReady());
    output->SetString("nativeAcceptedInspectorAccepted",std::to_string(receipts.accepted));output->SetString("nativeAcceptedInspectorReplies",std::to_string(receipts.replied));output->SetString("nativeAcceptedInspectorRefused",std::to_string(receipts.refused));output->SetString("nativeAcceptedInspectorUnwound",std::to_string(receipts.unwound));
    output->SetInt("nativeRequestBodyPhysical",int(suitecut::NativeRequestBodyReads::Physical()));
    output->SetInt("nativePrivateBodyQueuedReads",int(suitecut::NativePrivateResponseSession::Waiting()));output->SetInt("nativePrivateBodyInnerReceipts",int(suitecut::NativePrivateResponseSession::Physical()));output->SetInt("nativePrivateBodySessionReservations",int(suitecut::NativePrivateResponseSession::Sessions()));output->SetInt("nativePrivateBodyRecords",int(suitecut::NativePrivateResponseSession::Records()));
    output->SetInt("nativeImageStreamPhysical",int(suitecut::NativeResponseStreams::Physical()));output->SetDouble("nativeImageStreamRetainedBytes",double(suitecut::NativeResponseStreams::RetainedBytes()));output->SetInt("nativeImageStreamEntries",int(suitecut::NativeResponseStreams::Entries()));
    output->SetInt("nativeContextUnroutePending",int(context_unroute_all_->pending()));output->SetInt("nativeContextRoutePhysical",int(context_routes_->physical()));output->SetInt("nativeContextRoutePending",int(context_routes_->pending()));output->SetInt("nativeContextRouteTargets",int(context_routes_->targets()));output->SetDouble("nativeContextRouteMetadataBytes",double(suitecut::NativeContextRouteRegistration::retained_bytes()));
    output->SetInt("nativeContextHARPhysical",int(context_har_->physical()));output->SetInt("nativeContextHARTargets",int(context_har_->targets()));output->SetInt("nativeContextHARPending",int(context_har_->pending()));output->SetDouble("nativeContextHARPolicyBytes",double(context_har_->retained_bytes()));output->SetInt("nativeHARFilePhysical",int(har_files_->Physical()));output->SetInt("nativeHARCommitPhysical",int(har_recording_->Physical()));output->SetDouble("nativeHARRetainedBytes",double(network_archive_->retained_bytes()));output->SetDouble("nativeHARPageMetadataBytes",double(network_archive_->page_metadata_bytes()));output->SetDouble("nativeHARReplayBytes",double(suitecut::NativeHARReplayFiles::RetainedBytes()));
    http_->Diagnostics(output); downloads_->Diagnostics(output); workers_->Diagnostics(output);traces_->Diagnostics(output);output->SetInt("nativeTraceDOMPhysical",int(trace_dom_physical_));output->SetInt("nativeTraceProtocolPhysical",int(trace_physical_));output->SetInt("nativeTracePreviewPhysical",int(trace_preview_->PhysicalCount()));output->SetInt("nativeTraceSourcesPhysical",int(trace_sources_->PhysicalCount()));output->SetInt("nativeTraceBackgroundPhysical",int(trace_background_.PhysicalCount()));output->SetInt("nativeTraceBackgroundFailures",int(trace_background_.Failures()));
    output->SetInt("nativeTraceBackgroundWorkerLimit",int(trace_background_.WorkerLimit()));output->SetInt("nativeTraceBackgroundWorkers",int(trace_background_.WorkerCount()));
    output->SetDouble("nativeTraceBackgroundCapacity",double(trace_background_.Capacity()));output->SetDouble("nativeTraceBackgroundWorkerStartFailures",double(trace_background_.WorkerStartFailures()));
    output->SetInt("nativeArtifactInputReceipts",int(ArtifactInputReceipts()));
    output->SetInt("nativeArtifactInputRetained",int(screenshot_inputs_->RetainedCount()));
    output->SetInt("nativeScreenshotInputLogical",int(screenshot_inputs_->LogicalCount()));output->SetInt("nativeScreenshotInputPhysical",int(screenshot_inputs_->PhysicalCount()));
    output->SetInt("nativeScreenshotComparisonLogical",int(screenshot_comparisons_->LogicalCount()));output->SetInt("nativeScreenshotComparisonPhysical",int(screenshot_comparisons_->PhysicalCount()));output->SetDouble("nativeScreenshotComparisonBytes",double(screenshot_comparisons_->Bytes()));
    output->SetInt("nativeContextVideoPhysical",static_cast<int>(context_video_->Physical()));
    output->SetInt("nativeRecordingPhysical", static_cast<int>(recordings_->PhysicalCount()));
    output->SetInt("nativeRecordingMetadata", static_cast<int>(recordings_->MetadataCount()));
#if defined(OS_LINUX)
    output->SetInt("nativeStreamingPhysical", static_cast<int>(streaming_->PhysicalCount()));
    output->SetInt("nativeStreamingMetadata", static_cast<int>(streaming_->MetadataCount()));
#endif
  }
 private:
  Writer& writer_;
#if defined(__linux__)
  struct NativeWorkBarrier {
    suitecut::runner::RetainedSourceWorkWire::Challenge challenge;
    int command=0;suitecut::NativeStreamCut reader{};
    std::optional<suitecut::runner::RetainedSourceWorkWire::Cuts> cuts;
    bool observed=false,granted=false,refused=false;uint64_t resume_generation=0;
  };
  suitecut::runner::RetainedSourceEndpoint* native_work_endpoint_=nullptr;
  std::unique_ptr<suitecut::AcceptedEngineWorkWake> native_work_wake_;
  std::optional<NativeWorkBarrier> native_work_;
  uint64_t native_work_nonce_=0,native_work_generation_=0;
  bool native_work_installed_=false,native_work_posted_=false;
#endif
  std::shared_ptr<suitecut::AcceptedInspectorWork> inspector_work_=std::make_shared<suitecut::AcceptedInspectorWork>();
  std::shared_ptr<suitecut::AcceptedReaderWork> reader_work_=std::make_shared<suitecut::AcceptedReaderWork>();
  int width_, height_, fps_;
  double scale_;
  bool i420_, closing_ = false, deadline_timer_active_ = false;
  std::map<int, CefRefPtr<Source>> sources_;
  std::map<int,CefRefPtr<Source>> locator_handler_leases_;
  uint64_t locator_handler_invocation_=0;
  struct LocatorHandlerCommand {suitecut::NativeWorkOrigin origin;int page=0;uint64_t generation=0;};
  std::map<int,LocatorHandlerCommand> locator_handler_commands_;
  CefRefPtr<Source> reader_source_;
  std::shared_ptr<SuiteCutContextControls> contexts_;
  std::shared_ptr<SuiteCutContextWorkers> workers_;
  std::shared_ptr<suitecut::TraceControls> traces_;
  std::shared_ptr<suitecut::TracePreview> trace_preview_;
  std::shared_ptr<suitecut::TraceSources> trace_sources_;
  size_t trace_physical_=0,trace_dom_physical_=0;
  std::map<int,std::pair<suitecut::TraceJournal::Lease,uint64_t>> trace_context_api_;
  suitecut::TraceBackground trace_background_;
  std::shared_ptr<suitecut::ScreenshotComparisons> screenshot_comparisons_;
  std::shared_ptr<suitecut::ScreenshotInputs> screenshot_inputs_;
  std::shared_ptr<SuiteCutUtilityBrowsers> utilities_;
  std::shared_ptr<SuiteCutHTTPControls> http_;
  std::shared_ptr<suitecut::DownloadControls> downloads_;
  std::shared_ptr<suitecut::RecordingControls> recordings_;
  std::shared_ptr<suitecut::NativeContextVideo> context_video_;
  struct VideoPreparation { uint64_t generation; suitecut::NativeContextVideo::Prepared value; };
  std::map<int,VideoPreparation> video_preparations_;
#if defined(OS_LINUX)
  std::shared_ptr<suitecut::StreamingControls> streaming_;
#endif
  std::unique_ptr<suitecut::NativeContextConsoleControls> context_console_;
  std::unique_ptr<suitecut::NativeContextNetworkControls> context_network_;
  std::unique_ptr<suitecut::NativeContextWorkerNetwork> worker_network_;
  suitecut::NativeContextNetworkPolicy network_policy_;
  std::unique_ptr<suitecut::NativeNetworkArchives> network_archive_;
  std::unique_ptr<suitecut::NativeWebSocketRoutes>websocket_routes_;
  std::unique_ptr<suitecut::NativeContextRoutes>context_routes_;
  std::unique_ptr<suitecut::NativeContextHARRoutes>context_har_;
  std::unique_ptr<suitecut::NativeContextUnrouteAll>context_unroute_all_;
  std::shared_ptr<suitecut::NativeHARRecording>har_recording_;
  std::shared_ptr<suitecut::NativeHARReplayFiles>har_files_;
  suitecut::NativeContextNetworkHeaders network_headers_;
  suitecut::ContextSettingsRegistry settings_;
  suitecut::NativeContextGeolocation geolocation_;
  suitecut::NativeContextAuthenticationControls authentication_;
  std::map<int,std::shared_ptr<suitecut::NativeNetworkFatalTeardown>> authentication_failures_;
  SuiteCutOperationRegistry operations_;
  SuiteCutOperationRegistry startup_operations_;
  SuiteCutSelectorRegistry selectors_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

class Source : public CefClient, public CefLifeSpanHandler, public CefRenderHandler,
               public CefAudioHandler, public CefLoadHandler, public CefRequestHandler,
               public CefDevToolsMessageObserver, public CefJSDialogHandler,
               public CefDownloadHandler, public CefContextMenuHandler, public CefDisplayHandler, public CefPermissionHandler, public CefDialogHandler, public CefFrameHandler {
  // Declared first so every other Source member is destroyed before its real
  // object lifetime is retired; constructor unwinding is balanced as well.
  suitecut::NativeSourceLifetimeReceipt native_source_lifetime_;
 public:
  Source(Writer& writer, Engine& engine, int page, int width, int height, double scale, bool i420)
      : writer_(writer), engine_(engine), page_id_(page), width_(width), height_(height), capture_width_(i420?((static_cast<int>(std::ceil(width*scale))+1)&~1):static_cast<int>(std::ceil(width*scale))), capture_height_(i420?((static_cast<int>(std::ceil(height*scale))+1)&~1):static_cast<int>(std::ceil(height*scale))), scale_(scale), i420_(i420), handle_namespace_(HandleNamespace()) {context_settings_=engine_.settings().Get(engine_.contexts()->ContextId(page_id_));authentication_snapshot_=engine_.authentication().Get(engine_.contexts()->ContextId(page_id_));initial_empty_document_=!engine_.contexts()->IsPopup(page_id_);initial_empty_ready_=!initial_empty_document_;settings_bootstrap_=initial_empty_document_;}
  friend class Engine;
  void StopReader() {
    { std::lock_guard<std::mutex> lock(command_mutex_); stopped_ = true; }
    command_capacity_.notify_all(); writer_.WakeReaders();
    if (reader_.joinable()) reader_.join();
  }
  void SendPacket(Dict header, std::vector<uint8_t> data = {}) {
    header->SetInt("pageId", page_id_); writer_.Send(header, std::move(data));
  }
  void Error(const char* message) {
    auto header = Header("error"); header->SetString("error", message); SendPacket(header);
  }
  CefRefPtr<CefFrameHandler> GetFrameHandler() override { return this; }
  void OnFrameCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) override {
    if(settings_bootstrap_&&!settings_committed_&&frame->IsMain()){
      if(!engine_.contexts()->CredentialReadyStore()->RegisterBootstrap(browser->GetIdentifier(),browser->GetHost()->GetRequestContext())){SettingsFail("Native initial document identity capacity exceeded");return;}
    }
    if (!engine_.contexts()->InitScriptFrameCreated(page_id_, frame)) {
      Error("Native context init script IPC capacity exceeded"); browser->GetHost()->CloseBrowser(true);
    }
  }
  void OnFrameDestroyed(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame) override {
    engine_.contexts()->InitScriptFrameDestroyed(page_id_, frame->GetIdentifier().ToString());
  }
  void OnFrameDetached(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame) override {
    engine_.contexts()->InitScriptFrameDestroyed(page_id_, frame->GetIdentifier().ToString());
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
      CefProcessId sender, CefRefPtr<CefProcessMessage> message) override {
    if(SettingsMessage(frame,sender,message))return true;
    if (engine_.contexts()->InitScriptMessage(page_id_, frame, sender, message)) return true;
    if (sender == PID_RENDERER && message->GetName() == "suitecutInitScriptBootstrapError") {
      if (closing_page_ || !browser_) return true;
      Error(message->GetArgumentList()->GetString(0).ToString().c_str());
      if (browser_) browser_->GetHost()->CloseBrowser(true);
      return true;
    }
    if (sender == PID_RENDERER && message->GetName() == "suitecutInitScriptError") {
      ConsoleEvent("error", message->GetArgumentList()->GetString(0).ToString(), nullptr); return true;
    }
    return false;
  }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
  bool OnFileDialog(CefRefPtr<CefBrowser>, FileDialogMode mode, const CefString&, const CefString&,
      const std::vector<CefString>&, const std::vector<CefString>&, const std::vector<CefString>&,
      CefRefPtr<CefFileDialogCallback> callback) override { return FormFileDialog(mode, callback); }
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString& origin,
      uint32_t requested, CefRefPtr<CefPermissionPromptCallback> callback) override {
    const auto granted = engine_.contexts()->AllowedPermissions(page_id_, origin.ToString());
    callback->Continue(suitecut::NativePermissionRequestGranted(requested,granted) ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
    return true;
  }
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString& origin,
      uint32_t requested, CefRefPtr<CefMediaAccessCallback> callback) override {
    callback->Continue(requested & engine_.contexts()->AllowedMediaPermissions(page_id_, origin.ToString()));
    return true;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  bool DoClose(CefRefPtr<CefBrowser> browser) override {
    return engine_.contexts()->BeforeUnloadCloseApproved(page_id_,browser);
  }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, const CefString& address) override {
    if (!ready_ || !frame->IsMain()) return;
    const auto url = address.ToString();
    if (url.size() > 8192) { Error("Native address exceeds 8 KiB"); return; }
    auto header = Header("navigation");
    header->SetString("url", url);
    SendPacket(header);
  }
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override { return engine_.downloads()->CanDownload(page_id_); }
  bool OnBeforeDownload(CefRefPtr<CefBrowser>,CefRefPtr<CefDownloadItem> item,const CefString& name,CefRefPtr<CefBeforeDownloadCallback> callback) override { return engine_.downloads()->Before(page_id_,item,name.ToString(),callback); }
  void OnDownloadUpdated(CefRefPtr<CefBrowser>,CefRefPtr<CefDownloadItem> item,CefRefPtr<CefDownloadItemCallback> callback) override { engine_.downloads()->Updated(page_id_,item,callback); }
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> request,bool navigation,bool download,const CefString&,bool&) override {
    if(std::getenv("SUITECUT_NATIVE_RESOURCE_DIAGNOSTICS")) {
      const auto data=request->GetPostData();CefPostData::ElementVector elements;if(data)data->GetElements(elements);
      std::fprintf(stderr,"native cef resource page=%d browser=%d id=%llu type=%d frame=%s method=%s url=%s excluded=%d elements=%zu\n",page_id_,browser?browser->GetIdentifier():0,static_cast<unsigned long long>(request->GetIdentifier()),request->GetResourceType(),frame?frame->GetIdentifier().ToString().c_str():"",request->GetMethod().ToString().c_str(),request->GetURL().ToString().c_str(),data?data->HasExcludedElements():false,elements.size());
      for(size_t i=0;i<std::min(elements.size(),size_t(16));++i)std::fprintf(stderr,"native cef post element index=%zu type=%d bytes=%zu\n",i,elements[i]->GetType(),elements[i]->GetBytesCount());
    }
    if (auto icon = suitecut::HeadlessBrowserIconHandler(CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-headless") == "1", request)) return icon;
    return engine_.downloads()->ResourceHandler(page_id_,frame,request,navigation,download);
  }
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString& origin, JSDialogType type, const CefString& message,
                  const CefString& default_value, CefRefPtr<CefJSDialogCallback> callback, bool& suppress) override {
    const auto name = type == JSDIALOGTYPE_ALERT ? "alert" : type == JSDIALOGTYPE_CONFIRM ? "confirm" : "prompt";
    if (dialogs_ && dialogs_->Show(name, origin, message, default_value, callback, Now())) {
      suppress = false; WakeDeadlines(); return true;
    }
    suppress = true;
    return false;
  }
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString& message, bool,
                            CefRefPtr<CefJSDialogCallback> callback) override {
    const auto token=engine_.contexts()->BeforeUnloadCloseToken(page_id_);
    SuiteCutDialogControls::Decision decision;
    if(token)decision=[self=CefRefPtr<Source>(this),token](bool accept){
      if(!accept)self->engine_.contexts()->BeforeUnloadCloseCanceled(self->page_id_,token);
    };
    if (dialogs_ && dialogs_->Show("beforeunload", browser_ ? browser_->GetMainFrame()->GetURL().ToString() : "",
                                 message, "", callback, Now(),std::move(decision))) { WakeDeadlines(); return true; }
    callback->Continue(true, CefString());
    return true;
  }
  void OnResetDialogState(CefRefPtr<CefBrowser>) override { if (dialogs_) dialogs_->Clear(); }
  void OnDialogClosed(CefRefPtr<CefBrowser>) override { if (dialogs_) dialogs_->Closed(); }
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                           CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) override {
    model->Clear();
  }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
    rect = CefRect(0, 0, artifact_view_owner_ ? artifact_view_width_ : width_, artifact_view_owner_ ? artifact_view_height_ : height_);
    if (!artifact_view_owner_) { reported_view_width_ = rect.width; reported_view_height_ = rect.height; }
  }
  bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override {
    info.device_scale_factor = static_cast<float>(scale_);
    const auto screen=context_settings_&&context_settings_->screen?*context_settings_->screen:suitecut::ContextSettings::Size{width_,height_};
    info.rect = info.available_rect = CefRect(0, 0, screen.width, screen.height);
    return true;
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    const suitecut::NativeWorkOriginFrame native_creation(native_creation_origin_.Origin());
    browser_ = browser;NativeInspectorOpened(engine_.AcceptedInspector(),browser->GetIdentifier(),suitecut::AcceptedInspectorWork::Kind::Page);
    const auto native_inspector_callback=NativeInspectorCallback();
    NativeDebuggerDomains(); // Install before settings and author bootstrap can release.
    const auto policy_error=engine_.contexts()->RequestPolicyError(engine_.contexts()->ContextId(page_id_));
    if(!policy_error.empty()){Error(policy_error.c_str());browser->GetHost()->CloseBrowser(true);return;}
    CredentialPrepare();
    dialogs_ = std::make_unique<SuiteCutDialogControls>([this](Dict event) {
      const auto json = Json(event); const auto sequence = ++event_sequence_;
      const auto name = event->GetString("state") == "closed" ? "dialogclosed" : "dialog";
      JournalEvent(sequence, name, json);
      auto header = Header("dialog"); header->SetDouble("sequence", static_cast<double>(sequence));
      SendPacket(header, std::vector<uint8_t>(json.begin(), json.end()));
      std::vector<int> waiters;
      for (const auto& [id, event] : event_waiters_) if (event == name) waiters.push_back(id);
      for (const int id : waiters) Complete(id, true, "", json);
    });
    sockets_=std::make_unique<suitecut::NativeWebSocketControls>(
      [this](Dict info,const std::vector<uint8_t>& bytes){auto header=Header("websocket");header->SetInt("pageId",page_id_);header->SetDictionary("socket",info->Copy(false));const bool accepted=writer_.TryEvent(header,bytes);if(accepted)WebSocketPredicatePublished(info);return accepted;},
      [this]{WakeDeadlines();},[this](const std::string&error){Error(error.c_str());if(browser_)browser_->GetHost()->CloseBrowser(true);});
    sockets_->SetObserver([this](Dict event,const std::vector<uint8_t>&bytes,double observed){NativeTraceWebSocket(event,bytes,observed);});
    network_ = std::make_unique<suitecut::NetworkControls>(
      [this](const std::string& session,const std::string& method,Dict params,suitecut::NetworkControls::Result callback){NativeNetworkSend(session,method,params,std::move(callback),0);},
      [this](Dict info) {
        auto header = Header("network"); const auto json = Json(info);
        const auto event = info->GetString("event").ToString();
        if(event=="route"||event=="routematch"){
          if(ContextWorkerNetworkEvent(info))return;
          engine_.ContextRoute(engine_.contexts()->ContextId(page_id_),"page:"+std::to_string(page_id_),page_id_,nullptr,info);
        }
        if (event == "request" || event == "response" || event == "requestfinished" || event == "requestfailed") {
          if(ContextWorkerNetworkEvent(info))return;
          engine_.ContextNetwork(page_id_,info);
          auto request=info->GetDictionary("request");if(request && request->GetBool("serviceWorker"))return;
          const auto sequence = ++event_sequence_;
          header->SetDouble("sequence", static_cast<double>(sequence));
          JournalEvent(sequence, event, json);
        }
        auto request=info->GetDictionary("request");if(request && request->GetBool("serviceWorker"))return;
        SendPacket(header, std::vector<uint8_t>(json.begin(), json.end()));
      },
      [this](const std::string& error) { NetworkFatal(error); },
      [this] { if (!deadline_timer_active_) { deadline_timer_active_ = true; CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()), 10); } },
      [] { return Now(); },
      [this](uint64_t id,size_t offset,size_t total,std::span<const uint8_t> bytes,bool canceled) {
        auto header=Header("networkbody");header->SetInt("pageId",page_id_);header->SetString("bodyId",std::to_string(id));
        header->SetInt("offset",static_cast<int>(offset));header->SetInt("totalBytes",static_cast<int>(total));if(canceled)header->SetBool("canceled",true);
        return writer_.TryEvent(header,std::vector<uint8_t>(bytes.begin(),bytes.end()));
      });
    network_->SetBoundedSender([this](const std::string&session,const std::string&method,Dict params,suitecut::NetworkControls::Result done,double deadline){NativeNetworkSend(session,method,params,std::move(done),deadline);});
    network_->SetRouteFetch([http=engine_.http(),page=page_id_](int owner,const std::string&,const std::string&,Dict request,Dict options,double deadline,std::shared_ptr<void> lease,std::function<bool(bool,Dict,const std::string&)> done){http->RouteFetch(owner,page,request,options,std::move(done),deadline,std::move(lease));},
      [http=engine_.http()](int owner){http->Cancel(owner);});
    network_->SetSessionHandoff([this](const std::string&request,const std::string&previous,const std::string&session){NativeFrameRequestSession(request,previous,session);},
      [this](const std::string&frame,const std::string&root){return Descendant(frame,root);});
    const int archive_context=engine_.contexts()->ContextId(page_id_);network_context_id_=archive_context;const auto archive_owner="page:"+std::to_string(page_id_);
    if(!engine_.network_archive().Attach(archive_context,archive_owner,[self=CefRefPtr<Source>(this)](const std::string&session,const std::string&request,double deadline,suitecut::NativeHARCapture::BodyReply reply){self->NativeReadResponseBody(session,request,deadline,std::move(reply));})){
      Error("Native network archive owner capacity exceeded");browser->GetHost()->CloseBrowser(true);return;
    }
    if(!engine_.network_archive().PageCreated(archive_context,archive_owner,"page@"+std::to_string(page_id_),std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count())){NetworkFatal("Native HAR actual page metadata capacity exceeded");return;}
    if(!engine_.worker_network().Attach(archive_context,archive_owner,false,
      [self=CefRefPtr<Source>(this)](int owner,Dict command,double deadline,suitecut::NativeContextWorkerNetwork::Reply reply){
        if(self->closing_page_||!self->network_||(deadline&&Now()>=deadline)){reply(false,nullptr,"Actual page network observation channel is closed or deadline expired");return;}
        self->network_->Command(owner,command,[reply=std::move(reply)](bool ok,const std::string&error,const std::string&json){auto value=ok?CefParseJSON(json,JSON_PARSER_RFC):nullptr;reply(ok,value?value->GetDictionary():nullptr,error);},deadline,suitecut::NativeContextWorkerNetwork::RouteOperationGeneration(command),self->network_context_id_);self->WakeDeadlines();
      },
      [self=CefRefPtr<Source>(this)](const std::string&session,const std::string&request,double deadline,suitecut::NativeHARCapture::BodyReply reply){self->NativeReadResponseBody(session,request,deadline,std::move(reply));},
      [self=CefRefPtr<Source>(this)](int owner){if(self->network_)self->network_->Cancel(owner);})){
      NetworkFatal("Native page worker network observer capacity exceeded");return;
    }
    network_->SetObserver([this,archive_context,archive_owner](Dict event){
      const auto kind=event->GetString("event");if(kind!="request"&&kind!="requestupdated"&&kind!="response"&&kind!="requestfinished"&&kind!="requestfailed")return;
      if(!engine_.network_archive().Observe(archive_context,archive_owner,event,0)){Error("Native network archive metadata rejected");}if(engine_.network_archive().NeedsTick())engine_.WakeTrace();
    },[this,archive_context]{return engine_.network_archive().Active(archive_context);});
    network_->SetRequestBodyReader(
      [this](const std::string&session,const std::string&request,double deadline,suitecut::NativeNetworkPostPreparation::Data done,suitecut::NativeNetworkPostPreparation::Receipt receipt){NativeReadRequestBody(session,request,deadline,std::move(done),std::move(receipt));},
      [this,archive_context,archive_owner](const std::string&id){auto reserved=engine_.network_archive().ReservePost(archive_context,archive_owner,id);if(engine_.network_archive().NeedsTick())engine_.WakeTrace();return suitecut::NativeNetworkPostPreparation::Reservation{std::move(reserved.data),std::move(reserved.receipt)};});
    bindings_ = std::make_unique<suitecut::BindingControls>(
      std::to_string(page_id_) + "_" + handle_namespace_,
      [this](const std::string& session, const std::string& method, Dict params, suitecut::BindingControls::Result callback) {
        const int id = AllocateDevToolsId();
        if (!id || binding_calls_.size() >= 1024 || closing_page_) { callback(false, "Native binding command capacity exceeded or page closed"); return; }
        binding_calls_.emplace(id, BindingCall{session, suitecut::BindNativeLocatorHandlerAsync(std::move(callback)), Now() + 30000});
        if (!SubmitDevTools(id, method, params, session)) {
          const auto found = binding_calls_.find(id);
          if (found != binding_calls_.end()) { auto callback = std::move(found->second.callback); binding_calls_.erase(found); callback(false, "Native binding command dispatch failed"); }
        }
        if (!deadline_timer_active_) { deadline_timer_active_ = true; CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()), 10); }
      },
      [this](int owner, const suitecut::BindingControls::Target& target, Dict message, Dict params, suitecut::BindingControls::Result callback) {
        const auto frame = frame_states_.find(target.frame);
        if (frame == frame_states_.end() || frame->second.context != target.context || frame->second.session != target.session) { callback(false,"Native binding context was destroyed"); return; }
        operation_frames_[owner] = target.frame; operation_sessions_[owner] = target.session;
        auto config = message->Copy(false); config->SetString("frame", target.frame);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(owner));
        evaluation_groups_[owner].insert(target.session);
        HandleArguments(owner, config, params, true, [this, owner, target, params, callback = std::move(callback)]() mutable {
          const auto frame = frame_states_.find(target.frame);
          if (frame == frame_states_.end() || frame->second.context != target.context || frame->second.session != target.session) { callback(false,"Native binding context was destroyed"); return; }
          DevTools(owner,"Runtime.callFunctionOn",params,[callback = std::move(callback)](bool ok,const std::string& json) {
            auto parsed = CefParseJSON(json,JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
            callback(ok && result && !result->HasKey("exceptionDetails"),json);
          });
        });
      },
      [this](Dict info) {
        if (info->GetString("event") == "call") {
          const auto metadata = info->GetDictionary("frame");
          const auto frame = metadata ? frame_states_.find(metadata->GetString("id")) : frame_states_.end();
          if (frame != frame_states_.end()) info->SetDictionary("frame", FrameJson(frame->second));
        }
        auto header = Header("binding"); const auto json = Json(info); SendPacket(header,std::vector<uint8_t>(json.begin(),json.end()));
      },
      [this] { if (!deadline_timer_active_) { deadline_timer_active_ = true; CefPostDelayedTask(TID_UI,new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()),10); } },
      [] { return Now(); });
    std::string inherited_error;
    if (!engine_.contexts()->SeedPageBindings(page_id_,inherited_error)) { Error(inherited_error.c_str()); browser->GetHost()->CloseBrowser(true); return; }
    if(!engine_.PageCreated(page_id_, browser))return;
    browser->GetHost()->SetFocus(true);
    native_frame_debugger_registering_=true;
    observer_ = browser->GetHost()->AddDevToolsMessageObserver(this);
    if(!NativeFrameDebuggerObserved())return;
    NativeWorkerDebuggerObserved();
    bootstrap_tree_=AllocateDevToolsId();
    if(!NativeFrameDebuggerTreePrepare(bootstrap_tree_,""))return;
    NativeWorkerTargetDiscoveryStart();
    NativeStartPrivateResponseSession();
    CredentialStart();
    NativePristineStart();
    SettingsPrepare();
    EnableSession("");
    worker_websocket_attach_=[this](const std::string&session,const std::string&,const std::string&,double,suitecut::NativeWebSocketRoutes::Result done){NativeWebSocketPolicyAttach(session,std::move(done));};
    worker_websocket_detach_=[this](const std::string&session){NativeWebSocketPolicyDetach(session);};
    worker_websocket_required_=[this]{return engine_.websocket_routes().HasPolicy(engine_.contexts()->ContextId(page_id_),page_id_);};
    worker_websocket_failure_=[this](const std::string&error){NetworkFatal("Inherited native WebSocket worker policy failed: "+error);};
    NativeWebSocketPolicyAttach("",[this](bool ok,const std::string&error){if(!ok)NetworkFatal(error);});
    if(!engine_.network_policy().Attach(engine_.contexts()->ContextId(page_id_),"page:"+std::to_string(page_id_),
      [self=CefRefPtr<Source>(this)](bool offline,suitecut::NativeContextNetworkPolicy::Result done){
        if(self->closing_page_ || !self->network_){done(false,"Native policy target is closed");return;}
        auto params=CefDictionaryValue::Create();params->SetString("action","setOffline");params->SetBool("offline",offline);
        self->network_->Command(0,params,[done=std::move(done)](bool ok,const std::string& error,const std::string&){done(ok,error);});
      },[self=CefRefPtr<Source>(this)](bool ok,const std::string& error){
        if(self->closing_page_)return;
        if(!ok){self->Error(error.c_str());if(self->browser_)self->browser_->GetHost()->CloseBrowser(true);return;}
        self->NativeStartContextHeaders();
      })) {Error("Native network policy target capacity exceeded");browser->GetHost()->CloseBrowser(true);return;}
    if(!SubmitDevTools(bootstrap_tree_, "Page.getFrameTree", nullptr, "")){NativeFrameDebuggerMetadataFailure("Native debugger initial root tree dispatch was not accepted");return;}
    AutoAttach("");
  }
  void NativeStartContextHeaders() {
    if(closing_page_||!browser_)return;
    if(!engine_.network_headers().Attach(engine_.contexts()->ContextId(page_id_),"page:"+std::to_string(page_id_),
      [self=CefRefPtr<Source>(this)](Dict headers,suitecut::NativeContextNetworkHeaders::Result done){
        if(self->closing_page_||!self->network_){done(false,"Native HTTP headers target is closed");return;}
        auto params=CefDictionaryValue::Create();params->SetString("action","setContextExtraHTTPHeaders");params->SetDictionary("headers",headers->Copy(false));
        self->network_->Command(0,params,[done=std::move(done)](bool ok,const std::string&error,const std::string&){done(ok,error);});
      },[self=CefRefPtr<Source>(this)](bool ok,const std::string&error){
        if(self->closing_page_)return;
        if(!ok){self->Error(error.c_str());if(self->browser_)self->browser_->GetHost()->CloseBrowser(true);return;}
        self->NativeStartContextHAR();
      })){Error("Native context HTTP headers target capacity exceeded");browser_->GetHost()->CloseBrowser(true);}
  }
  void NativeStartContextHAR(){
    if(closing_page_||!browser_)return;
    if(!engine_.har_routes().Attach(engine_.contexts()->ContextId(page_id_),"page:"+std::to_string(page_id_),
      [self=CefRefPtr<Source>(this)](suitecut::NativeContextHARRoutes::Mutation mutation,const suitecut::NativeContextHARRoutes::Route&route,double deadline,suitecut::NativeContextHARRoutes::Result done){
        if(self->closing_page_||!self->network_){done(false,"Actual Source HAR policy target is closed");return;}
        if(mutation==suitecut::NativeContextHARRoutes::Mutation::Install)self->network_->BindReplayPolicy(route.id,route.pattern,route.policy,std::move(done),deadline,true);
        else self->network_->UnbindReplayPolicy(route.id,route.policy,std::move(done),deadline);
      },[self=CefRefPtr<Source>(this)](bool ok,const std::string&error){if(self->closing_page_)return;if(!ok){self->NetworkFatal(error);return;}self->NativeStartContextRoutes();}
    ))NetworkFatal("Native Source context HAR policy capacity exceeded");
  }
  #include "context_route_source.inc"
  bool ContextWorkerNetworkEvent(Dict event){
    auto request=event?(event->GetString("event")=="routematch"?event:event->GetDictionary("request")):nullptr;if(!request)return false;
    const auto worker=request->GetDictionary("workerInfo");if(!worker||!request->GetBool("contextWorker"))return false;
    engine_.ContextWorkerNetwork(network_context_id_,"page:"+std::to_string(page_id_),worker,event);return true;
  }
  void NativeStartPrivateResponseSession(){
    private_response_=std::make_unique<suitecut::NativePrivateResponseSession>(
      [this](const std::string&method,Dict params,double deadline,suitecut::NativePrivateResponseSession::Reply done){NativeNetworkSend("",method,params,std::move(done),deadline);},
      []{return Now();},[this]{WakeDeadlines();},[this](const std::string&error){NetworkFatal(error);});
    if(!private_response_->Start(Now()+30000,[this](bool ok,const std::string&error){
      if(closing_page_)return;
      private_response_ready_=true;if(!ok)private_response_error_=error.substr(0,512);MaybeReady();
    })){private_response_.reset();private_response_ready_=true;private_response_error_="Private body session reservation capacity exceeded";}
  }
  static bool NativePrivateBodyFallback(bool actual_ok,const std::string&actual_json){
    if(!actual_ok)return actual_json.size()<=65536;
    const auto parsed=CefParseJSON(actual_json,JSON_PARSER_RFC);const auto value=parsed?parsed->GetDictionary():nullptr;
    return value&&value->GetType("body")==VTYPE_STRING&&value->GetType("base64Encoded")==VTYPE_BOOL&&value->GetString("body").empty();
  }
  std::string NativePrimaryBodyError(int owner,const std::string&json){
    const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto response=parsed?parsed->GetDictionary():nullptr;
    std::string error=response&&response->GetType("message")==VTYPE_STRING?response->GetString("message").ToString():"Native DevTools command failed";
    const auto read=body_waiters_.find(owner);
    if(read!=body_waiters_.end()&&!read->second.loader.empty()){
      const auto frame=frame_states_.find(read->second.frame);
      if(frame!=frame_states_.end()&&!frame->second.loader.empty()&&frame->second.loader!=read->second.loader)error+=" (response frame has navigated away)";
    }
    return error;
  }
  void NativeImageStreamEvent(const std::string&method,Dict actual,const std::string&session) {
    if(!image_streams_)image_streams_=std::make_unique<suitecut::NativeResponseStreams>(
      [this](const std::string&session,const std::string&request,double deadline,suitecut::NativeResponseStreams::Result done){auto params=CefDictionaryValue::Create();params->SetString("requestId",request);NativeNetworkSend(session,"Network.streamResourceContent",params,std::move(done),deadline);},
      []{return Now();},[this]{WakeDeadlines();});
    image_streams_->Event(method,actual,session);
  }
  void NativeReadResponseBody(const std::string& session,const std::string& request,double deadline,suitecut::NativeResponseBodyReads::Reply reply) {
    if(closing_page_||!browser_){reply(false,{},"Actual native response-body Source is closed");return;}
    if(!response_reads_)response_reads_=std::make_unique<suitecut::NativeResponseBodyReads>(
      [this](const std::string& target,const std::string& request,double original,suitecut::NativeResponseBodyReads::Result done){
        if(closing_page_||!browser_){done(false,"Actual native response-body Source is closed");return;}
        const int id=AllocateDevToolsId();if(!id||network_calls_.size()>=1024){done(false,"Native response-body command capacity exceeded");return;}
        const double physical_deadline=original?original:Now()+30000;
        network_calls_.emplace(id,NetworkCall{target,[this,target,request,physical_deadline,done=std::move(done)](bool ok,const std::string&json){
          const auto result=image_streams_?image_streams_->Resolve(target,request,ok,json):std::pair<bool,std::string>{ok,json};
          if(target.empty()&&private_response_&&NativePrivateBodyFallback(result.first,result.second)&&private_response_->Read(request,physical_deadline,[this,done,result](bool recovered,const std::string&actual){if(closing_page_||!browser_){done(false,closed_reason_.empty()?"Native page is closed":closed_reason_);return;}done(recovered?true:result.first,recovered?actual:result.second);} ))return;
          done(result.first,result.second);
        },physical_deadline});WakeDeadlines();
        auto params=CefDictionaryValue::Create();params->SetString("requestId",request);
        if(!SubmitDevTools(id,"Network.getResponseBody",params,target)){
          const auto found=network_calls_.find(id);if(found!=network_calls_.end()){auto callback=std::move(found->second.callback);network_calls_.erase(found);callback(false,"CEF rejected actual response-body read");}
        }
      },[]{return Now();},[this]{WakeDeadlines();});
    // HAR owns logical timeout. This callback settles only on actual CDP ACK
    // or real channel close, preserving native physical-read accounting.
    response_reads_->Read(session,request,deadline,std::move(reply));
  }
  void NativeReadRequestBody(const std::string&session,const std::string&request,double deadline,suitecut::NativeRequestBodyReads::Reply reply,suitecut::NativeRequestBodyReads::Receipt receipt) {
    if(closing_page_||!browser_){if(receipt)receipt();return;}
    if(!request_reads_)request_reads_=std::make_unique<suitecut::NativeRequestBodyReads>(
      [this](const std::string&session,const std::string&request,double original,suitecut::NativeRequestBodyReads::Result done){auto params=CefDictionaryValue::Create();params->SetString("requestId",request);NativeNetworkSend(session,"Network.getRequestPostData",params,std::move(done),original);},
      []{return Now();},[this]{WakeDeadlines();});
    request_reads_->Read(session,request,deadline,std::move(reply),std::move(receipt));
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> actual_browser) override {
    const auto native_inspector_callback=NativeInspectorCallback();
    closing_page_ = true;
    SDKBrowserControlClosed();
    if(native_widget_mouse_)native_widget_mouse_->ChannelClosed();
    engine_.websocket_routes().DetachPrefix("page:"+std::to_string(page_id_)+"/ws:");
    NativeWebSocketPolicyClearScopes("Actual native WebSocket source channel closed");
    native_websocket_explicit_payloads_.Closed(engine_.contexts()->ContextId(page_id_),page_id_);
    CancelSettingsWatchdogs();
    ScreenshotAssertionChannelClosed();
    NativePristineClosed();
    native_virtual_time_close_fence_.Closed();
    if(native_virtual_time_)native_virtual_time_->Closed();
    if(element_states_)element_states_->Close();
    if(selector_waits_)selector_waits_->Close();
    OwnerFrameDocumentsClosed();
    if(snapshot_references_)snapshot_references_->Destroyed();
    if(page_errors_)page_errors_->Close();
    if(request_reads_){request_reads_->CancelAll();request_reads_->Close();}
    if(private_response_)private_response_->ActualClose();
    if(response_reads_)response_reads_->Close();
    if(image_streams_)image_streams_->Close();
    if(network_fatal_)network_fatal_->Closed();
    context_auth_open_=false;GeolocationClear();
    if(settings_page_){settings_page_->Clear();}
    settings_initial_request_=nullptr;
    engine_.recordings()->ClosePage(page_id_);
#if defined(OS_LINUX)
    engine_.streaming()->ClosePage(page_id_);
#endif
    CredentialClose();
    if(native_touch_)native_touch_->Close();
    NativeDebuggerClosed();
    NativeDeveloperClosed();
    NativeCoverageClosed();
    NativeHighlightClosed();
    engine_.screenshot_inputs()->PageClosed(page_id_);engine_.screenshot_comparisons()->PageClosed(page_id_);screenshot_comparison_samples_.clear();
    WorkerConsoleClear(true);
    NativeWorkerTargetOwnerClosed();WorkerClear();
    devtools_dispatch_.clear(); devtools_dispatch_bytes_ = 0;
    if (dialogs_) dialogs_->Clear();
    if (artifact_view_owner_) ArtifactReleaseView(artifact_view_owner_);
    if (artifacts_) artifacts_->Clear();
    NativeMediaClear();
    if(native_idle_)native_idle_->Close();
    if(sockets_)sockets_->Clear();
    if (network_) {network_->HARWorkChannelClosed();network_->Clear();}
    if (bindings_) bindings_->Clear();
    const auto close_error = closed_reason_.empty() ? std::string("Native page is closed") : closed_reason_;
    for (const int id : operations_.Ids()) {
      const auto receipt = expect_results_.find(id);
      auto value = receipt == expect_results_.end() ? nullptr : CefParseJSON(receipt->second, JSON_PARSER_RFC);
      auto snapshot = value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
      if (snapshot) {
        snapshot->SetString("closedReason", close_error);
        Complete(id, true, "", Json(snapshot));
      } else Complete(id, false, close_error);
    }
    if(!screenshot_assertion_retirement_.Size())operations_.Clear();
    auto binding_calls = std::move(binding_calls_); binding_calls_.clear();
    for (auto& [id, call] : binding_calls) call.callback(false, close_error);
    auto network_calls = std::move(network_calls_); network_calls_.clear();
    for (auto& [id, call] : network_calls) call.callback(false, call.expired?"Native network command deadline expired":close_error);
    manual_drag_ = nullptr; drag_work_.clear();
    form_clicks_.clear(); form_uploads_.clear(); upload_store_.Clear(); pending_uploads_.clear(); pending_upload_bytes_ = 0;
    if (artifact_view_owner_) ArtifactReleaseView(artifact_view_owner_);
    if (artifacts_) artifacts_->Clear();
    locator_attempts_.clear();
    locator_logs_.clear(); locator_waits_.clear();
    locator_modifiers_.clear();
    send_keys_physical_.clear(); send_keys_physical_count_ = 0; send_keys_cleanup_commands_.clear();
    input_work_.clear(); mouse_queue_.clear(); keyboard_queue_.clear(); pressed_keys_.clear();
    devtools_.clear(); NativeWorkerDebuggerClosed(); NativeDropClosed(); if(handle_disposals_)handle_disposals_->Close();
    context_retries_.clear(); artifact_configs_.clear(); artifact_attempts_.clear(); artifact_input_configs_.clear();
    count_queries_.clear(); count_contexts_.clear(); visibility_snapshots_.Clear();
    expect_results_.clear();
    evaluation_groups_.clear();
    navigation_waiters_.clear();
    navigations_.clear(); body_waiters_.clear(); active_requests_.clear(); documents_.clear(); document_order_.clear();
    ClearConsoleRawArguments(true); handles_.clear(); console_journal_.Clear(); page_error_journal_.Clear();
    pending_handle_groups_.clear();
    pending_child_bootstraps_.clear(); init_snapshot_requests_.clear(); init_snapshot_continuations_.clear(); init_snapshot_continuation_count_ = 0;
    frame_states_.clear();
    operation_frames_.clear();
    operation_sessions_.clear();
    sessions_.clear();
    native_target_parents_.clear();
    session_trees_.clear();
    utility_worlds_.clear();
    event_waiters_.clear();
    event_history_.clear();
    event_history_bytes_ = 0;
    native_file_selection_.reset();  // Actual OnBeforeClose retires any blocked native lane.
    folder_observers_.clear(); folder_operation_ = 0; folder_generation_ = 0;
    folder_path_.clear(); folder_continued_ = false;
    std::vector<uint8_t>().swap(view_); std::vector<uint8_t>().swap(popup_);
    frame_width_ = frame_height_ = popup_width_ = popup_height_ = 0;
    ready_ = show_popup_ = false; audio_valid_ = false;
    observer_ = nullptr;
    browser_ = nullptr;
    NativeTraceClear();NativeInspectorClosed(actual_browser->GetIdentifier());
    native_creation_handler_receipt_.reset();
    RendererRecoveryHandlerClosed();
    engine_.PageClosed(page_id_);
  }
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int popup_id, const CefString& target_url,
                     const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures&,
                     CefWindowInfo& window, CefRefPtr<CefClient>& client, CefBrowserSettings&,
                     CefRefPtr<CefDictionaryValue>& extra_info, bool*) override {
    if (closing_page_) return true;
    auto popup = engine_.PopupClient(page_id_, popup_id, target_url.ToString());
    if (!popup) return true;
    extra_info = engine_.BrowserInfo(popup->page_id_);
    window.SetAsWindowless(0); client = popup; return false;
  }
  void OnBeforePopupAborted(CefRefPtr<CefBrowser>, int popup_id) override { engine_.AbortPopup(page_id_, popup_id); }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList& dirty,
               const void* buffer, int width, int height) override {
    ScreenshotAssertionViewPaint(type,width,height);
    if (ArtifactViewPaint(type, width, height)) {
      (type == PET_VIEW ? view_damage_valid_ : popup_damage_valid_) = false;
      return;
    }
    const double captured = Now();
    if (width <= 0 || height <= 0 || width > 3840 || height > 2160) {
      (type == PET_VIEW ? view_damage_valid_ : popup_damage_valid_) = false;
      return;
    }
    if (type == PET_VIEW) {
      engine_.TracePaint(page_id_,width_,height_,captured,buffer,width,height);
      if (!SuiteCutUpdateCapture(view_, buffer, width, height,
          view_damage_valid_ ? frame_width_ : 0, frame_height_, dirty)) {
        view_damage_valid_ = false; return;
      }
      view_damage_valid_ = true;
      frame_width_ = width;
      frame_height_ = height;
      // Retain the native CSS view associated with this paint. Pixel/DPR
      // division cannot invert ceil(CSS*DPR), especially for tiny viewports.
      // A late paint from an older, differently sized surface is not video
      // input for the newly requested view; retain the last encoder frame.
      const double native_scale = static_cast<float>(scale_);
      const bool current_view = reported_view_width_ == width_ && reported_view_height_ == height_ &&
          width == std::ceil(reported_view_width_ * native_scale) &&
          height == std::ceil(reported_view_height_ * native_scale);
      frame_css_width_ = current_view ? reported_view_width_ : 0;
      frame_css_height_ = current_view ? reported_view_height_ : 0;
      ScreenshotComparisonPaint();
    } else {
      if (!SuiteCutUpdateCapture(popup_, buffer, width, height,
          popup_damage_valid_ ? popup_width_ : 0, popup_height_, dirty)) {
        popup_damage_valid_ = false; return;
      }
      popup_damage_valid_ = true;
      popup_width_ = width;
      popup_height_ = height;
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
    if (!audio_valid_) Error("Unsupported native audio format");
  }
  void OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** data, int frames, int64_t pts) override {
    if (!audio_valid_ || frames < 1 || frames > 4096) return;
    const double timestamp = audio_clock_.Map(static_cast<double>(pts), Now());
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
    engine_.recordings()->Audio(page_id_, pcm, timestamp);
#if defined(OS_LINUX)
    engine_.streaming()->Audio(page_id_, pcm, timestamp);
#endif
    auto header = Header("audio");
    header->SetDouble("timestampMs", timestamp);
    header->SetInt("frames", frames);
    SendPacket(header, std::move(pcm));
  }
  void OnAudioStreamStopped(CefRefPtr<CefBrowser>) override { audio_valid_ = false; }
  void OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString&) override { Error("Native audio capture failed"); }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int status) override {
    if(frame->IsMain()&&!settings_bootstrap_&&settings_page_&&!settings_started_){settings_empty_seen_=true;SettingsStart();}
    if (frame->IsMain()) {
      last_cef_load_url_ = frame->GetURL().ToString();
      const auto current = frame_states_.find(main_frame_id_);
      if (current != frame_states_.end() && current->second.url == last_cef_load_url_) {
        // The CEF client can attach after a popup's early CDP load events. This
        // real embedder load-end callback supplies the completed milestones.
        current->second.milestones.insert("commit"); current->second.milestones.insert("DOMContentLoaded"); current->second.milestones.insert("load");
        CheckNavigations();
      }
    }
    if (frame->IsMain() && !ready_) {
      // Handshake only after the initial blank document loads, so its load-end
      // cannot accidentally resolve the operator's first navigation command.
      initial_loaded_ = true;
      MaybeReady();
      return;
    }
    if (frame->IsMain() && navigation_) {
      Complete(navigation_, status < 400, status < 400 ? "" : "HTTP navigation failed");
    }
    if (frame->IsMain() && ready_) {
      auto response = CefDictionaryValue::Create();
      response->SetString("url", frame->GetURL());
      response->SetInt("status", status);
      const auto waiters = navigation_waiters_;
      for (const int id : waiters) Complete(id, true, "", Json(response));
    }
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                   const CefString&, const CefString&) override {
    if (frame->IsMain() && recovery_requested_ && code != ERR_ABORTED) {
      recovery_requested_ = false;
      RendererRecoveryHandlerFailed("Native handler renderer recovery navigation failed");
      std::vector<int> failed;
      for (const auto& [id, nav] : navigations_) if (nav.recovery) failed.push_back(id);
      for (const int id : failed) Complete(id,false,"Native renderer recovery navigation failed (error " + std::to_string(static_cast<int>(code)) + ")");
    }
    if (frame->IsMain() && navigation_ && code != ERR_ABORTED) {
      Complete(navigation_, false, "Native navigation failed");
    }
  }
  void OnTitleChange(CefRefPtr<CefBrowser>,const CefString&title)override{
    if(!closing_page_&&network_&&!engine_.network_archive().PageTitle(network_context_id_,"page:"+std::to_string(page_id_),title.ToString()))NetworkFatal("Native HAR actual page title capacity exceeded");
  }
  void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, TransitionType) override {
    if (frame->IsMain()) {
      for (const auto& [id, handle] : handles_) {
        auto params = CefDictionaryValue::Create(); params->SetString("objectGroup", handle.group);
        SendDevTools("Runtime.releaseObjectGroup", params, handle.session);
      }
      ClearConsoleRawArguments(true); handles_.clear();
    }
  }
  void OnRenderViewReady(CefRefPtr<CefBrowser>) override {
    ++renderer_views_; CheckRendererRecovery();
  }
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus, int, const CefString&) override {
    if(engine_.websocket_routes().HasPolicy(engine_.contexts()->ContextId(page_id_),page_id_)||engine_.websocket_routes().Owns(NativeWebSocketPolicyOwner(""))||native_routed_active_||native_routed_configuring_)NativeWebSocketPolicyLoseRoot("Native WebSocket renderer terminated; recreate the page because backend V1 has no authenticated agent handoff");
    NativeHARPrimaryReset();
    NativeFrameDebuggerRootRealmInvalidated();
    NativeDeveloperRealmDestroyed();
    // A clock owns the current document, not a recoverable renderer epoch.
    // Keep private Inspector receipts until actual OnBeforeClose settles them.
    engine_.contexts()->SnapshotRendererTerminated(page_id_);
    NativeVirtualTimeRendererTerminated();
    native_coverage_inventory_.Detached("");
    NativeCoverageRendererTerminated();
    engine_.screenshot_inputs()->PageClosed(page_id_);
    recovery_requested_ = false; failed_renderer_views_ = renderer_views_;
    RendererRecoveryHandlerFailed("Native renderer terminated during handler recovery");
    engine_.recordings()->ClosePage(page_id_);
#if defined(OS_LINUX)
    engine_.streaming()->FailPage(page_id_);
#endif
    NativeDeveloperChanged(); renderer_failed_ = true; NativePristineRendererFailed(); SnapshotReferenceRendererClosed(); NativeHighlightDetached(""); RememberFailedEpoch();
    const char* reason = "Browser renderer terminated";
    if (page_id_ == 1) {
      for (const int id : operations_.Ids()) Complete(id, false, reason);
      Error(reason);
    } else {
      ChildRendererCrash(reason);
      if (!ready_ && browser_) { closed_reason_ = reason; browser_->GetHost()->CloseBrowser(true); }
    }
  }
  bool OnDevToolsMessage(CefRefPtr<CefBrowser>, const void* message, size_t size) override {
    const auto native_inspector_callback=NativeInspectorCallback();
    DevToolsObserverScope observer(devtools_observer_depth_);
    if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000)
      std::fprintf(stderr, "native received: %.*s\n", static_cast<int>(std::min(size, size_t(2048))), static_cast<const char*>(message));
    if (size > kMaxResponseBytes + 65536) { Error("Native protocol message limit exceeded"); return true; }
    auto parsed = CefParseJSON(std::string(static_cast<const char*>(message), size), JSON_PARSER_RFC);
    auto data = parsed ? parsed->GetDictionary() : nullptr;
    if (!data) {
      // An actual Inspector reply can contain a JavaScript isolated UTF16
      // scalar that CEF dictionaries cannot represent. Parse a separate
      // validation projection only to recover the genuine envelope identity.
      // No projected role/name/value is ever forwarded as browser metadata.
      const auto original = std::string(static_cast<const char*>(message), size);
      const auto projection = suitecut::StorageJSONValidationProjection(original);
      if (projection != original) {
        const auto validated = CefParseJSON(projection, JSON_PARSER_RFC);
        const auto envelope = validated ? validated->GetDictionary() : nullptr;
        if (envelope && envelope->GetType("id") == VTYPE_INT &&
            (!envelope->HasKey("sessionId") || envelope->GetType("sessionId") == VTYPE_STRING)) {
          const int native_id=envelope->GetInt("id");const auto native_session=envelope->GetString("sessionId").ToString();
          const auto pending=devtools_.find(native_id);const auto trace=native_trace_protocol_.find(native_id);
          const bool trace_scalars=trace!=native_trace_protocol_.end()&&trace->second.session==native_session;
          const auto tree=session_trees_.find(native_id);
          const bool frame_scalars=(bootstrap_tree_&&native_id==bootstrap_tree_&&native_session.empty())||(tree!=session_trees_.end()&&tree->second==native_session);
          if((frame_scalars||trace_scalars||(pending!=devtools_.end()&&pending->second.session==native_session&&pending->second.lossless_scalars)||(page_errors_&&page_errors_->Accepts(native_id,native_session)))&&!envelope->HasKey("error")){
            std::string lossless,error;
            if(suitecut::DevToolsLosslessProjection(original,lossless,error)){
              auto typed=CefParseJSON(lossless,JSON_PARSER_RFC);auto packet=typed?typed->GetDictionary():nullptr;auto result=packet?packet->GetDictionary("result"):nullptr;
              if(result){const auto json=Json(result);ToolResult(native_id,true,json.data(),json.size(),native_session);return true;}
            }
          }
          constexpr char failure[] = R"({"message":"Native DevTools raw UTF16 scalar is unsupported"})";
          ToolResult(envelope->GetInt("id"), false, failure, sizeof(failure) - 1,
                     envelope->GetString("sessionId").ToString());
        }
      }
      if(projection!=original){
        const auto validation=CefParseJSON(projection,JSON_PARSER_RFC);const auto envelope=validation?validation->GetDictionary():nullptr;
        if(envelope&&!envelope->HasKey("id")&&(envelope->GetString("method")=="Page.frameNavigated"||envelope->GetString("method")=="Runtime.consoleAPICalled"||envelope->GetString("method")=="Runtime.exceptionThrown"||(native_coverage_&&(envelope->GetString("method")=="Debugger.scriptParsed"||envelope->GetString("method")=="CSS.styleSheetAdded"||envelope->GetString("method")=="Profiler.navigationCoverageFinalized"||envelope->GetString("method")=="CSS.navigationCoverageFinalized"))||(native_debugger_domains_&&(envelope->GetString("method")=="Debugger.scriptParsed"||envelope->GetString("method")=="Debugger.paused")))){
          std::string lossless,cause;
          if(!suitecut::DevToolsLosslessProjection(original,lossless,cause)){Error(cause.c_str());return true;}
          const auto parsed=CefParseJSON(lossless,JSON_PARSER_RFC);const auto event=parsed?parsed->GetDictionary():nullptr;const auto params=event?event->GetDictionary("params"):nullptr;
          if(!params){Error("Invalid native lossless runtime event");return true;}
          if(event->GetType("method")!=VTYPE_STRING||(event->HasKey("sessionId")&&event->GetType("sessionId")!=VTYPE_STRING)){Error("Invalid native lossless event identity");return true;}
          NativeDeveloperEvent(event->GetString("method").ToString(),params,event->GetString("sessionId").ToString());
          if(NativeDebuggerEvent(event->GetString("method").ToString(),params,event->GetString("sessionId").ToString()))return true;
          if(NativeCoverageEvent(event->GetString("method").ToString(),params,event->GetString("sessionId").ToString()))return true;
          const auto json=Json(params);ToolEvent(event->GetString("method"),json.data(),json.size(),event->GetString("sessionId").ToString());
        }
      }
      return true;
    }
    const auto session = data->GetString("sessionId").ToString();
    if (data->GetType("id") == VTYPE_INT) {
      const int id = data->GetInt("id");
      const bool success = !data->HasKey("error");
      auto result = data->GetDictionary(success ? "result" : "error");
      const auto json = result ? Json(result) : "{}";
      ToolResult(id, success, json.data(), json.size(), session);
    } else if (data->GetType("method") == VTYPE_STRING) {
      auto params = data->GetDictionary("params");
      if(session.empty()&&private_response_&&private_response_->Event(data->GetString("method"),params))return true;
      NativeHARResponseBytes(data->GetString("method").ToString(),params,session);
      if (data->GetString("method") == "Fetch.requestPaused") {
        if (params && InitSnapshotTraffic("Fetch.requestPaused",params,session)) return true;
        if (network_) network_->Event("Fetch.requestPaused",params,session);
        else Error("Native interception controller unavailable");
        return true;
      }
      // Already parsed byte entries may accompany a Latin1 rendered POST.
      // Project once into the bounded native publication, avoiding giant copies.
      if(data->GetString("method")=="Network.requestWillBeSent" && params) {
        if(InitSnapshotTraffic("Network.requestWillBeSent",params,session))return true;
        WorkerAttributeNetwork("Network.requestWillBeSent",params,session);
        AttributeInitialPopupRequest(params);
        if(session.empty()&&ready_&&params->GetString("frameId")==main_frame_id_&&params->GetString("type")=="Document")if(!engine_.network_archive().PageLifecycle(network_context_id_,"page:"+std::to_string(page_id_),"request",suitecut::DictionaryNumber(params,"timestamp")*1000,suitecut::DictionaryNumber(params,"wallTime")*1000,params->GetString("loaderId")))NetworkFatal("Native HAR actual main document time rejected");
        if(!engine_.network_archive().OriginalRequest(engine_.contexts()->ContextId(page_id_),"page:"+std::to_string(page_id_),session,params)){Error("Native network archive original request rejected");}if(engine_.network_archive().NeedsTick())engine_.WakeTrace();
        if(network_)network_->Event("Network.requestWillBeSent",params,session);
        if(session.empty()&&private_response_)private_response_->Primary("Network.requestWillBeSent",params);
        NativeImageStreamEvent("Network.requestWillBeSent",params,session);
        NetworkEvent("Network.requestWillBeSent",params,session);return true;
      }
      if(data->GetString("method")=="Network.dataReceived"&&params){
        if(InitSnapshotTraffic("Network.dataReceived",params,session))return true;
        if(network_)network_->Event("Network.dataReceived",params,session);
        NativeImageStreamEvent("Network.dataReceived",params,session);return true;
      }
      if(data->GetString("method")=="Fetch.authRequired"){if(network_)network_->Event("Fetch.authRequired",params,session);return true;}
      if(data->GetString("method")=="Network.suiteCutWebSocketRoutedV1"){if(!NativeWebSocketPolicyEvent(params,session))NativeWebSocketRouteEvent(params,session);return true;}
      if(data->GetString("method").ToString().starts_with("Network.webSocket")){
        if(sockets_)sockets_->Event(data->GetString("method"),params,session,Now());
        return true;
      }
      NativeDeveloperEvent(data->GetString("method").ToString(),params,session);
      if(NativeDebuggerEvent(data->GetString("method").ToString(),params,session))return true;
          if(NativeCoverageEvent(data->GetString("method").ToString(),params,session))return true;
      const auto json = params ? Json(params) : "{}";
      ToolEvent(data->GetString("method").ToString(), json.data(), json.size(), session);
    }
    return true;
  }
  void ToolResult(int id, bool success, const void* result, size_t size, const std::string& session) {
    const suitecut::NativeWorkOriginFrame native_origin(NativeInspectorReplyOrigin(id,session));
    NativeInspectorReply(id,session);
    NativeHARPrimaryReceipt(id,success,session);
    // This is the real native reply on the exact root Source channel, not a
    // guessed match to an interleaved process-wide diagnostic reply line.
    if(session.empty()&&id==diagnostic_network_enable_){
      diagnostic_network_enable_=-1;
      if(std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS")&&diagnostic_messages_++<2000)
        std::fprintf(stderr,"native Network.enable result page=%d browser=%d id=%d session=root ok=%d\n",page_id_,browser_?browser_->GetIdentifier():0,id,success?1:0);
    }
    if(element_states_&&element_states_->Complete(id,success&&size<=65536,size&&size<=65536?std::string(static_cast<const char*>(result),size):"{}",session))return;
    if(page_errors_&&page_errors_->Complete(id,success&&size<=suitecut::PageErrorControls::kResponse,size&&size<=suitecut::PageErrorControls::kResponse?std::string(static_cast<const char*>(result),size):"{}",session))return;
    if(NativeTraceProtocolResult(id,success,result,size,session))return;
    if (WorkerToolResult(id,success,result,size,session)) return;
    const auto binding_call = binding_calls_.find(id);
    if (binding_call != binding_calls_.end()) {
      if (binding_call->second.session != session) { Error("Native binding protocol session mismatch"); return; }
      auto callback = std::move(binding_call->second.callback); binding_calls_.erase(binding_call);
      if (size > 1048576) callback(false,"Native binding protocol response limit exceeded");
      else callback(success,size ? std::string(static_cast<const char*>(result),size) : "{}");
      return;
    }
    const auto network_call = network_calls_.find(id);
    if (network_call != network_calls_.end()) {
      if (network_call->second.session != session) { Error("Native network protocol session mismatch"); return; }
      const bool expired=network_call->second.expired;auto callback = std::move(network_call->second.callback); network_calls_.erase(network_call);
      if(expired)callback(false,"Native network command deadline expired");
      else if (size > kMaxResponseBytes) callback(false, "Native network protocol response limit exceeded");
      else callback(success, std::string(static_cast<const char*>(result), size));
      return;
    }
    if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && !success && diagnostic_messages_++ < 2000)
      std::fprintf(stderr, "native protocol error id=%d session=%s: %.*s\n", id, session.c_str(), static_cast<int>(std::min(size, size_t(8192))), static_cast<const char*>(result));
    const auto utility = utility_worlds_.find(id);
    if (utility != utility_worlds_.end()) {
      const auto world = utility->second; utility_worlds_.erase(utility);
      const auto frame = frame_states_.find(world.frame);
      if (frame == frame_states_.end() || frame->second.session != session || frame->second.loader != world.loader) return;
      if (!success) { Error("Native DOM execution world could not be created"); return; }
      auto parsed = CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
      auto response = parsed ? parsed->GetDictionary() : nullptr;
      if (response && response->GetType("executionContextId") == VTYPE_INT)
        frame->second.utility_context = response->GetInt("executionContextId");
      CheckNavigations(); return;
    }
    const auto boot = session_trees_.find(id);
    if (boot != session_trees_.end()) {
      const auto expected = boot->second;
      NativeFrameDebuggerTreeReceipt frame_tree_receipt{this,id,session,NativeFrameDebuggerAttachment(session)};
      session_trees_.erase(boot);
      if (expected != session || !sessions_.contains(session)) return;
      if (!success) { Error("Native child frame bootstrap failed"); return; }
      auto parsed = CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
      auto response = parsed ? parsed->GetDictionary() : nullptr;
      auto tree = response ? response->GetDictionary("frameTree") : nullptr;
      if (tree) ImportFrameTree(tree, session,id);
      return;
    }
    if (id == bootstrap_tree_) {
      NativeFrameDebuggerTreeReceipt frame_tree_receipt{this,id,session,NativeFrameDebuggerAttachment(session)};
      bootstrap_tree_ = 0;
      if (!success || size > 1048576) { Error("Native frame bootstrap failed"); return; }
      auto parsed = CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
      auto response = parsed ? parsed->GetDictionary() : nullptr;
      auto tree = response ? response->GetDictionary("frameTree") : nullptr;
      if (!tree) { Error("Native frame tree unavailable"); return; }
      ImportFrameTree(tree,"",id);
      SettingsCommit();
      MaybeReady();
      return;
    }
    const auto found = devtools_.find(id);
    if (found == devtools_.end()) {
      // A canceled/expired evaluation can finish later. Its returned object
      // must not become an unowned remote reference in the renderer.
      if (success && size <= 1048576 && size > 0) {
        auto parsed = CefParseJSON(std::string(static_cast<const char*>(result), size), JSON_PARSER_RFC);
        auto response = parsed ? parsed->GetDictionary() : nullptr;
        auto remote = response ? response->GetDictionary("result") : nullptr;
        auto details = response ? response->GetDictionary("exceptionDetails") : nullptr;
        std::set<std::string> objects;
        const auto collect = [&objects](Dict remote) {
          if (remote && remote->GetType("objectId") == VTYPE_STRING) objects.insert(remote->GetString("objectId"));
        };
        collect(remote);
        if (response) collect(response->GetDictionary("object"));
        if (details) collect(details->GetDictionary("exception"));
        // A canceled metadata query can return several remote references,
        // including accessor functions and symbols, after its group was freed.
        if (response) for (const char* key : {"result", "internalProperties", "privateProperties"}) {
          auto fields = response->GetList(key);
          if (fields) for (size_t i = 0; i < fields->GetSize(); ++i) {
            auto field = fields->GetDictionary(i);
            if (field) for (const char* member : {"value", "get", "set", "symbol"}) collect(field->GetDictionary(member));
          }
        }
        for (const auto& object : objects) {
          auto params = CefDictionaryValue::Create(); params->SetString("objectId", object);
          SendDevTools("Runtime.releaseObject", params, session);
        }
      }
      return;
    }
    if (found->second.session != session) {
      if(SelectorWaitRejectedSession(id,success,result,size,session))return;
      Error("Native protocol session mismatch");return;
    }
    auto callback = std::move(found->second.callback);
    const int owner = found->second.owner; const bool retained = found->second.retain_after_cancel; const size_t response_limit=found->second.response_limit;
    const auto artifact_generation=found->second.artifact_generation;const bool artifact_capture=found->second.artifact_capture;
    devtools_.erase(found);
    // Current preparation replies preserve the ordinary navigation/error path.
    // Cancellation/stale generations and late ACKs still settle their retained
    // callback, without ever acting on a replacement owner.
    const bool artifact_regular=artifact_generation&&!artifact_capture&&operations_.IsCurrent(owner,artifact_generation)&&!operations_.IsExpired(owner,artifact_generation,Now())&&size<=response_limit;
    if (retained&&!artifact_regular) {
      if (size > response_limit) callback(false, response_limit==kMaxResponseBytes?"{\"message\":\"Native artifact protocol response exceeds 192 MiB\"}":"{\"message\":\"Native DevTools response byte limit exceeded\"}");
      else callback(success, size ? std::string(static_cast<const char*>(result), size) : "{}");
      return;
    }
    if (!operations_.Contains(owner)) return;
    if (size > kMaxResponseBytes) { Complete(owner, false, "Native DevTools response exceeds 192 MiB"); return; }
    const auto bytes = static_cast<const uint8_t*>(result);
    const auto json = size ? std::string(reinterpret_cast<const char*>(bytes), size) : "{}";
    if(image_streams_)if(const auto body=body_waiters_.find(owner);body!=body_waiters_.end()&&body->second.dispatched){
      const auto document=documents_.find(body->second.key);
      if(document!=documents_.end()){
        const auto recovered=image_streams_->Resolve(document->second.session,document->second.request,success,json);
        if(recovered.first&&(!success||recovered.second!=json)){callback(true,recovered.second);return;}
      }
    }
    if(private_response_&&NativePrivateBodyFallback(success,json))if(const auto body=body_waiters_.find(owner);body!=body_waiters_.end()&&body->second.dispatched){
      const auto document=documents_.find(body->second.key);const auto operation=operations_.Get(owner);
      if(operation&&document!=documents_.end()&&document->second.session.empty()){
        const auto generation=operation->generation;const auto deadline=operation->deadline_ms?operation->deadline_ms:Now()+30000;
        if(private_response_->Read(document->second.request,deadline,[this,owner,generation,callback,success,json](bool ok,const std::string&actual){
          if(!operations_.IsCurrent(owner,generation))return;
          if(closing_page_||!browser_){Complete(owner,false,closed_reason_.empty()?"Native page is closed":closed_reason_);return;}
          if(operations_.IsExpired(owner,generation,Now())){Complete(owner,false,"Native response body original deadline exceeded");return;}
          if(ok)callback(true,actual);else if(success)callback(true,json);else Complete(owner,false,NativePrimaryBodyError(owner,json));
        }))return;
      }
    }
    if (!success) {
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
      auto response = parsed ? parsed->GetDictionary() : nullptr;
      std::string message = response && response->GetType("message") == VTYPE_STRING
        ? response->GetString("message").ToString() : "Native DevTools command failed";
      if (message.find("navigated") != std::string::npos ||
          (message.find("context") != std::string::npos &&
          (message.find("destroyed") != std::string::npos || message.find("find") != std::string::npos)))
        message += " (execution context unavailable after navigation or closure)";
      // A failed body lookup can outlive its actual document resource. Use
      // the original request's loader, rather than the current URL, to explain
      // the failure while preserving Chromium's original error text.
      const auto body_read = body_waiters_.find(owner);
      if (body_read != body_waiters_.end() && body_read->second.dispatched && !body_read->second.loader.empty()) {
        const auto frame = frame_states_.find(body_read->second.frame);
        if (frame != frame_states_.end() && !frame->second.loader.empty() && frame->second.loader != body_read->second.loader)
          message += " (response frame has navigated away)";
      }
      if (count_queries_.contains(owner) &&
          (message.find("Cannot find context") != std::string::npos || message.find("Execution context was destroyed") != std::string::npos ||
           message.find("Inspected target navigated") != std::string::npos || message.find("Could not find object with given id") != std::string::npos)) {
        CompleteCountZero(owner); return;
      }
      // An actual frame/context lifecycle event owns empty-scope visibility.
      // Unrelated protocol failures remain errors, never a catch-and-false.
      if(visibility_snapshots_.Contains(owner)){Complete(owner,false,message);return;}
      if (context_retries_.contains(owner) &&
          (message.find("context") != std::string::npos || message.find("navigated") != std::string::npos)) {
        RetryContext(owner); return;
      }
      Complete(owner, false, message);
      return;
    }
    callback(true, json);
  }
  std::set<std::string> init_snapshot_requests_;
  struct ChildBootstrap {
    std::string frame;
    bool ignored = false;
    int phase = 0, remaining = 2;
    double deadline = 0;
  };
  std::map<std::string, ChildBootstrap> pending_child_bootstraps_;
  bool child_bootstrap_task_ = false, child_bootstrap_timer_ = false, child_bootstrap_failed_ = false;
  void FailChildBootstrap(const std::string& error) {
    if (closing_page_ || child_bootstrap_failed_) return;
    child_bootstrap_failed_ = true; pending_child_bootstraps_.clear();
    Error(error.c_str()); if (browser_) browser_->GetHost()->CloseBrowser(true);
  }
  void WakeChildBootstrapDeadlines() {
    if (child_bootstrap_timer_ || pending_child_bootstraps_.empty() || closing_page_ || child_bootstrap_failed_) return;
    child_bootstrap_timer_ = true;
    CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
      self->child_bootstrap_timer_ = false;
      if (self->closing_page_ || self->child_bootstrap_failed_) return;
      for (const auto& [session, bootstrap] : self->pending_child_bootstraps_) {
        if (bootstrap.deadline <= Now()) { self->FailChildBootstrap("Native child target initialization timed out"); return; }
      }
      self->WakeChildBootstrapDeadlines();
    }), 1000);
  }
  void ChildBootstrapFinished(const std::string& session, const std::string& frame, bool ok, const std::string& error) {
    const auto found = pending_child_bootstraps_.find(session);
    if (found == pending_child_bootstraps_.end() || found->second.frame != frame || found->second.phase != 1 ||
        closing_page_ || child_bootstrap_failed_ || !sessions_.contains(session) || sessions_.at(session) != frame) return;
    if (!ok) { FailChildBootstrap("Native child target initialization failed: " + error); return; }
    if (--found->second.remaining == 0) { found->second.phase = 2; WakeChildBootstraps(); }
  }
  void DeferChildBootstrap(const std::string& session, const std::string& frame, bool ignored) {
    if (closing_page_ || child_bootstrap_failed_ || !browser_) return;
    if (pending_child_bootstraps_.size() >= 1024 && !pending_child_bootstraps_.contains(session)) {
      FailChildBootstrap("Native child target initialization capacity exceeded"); return;
    }
    pending_child_bootstraps_.try_emplace(session, ChildBootstrap{frame, ignored, ignored ? 2 : 0, 8, Now() + 30000});
    WakeChildBootstraps(); WakeChildBootstrapDeadlines();
  }
  void WakeChildBootstraps() {
    if (child_bootstrap_task_ || pending_child_bootstraps_.empty() || closing_page_ || child_bootstrap_failed_) return;
    std::vector<std::string> runnable;
    for (const auto& [session, bootstrap] : pending_child_bootstraps_) if (bootstrap.phase != 1) runnable.push_back(session);
    if (runnable.empty()) return;
    child_bootstrap_task_ = true;
    CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), runnable = std::move(runnable)] {
      // Fetch.enable changes Chromium's observer lists. Bootstrap after the
      // Target.attachedToTarget notification returns, then wait for real native
      // route/binding acknowledgments before resuming child author execution.
      for (const auto& session : runnable) {
        if (self->closing_page_ || !self->browser_ || self->child_bootstrap_failed_) break;
        const auto found = self->pending_child_bootstraps_.find(session);
        if (found == self->pending_child_bootstraps_.end()) continue;
        const auto frame = found->second.frame;
        if (!found->second.ignored && (!self->sessions_.contains(session) || self->sessions_.at(session) != frame)) {
          self->pending_child_bootstraps_.erase(found); continue;
        }
        if (found->second.phase == 2) {
          self->pending_child_bootstraps_.erase(found);
          self->NativePristineChildResumed(session);
          if (!self->SendDevTools("Runtime.runIfWaitingForDebugger", nullptr, session)) {
            self->FailChildBootstrap("CEF rejected native child target resume"); break;
          }
          continue;
        }
        if (found->second.phase != 0) continue;
        found->second.phase = 1;
        const int tree = self->AllocateDevToolsId();
        if (!tree) { self->FailChildBootstrap("Native child frame bootstrap command capacity exceeded"); break; }
        if(!found->second.ignored&&!self->NativeFrameDebuggerTreePrepare(tree,session))break;
        self->EnableSession(session);
        if (self->file_chooser_subscribed_) {
          auto params = CefDictionaryValue::Create(); params->SetBool("enabled", true);
          self->SendDevTools("Page.setInterceptFileChooserDialog", params, session);
        }
        self->session_trees_[tree] = session;
        if (!self->SubmitDevTools(tree, "Page.getFrameTree", nullptr, session)) {
          self->session_trees_.erase(tree); self->FailChildBootstrap("CEF rejected native child frame bootstrap"); break;
        }
        self->AutoAttach(session);
        const auto coverage_deadline = found->second.deadline;
        auto ready = [self, session, frame](bool ok, const std::string& error) { self->ChildBootstrapFinished(session, frame, ok, error); };
        if (self->network_) self->network_->SessionAttached(session, ready); else ready(true, "");
        if (self->bindings_) self->bindings_->SessionAttached(session, ready); else ready(true, "");
        self->NativeMediaChild(session,ready);
        if(self->native_idle_)self->NativeIdleChild(session,coverage_deadline,ready);else ready(true,"");
        self->SettingsChild(session,ready);
        self->NativeCoverageChild(session,frame,coverage_deadline,ready);
        self->NativeWebSocketPolicyAttach(session,ready);
        const auto waiting=self->native_pristine_child_waiting_.find(session);
        self->NativePristineChildStart(session,frame,waiting!=self->native_pristine_child_waiting_.end()&&waiting->second,coverage_deadline,ready);
      }
      self->child_bootstrap_task_ = false; self->WakeChildBootstraps();
    }));
  }
  std::deque<std::pair<Dict, std::string>> init_snapshot_continuations_;
  size_t init_snapshot_continuation_count_ = 0;
  bool init_snapshot_continuation_task_ = false;
  void WakeInitSnapshotContinuations() {
    if (init_snapshot_continuation_task_ || init_snapshot_continuations_.empty() || closing_page_) return;
    init_snapshot_continuation_task_ = true;
    CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
      auto pending = std::move(self->init_snapshot_continuations_);
      self->init_snapshot_continuations_.clear();
      for (const auto& [params, session] : pending) {
        if (self->closing_page_ || !self->browser_) break;
        --self->init_snapshot_continuation_count_;
        self->SendDevTools("Fetch.continueRequest", params, session);
      }
      self->init_snapshot_continuation_count_ = self->init_snapshot_continuations_.size();
      self->init_snapshot_continuation_task_ = false;
      self->WakeInitSnapshotContinuations();
    }));
  }
  bool InitSnapshotTraffic(const std::string& method, Dict data, const std::string& session) {
    const auto request = data->GetDictionary("request");
    const bool private_request = request && request->GetString("url").ToString().starts_with("suitecut-init:");
    if (method == "Fetch.requestPaused" && private_request) {
      auto params = CefDictionaryValue::Create(); params->SetString("requestId", data->GetString("requestId"));
      // CEF synchronously notifies DevTools observers. Continuing here can
      // re-enter Chromium's observer list while its original notification is
      // active. One bounded UI task continues after that notification returns.
      if (init_snapshot_continuation_count_ >= 64) { Error("Native init snapshot continuation capacity exceeded"); return true; }
      init_snapshot_continuations_.emplace_back(params, session); ++init_snapshot_continuation_count_;
      WakeInitSnapshotContinuations(); return true;
    }
    if (!method.starts_with("Network.")) return false;
    const auto key = session + "\n" + data->GetString("requestId").ToString();
    if (method == "Network.requestWillBeSent" && private_request) {
      if (init_snapshot_requests_.size() >= 1024) { Error("Native init snapshot request tracking capacity exceeded"); return true; }
      init_snapshot_requests_.insert(key); return true;
    }
    if (!init_snapshot_requests_.contains(key)) return false;
    if (method == "Network.loadingFinished" || method == "Network.loadingFailed") init_snapshot_requests_.erase(key);
    return true;
  }
  void ToolEvent(const std::string& method, const void* params, size_t size, const std::string& session) {
    if(method=="Emulation.virtualTimeBudgetExpired"){if(session.empty()&&size<=64&&native_virtual_time_)native_virtual_time_->BudgetExpired(Now());return;}
    if (size > 1048576 && method == "Fetch.requestPaused") {
      auto parsed = CefParseJSON(std::string(static_cast<const char*>(params),size),JSON_PARSER_RFC);
      if (network_) network_->RejectOversizedPause(parsed ? parsed->GetDictionary() : nullptr,session);
      else Error("Native interception controller unavailable");
      return;
    }
    if(method=="Runtime.exceptionThrown"&&size>1048576){Error("Native page error event limit exceeded");if(browser_)browser_->GetHost()->CloseBrowser(true);return;}
    if (size > 1048576 || (method != "Page.frameAttached" && method != "Page.frameNavigated" &&
        method != "Page.frameDetached" && method != "Page.navigatedWithinDocument" &&
        method != "Page.frameRequestedNavigation" && method != "Page.frameStartedNavigating" && method != "Page.frameStoppedLoading" &&
        method != "Runtime.executionContextCreated" && method != "Runtime.executionContextDestroyed" &&
        method != "Runtime.executionContextsCleared" && method != "Target.attachedToTarget" &&
        method != "Target.detachedFromTarget" && method != "Target.targetDestroyed" && method != "Target.targetInfoChanged" && method != "Inspector.workerScriptLoaded" && method != "Runtime.consoleAPICalled" &&
        method != "Runtime.bindingCalled" && method != "Runtime.exceptionThrown" && method != "Log.entryAdded" && method != "Page.lifecycleEvent" && method != "Input.dragIntercepted" && method != "Page.fileChooserOpened" &&
        method != "WebAuthn.credentialAdded" && method != "WebAuthn.credentialAsserted" &&
        method != "Network.requestWillBeSent" && method != "Network.responseReceived" && method != "Network.dataReceived" &&
        method != "Network.loadingFinished" && method != "Network.loadingFailed" &&
        method != "Network.requestWillBeSentExtraInfo" && method != "Network.responseReceivedExtraInfo" && method != "Fetch.requestPaused")) return;
    if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000)
      std::fprintf(stderr, "native event %s session=%s: %.*s\n", method.c_str(), session.c_str(), static_cast<int>(std::min(size, size_t(8192))), static_cast<const char*>(params));
    // Passive diagnostics only: never route chunk observations through request
    // migration, archive publication, native cursors or SDK event consumers.
    if (method == "Network.dataReceived") return;
    auto parsed = CefParseJSON(std::string(static_cast<const char*>(params), size), JSON_PARSER_RFC);
    auto data = parsed ? parsed->GetDictionary() : nullptr;
    if(data&&NativeFrameDebuggerRegistrationEvent(method))return;
    if(NativeVirtualTimeTargetEvent(method,data,session))return;
    if(data&&method=="Page.frameStartedNavigating")NativePristineNavigationStarted(session,data->GetString("frameId").ToString());
    if (data && InitSnapshotTraffic(method, data, session)) return;
    if (!data) return;
    if(native_coverage_&&session.empty()&&method=="Page.frameNavigated"){NativeCoverageScripts();auto frame=data->GetDictionary("frame");if(frame&&!frame->HasKey("parentId"))native_coverage_->Navigated();}
    if(CredentialEvent(method,data,session))return;
    if(element_states_&&method=="Runtime.executionContextDestroyed"&&data->GetInt("executionContextId")>0)element_states_->ContextDestroyed(session,data->GetInt("executionContextId"));
    if(element_states_&&method=="Runtime.executionContextsCleared")element_states_->ContextDestroyed(session);
    // Retire exact renderer object ownership before worker-only context events
    // return early; physical private commands remain pinned until their ACK.
    if(page_errors_&&method=="Runtime.executionContextDestroyed"&&data->GetInt("executionContextId")>0)page_errors_->ContextDestroyed(session,data->GetInt("executionContextId"));
    if(page_errors_&&method=="Runtime.executionContextsCleared")page_errors_->ContextDestroyed(session);
    NativeWorkerDebuggerRuntime(method,data,session);
    NativeDeveloperWorkersChanged();
    WorkerConsoleRuntimeEvent(method,data,session);
    if (WorkerRuntimeEvent(method,data,session)) return;
    WorkerAttributeNetwork(method,data,session);
    if(method=="Network.requestWillBeSent")AttributeInitialPopupRequest(data);
    if (method == "Target.targetDestroyed") {NativeWorkerTargetDestroyed(data);return;}
    if (method == "Target.targetInfoChanged") { WorkerUpdated(data); return; }
    InitialNavigationEvent(method,data,session);
    if (ActionNavigationEvent(method,data)) return;
    if (method == "Runtime.exceptionThrown") { NativeVirtualTimeException(data,session);ConsoleException(data,session);return; }
    if (method == "Runtime.bindingCalled") { if (bindings_) bindings_->Event(data,session); return; }
    if (network_ && (method.starts_with("Network.") || method == "Fetch.requestPaused")) network_->Event(method, data, session);
    if(method=="Network.requestWillBeSent"||method=="Network.loadingFinished"||method=="Network.loadingFailed"){if(session.empty()&&private_response_)private_response_->Primary(method,data);NativeImageStreamEvent(method,data,session);}
    if (method == "Fetch.requestPaused") return;
    if (method == "Page.fileChooserOpened") { FileChooserEvent(data, session); return; }
    if (method == "Input.dragIntercepted") { NativeDragEvent(data); return; }
    if (method == "Page.lifecycleEvent") {
      const auto id = data->GetString("frameId").ToString();
      const auto frame = frame_states_.find(id);
      if (frame != frame_states_.end()) {
        const auto loader = data->GetString("loaderId").ToString();
        if (data->GetString("name") == "init" && loader != frame->second.loader) {
          frame->second.loader = loader; frame->second.milestones.clear();
        }
        if (loader == frame->second.loader) frame->second.milestones.insert(data->GetString("name").ToString());
        if(ready_&&session.empty()&&id==main_frame_id_&&loader==frame->second.loader){
          const auto timestamp=suitecut::DictionaryNumber(data,"timestamp")*1000;
          if(!engine_.network_archive().PageLifecycle(network_context_id_,"page:"+std::to_string(page_id_),data->GetString("name"),timestamp,std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count(),loader))NetworkFatal("Native HAR actual page lifecycle metadata rejected");
        }
      }
      CheckNavigations(); return;
    } else if (method.starts_with("Network.")) {
      if (method == "Network.requestWillBeSent" || method == "Network.responseReceived" || method == "Network.loadingFinished" || method == "Network.loadingFailed") NetworkEvent(method, data, session);
      return;
    } else if (method == "Runtime.consoleAPICalled") {
      const auto worker=worker_sessions_.find(session);
      if(worker!=worker_sessions_.end()&&worker_targets_.at(worker->second).kind!="worker") {
        auto args=data->GetList("args");if(args)for(size_t i=0;i<args->GetSize();++i){auto value=args->GetDictionary(i);if(value&&value->GetType("objectId")==VTYPE_STRING){auto release=CefDictionaryValue::Create();release->SetString("objectId",value->GetString("objectId"));SendDevTools("Runtime.releaseObject",release,session);}}return;
      }
      auto args=data->GetList("args");std::string text;
      if(args)for(std::size_t index=0;index<args->GetSize();++index){if(index)text+=" ";text+=ConsolePreview(args->GetDictionary(index));if(text.size()>1000000)break;}
      auto stack=data->GetDictionary("stackTrace");auto calls=stack?stack->GetList("callFrames"):nullptr;
      auto location=calls&&calls->GetSize()?calls->GetDictionary(0):nullptr;
      ConsoleEvent(data->GetString("type"),text,location,Number(data,"timestamp"),args,session,data->GetInt("executionContextId"));return;
    } else if (method == "Log.entryAdded") {
      auto entry = data->GetDictionary("entry");
      if(entry&&entry->GetString("source")=="worker"&&worker_targets_.contains(entry->GetString("workerId")))return;
      if (entry) {
        auto location = CefDictionaryValue::Create(); location->SetString("url", entry->GetString("url"));
        location->SetInt("lineNumber", entry->GetInt("lineNumber"));
        ConsoleEvent(entry->GetString("level").ToString(), entry->GetString("text").ToString(), location,Number(entry,"timestamp"));
      }
      return;
    } else if (method == "Target.attachedToTarget") {
      auto info = data->GetDictionary("targetInfo");
      const auto child = data->GetString("sessionId").ToString();
      if (!info || child.empty() || child.size() > 128) { FailChildBootstrap("Invalid native child target identity"); return; }
      const auto worker_generation=worker_generation_;
      if (WorkerAttached(data,session)) {NativeWorkerReattached(data,worker_generation);NativeWorkerDebuggerAttached(data,worker_generation);return;}
      if (info->GetString("type") != "iframe") {
        DeferChildBootstrap(child, "", true);
        return;
      }
      if (sessions_.size() >= 1024 && !sessions_.contains(child)) { FailChildBootstrap("Native target session limit exceeded"); return; }
      const auto frame_id = info->GetString("targetId").ToString();
      if (frame_id.empty() || frame_id.size() > 256) { FailChildBootstrap("Invalid native child frame identity"); return; }
      if(session.size()>128||child==session||
         (native_target_parents_.size()>=1024&&!native_target_parents_.contains(child))||
         (native_target_parents_.contains(child)&&native_target_parents_.at(child)!=session)){
        FailChildBootstrap("Invalid native child target parent identity or parent-session limit");return;
      }
      if(network_&&!network_->FrameSessionAttached(child,session,frame_id))return;
      if(!NativeFrameDebuggerAttached(child,session,frame_id))return;
      // Only actual observed live Inspector edges belong here. NetworkControls
      // separately retains its migration graph while detached request bodies drain.
      native_target_parents_[child]=session;
      sessions_[child] = frame_id;
      const auto frame = frame_states_.find(frame_id);
      if (frame != frame_states_.end()) { NativeFrameDebuggerFrameInvalidated(frame_id,frame->second.session); frame->second.session = child; frame->second.context = 0; FrameContextChanged(frame->second); frame->second.context_identity.clear(); frame->second.utility_context=0; frame->second.utility_identity.clear(); }
      native_pristine_child_waiting_[child]=data->GetType("waitingForDebugger")==VTYPE_BOOL&&data->GetBool("waitingForDebugger");
      DeferChildBootstrap(child, frame_id, false);
      return;
    } else if (method == "Target.detachedFromTarget") {
      NativeTargetSessionDetached(data->GetString("sessionId").ToString());
      return;
    } else if (method == "Page.frameAttached") {
      auto info = CefDictionaryValue::Create();
      info->SetString("id", data->GetString("frameId")); info->SetString("parentId", data->GetString("parentFrameId"));
      StoreFrame(info, "frameattached", session);
    } else if (method == "Page.frameNavigated") {
      auto frame = data->GetDictionary("frame");
      if (frame) { StoreFrame(frame, "framenavigated", session,data->GetString("type")=="BackForwardCacheRestore"); NativePristineCommitted(session,frame->GetString("id").ToString(),data->GetString("type")=="BackForwardCacheRestore"); }
    } else if (method == "Page.navigatedWithinDocument") {
      if(data->GetString("frameId")==main_frame_id_){initial_history_dirty_=true;initial_history_candidate_=false;}
      const auto found = frame_states_.find(data->GetString("frameId"));
      if (found != frame_states_.end()) {
        const auto url = data->GetString("url").ToString();
        if (url.size() <= 8192) {
          found->second.url = url; ++found->second.navigation;
          FrameEvent("framenavigated", found->second); CheckNavigations();
          if (found->first == main_frame_id_) {
            if (navigation_) Complete(navigation_, true);
            auto response = CefDictionaryValue::Create(); response->SetString("url", url); response->SetInt("status", 0);
            const auto waiters = navigation_waiters_;
            for (const int id : waiters) Complete(id, true, "", Json(response));
          }
        }
      }
    } else if (method == "Page.frameDetached") {
      const auto id = data->GetString("frameId").ToString();
      if (data->GetString("reason") == "swap") {
        const auto frame = frame_states_.find(id);
        if (frame != frame_states_.end() && frame->second.session == session) { NativeFrameDebuggerFrameInvalidated(id,session); CompleteVisibilityInFrame(id); frame->second.context = 0; FrameContextChanged(frame->second); frame->second.context_identity.clear(); frame->second.utility_context=0; frame->second.utility_identity.clear(); }
      } else DetachFrame(id);
    } else if (method == "Runtime.executionContextCreated") {
      auto context = data->GetDictionary("context");
      auto aux = context ? context->GetDictionary("auxData") : nullptr;
      if (aux && (aux->GetBool("isDefault") || context->GetString("name") == "__suitecut_dom__")) {
        const auto id = aux->GetString("frameId").ToString();
        if (id.empty() || id.size() > 128 || (!frame_states_.contains(id) && frame_states_.size() >= 1024)) return;
        const auto identity=context->GetString("uniqueId").ToString();
        if(identity.empty()||identity.size()>256){Error("Native execution context unique identity unavailable or too large");return;}
        auto& frame = frame_states_[id]; frame.id = id; frame.session = session; FrameContextChanged(frame);
        if (aux->GetBool("isDefault")) { frame.context = context->GetInt("id"); frame.context_identity=identity; frame.origin = context->GetString("origin").ToString(); NativeFrameDebuggerDefaultObserved(frame); NativePristineContextCreated(id); if(id==main_frame_id_&&settings_bootstrap_)SettingsCommit(); EnsureUtilityWorld(id); if (bindings_) bindings_->ContextAvailable(FrameJson(frame),session,frame.context); }
        else { frame.utility_context = context->GetInt("id"); frame.utility_identity=identity; }
        CheckNavigations();
      }
    } else if (method == "Runtime.executionContextDestroyed") {
      const int context = data->GetInt("executionContextId");
      const auto destroyed_identity=data->GetString("executionContextUniqueId").ToString();
      NativePristineContextDestroyed(session,context,destroyed_identity);
      if (bindings_ && context > 0) bindings_->ContextRemoved(session,context);
      if (context > 0) CompleteCountsInContext(session, context,destroyed_identity);
      for (auto& [id, frame] : frame_states_) if (frame.session == session) {
        if (frame.context == context && (destroyed_identity.empty()||frame.context_identity==destroyed_identity)) {NativeFrameDebuggerFrameInvalidated(id,session);FrameContextChanged(frame);frame.context = 0;frame.context_identity.clear();}
        if (frame.utility_context == context && (destroyed_identity.empty()||frame.utility_identity==destroyed_identity)) {FrameContextChanged(frame);frame.utility_context = 0;frame.utility_identity.clear();}
      }
    } else {
      NativeFrameDebuggerContextsCleared(session);
      NativePristineContextsCleared(session);
      CompleteCountsInContext(session);
      if (bindings_) bindings_->ContextRemoved(session);
      for (auto& [id, frame] : frame_states_) if (frame.session == session) { FrameContextChanged(frame);frame.context = 0; frame.utility_context = 0; frame.context_identity.clear();frame.utility_identity.clear(); }
    }
  }
 private:
  using DevToolsCallback = std::function<void(bool, const std::string&)>;
  struct DevToolsCall { int owner; DevToolsCallback callback; std::string session; bool retain_after_cancel = false; size_t response_limit = kMaxResponseBytes; bool retained_control = false; bool lossless_scalars = false; uint64_t artifact_generation = 0; bool artifact_capture = false; bool selector_group_release = false; };
  struct NetworkCall { std::string session; suitecut::NetworkControls::Result callback; double deadline;bool expired=false; };
  struct BindingCall { std::string session; suitecut::BindingControls::Result callback; double deadline; bool expired = false; };
  struct UtilityWorld { std::string frame, session, loader; };
  struct ObjectHandle { std::string object_id, group, frame_id; int context = 0; std::string session, node_id, context_identity; uint64_t renderer_epoch=0; };
  struct FrameState {
    std::string id, parent, name, url; int context = 0, utility_context = 0; std::string session, origin, loader, context_identity, utility_identity;
    uint64_t context_generation=0;
    std::set<std::string> milestones; uint64_t navigation = 0; double network_activity = 0;
  };
  void FrameContextChanged(FrameState&frame){
    NativeHighlightsFrameChanged(frame.id);
    if(snapshot_references_){snapshot_references_->InvalidateFrame(frame.id,Now());WakeDeadlines();}
    if(frame.context_generation==std::numeric_limits<uint64_t>::max()){Error("Native execution context generation exhausted");return;}
    ++frame.context_generation;
  }
  struct Navigation {
    std::string frame, url, wait, loader, previous_loader, api;
    int timeout = 30000;
    uint64_t after = 0, history_candidate = 0, native_download_after = 0;
    std::string download_abort, download_abort_url;
    bool download_created = false;
    int history_entry = -1;
    std::string history_url;
    bool history_pending = false, history_verified = false;
    bool dispatched = false, same_document = false, wait_only = false, next = false, replace_initial = false, recovery = false;
  };
  struct Document {
    std::string frame, request, session, loader, method, url;
    Dict response; bool finished = false; std::string error; bool document = false;
  };
  struct ActiveRequest { std::string frame; double started; };
  struct BodyWait { std::string key, frame, loader; bool dispatched = false; };
#include "element_state_controller.inc"
#include "selector_wait_controller.inc"
#include "frame_element_controller.inc"
#include "owner_frame_controller.inc"
#include "query_selection_controller.inc"
#include "handle_disposal_controller.inc"
#include "page_error_controller.inc"
#include "console_controller.inc"
  Dict FrameJson(const FrameState& frame) {
    auto info = CefDictionaryValue::Create();
    info->SetString("id", frame.id); info->SetString("parentId", frame.parent);
    suitecut::InboundSetString(info,"name",frame.name); info->SetString("url", frame.url);
    info->SetDouble("navigationSequence", static_cast<double>(frame.navigation));
    return info;
  }
  struct EventRecord { uint64_t sequence; std::string event, json; };
  void JournalEvent(uint64_t sequence, const std::string& event, const std::string& json) {
    while (!event_history_.empty() && (event_history_.size() >= 128 || event_history_bytes_ + json.size() > 4 * 1048576)) {
      event_history_bytes_ -= event_history_.front().json.size(); event_history_.pop_front();
    }
    event_history_.push_back({sequence, event, json}); event_history_bytes_ += json.size();
    PumpEventPredicates();
  }
  void FrameEvent(const char* event, const FrameState& frame) {
    if(settings_bootstrap_)return;
    if (std::string(event)=="framenavigated" && frame.id==main_frame_id_) ConsoleNavigation();
    if (!ready_) return;
    const auto sequence = ++event_sequence_;
    auto header = Header("event"); header->SetString("event", event); header->SetDictionary("frame", FrameJson(frame));
    header->SetDouble("sequence", static_cast<double>(sequence));
    JournalEvent(sequence, event, Json(FrameJson(frame)));
    SendPacket(header);
    std::vector<int> waiters;
    for (const auto& [id, name] : event_waiters_) if (name == event) waiters.push_back(id);
    for (const int id : waiters) Complete(id, true, "", Json(FrameJson(frame)));
  }
  void StoreFrame(Dict info, const char* event, const std::string& session = "",bool restored=false) {
    const auto id = info->GetString("id").ToString();
    auto parent = info->GetString("parentId").ToString();
    if (!session.empty() && parent.empty()) {
      const auto existing = frame_states_.find(id);
      if (existing != frame_states_.end()) parent = existing->second.parent;
    }
    std::string name;
    if(!suitecut::ReadInspectorFrameName(info,name)){Error("Native frame name metadata encoding or unit limit exceeded");return;}
    // Chromium reports a committed frame's fragment separately from its URL.
    const auto url = info->GetString("url").ToString() + info->GetString("urlFragment").ToString();
    if (id.empty() || id.size() > 128 || parent.size() > 128 || url.size() > 8192 ||
        (!frame_states_.contains(id) && frame_states_.size() >= 1024)) { Error("Native frame metadata limit exceeded"); return; }
    if (std::strcmp(event, "framenavigated") == 0) {
      NativeFrameDebuggerFrameInvalidated(id,session);
      CompleteCountsInFrame(id);
      if (bindings_&&!restored) bindings_->FrameRemoved(id);
      // Chromium can discard an old document's complete child tree without
      // emitting an individual frameDetached event for each descendant.
      std::vector<std::string> children;
      for (const auto& [child, state] : frame_states_) if (state.parent == id) children.push_back(child);
      for (const auto& child : children) DetachFrame(child);
    }
    auto& frame = frame_states_[id];
    frame.id = id; frame.parent = parent; frame.name = name; frame.url = url; frame.session = session;
    engine_.contexts()->PageOriginObserved(page_id_, frame.url);
    if (info->GetType("securityOrigin") == VTYPE_STRING) frame.origin = info->GetString("securityOrigin").ToString();
    const auto loader = info->GetString("loaderId").ToString();
    if (!loader.empty() && loader != frame.loader) { frame.loader = loader; frame.milestones.clear(); }
    if (std::strcmp(event, "framenavigated") == 0) {
      FrameContextChanged(frame);if(!restored){frame.context = 0; frame.utility_context = 0;frame.context_identity.clear();frame.utility_identity.clear();} frame.milestones.insert("commit"); ++frame.navigation; InvalidateFrameHandles(id); upload_store_.DropFrame(id);
    }
    if (parent.empty() && session.empty()) {main_frame_id_ = id;NativeFrameDebuggerMainObserved();}
    if(restored&&frame.context>0&&!frame.context_identity.empty())NativeFrameDebuggerDefaultObserved(frame);
    if (!*event && id == main_frame_id_ && frame.url == last_cef_load_url_) {
      frame.milestones.insert("commit"); frame.milestones.insert("DOMContentLoaded"); frame.milestones.insert("load");
    }
    EnsureUtilityWorld(id);
    if (*event) FrameEvent(event, frame);
    if(restored){if(bindings_&&frame.context)bindings_->ContextAvailable(FrameJson(frame),session,frame.context);ObserveRestoredDocument(id);}
    CheckNavigations();
  }
  void ImportFrameTree(Dict tree, const std::string& session = "",int observed_tree=0) {
    auto info = tree->GetDictionary("frame");
    if (!info) return;
    StoreFrame(info, "", session);
    NativeFrameDebuggerInitialAssociated(observed_tree,info->GetString("id").ToString(),session);
    auto children = tree->GetList("childFrames");
    if (children && frame_states_.size() < 1024) for (size_t i = 0; i < children->GetSize(); ++i) {
      auto child = children->GetDictionary(i); if (child) ImportFrameTree(child, session,observed_tree);
    }
  }
  bool http_user_agent_pending_=false;
  void MaybeReady() {
    if (ready_ || !native_websocket_policy_root_ready_ || !native_worker_discovery_ready_ || !private_response_ready_ || (private_response_&&private_response_->parent_close_requested()) || !geolocation_ready_ || !initial_empty_ready_ || settings_bootstrap_ || (settings_page_&&!settings_applied_) || !authentication_ready_ || !network_policy_ready_ || !initial_loaded_ || main_frame_id_.empty()) return;
    const auto frame = frame_states_.find(main_frame_id_);
    if (frame == frame_states_.end()) return;
    if(!NativeWidgetMouseReady())return;
    if(engine_.http_user_agent_.empty()){
      if(http_user_agent_pending_)return;
      http_user_agent_pending_=true;const int id=AllocateDevToolsId();
      if(!id||network_calls_.size()>=1024){NetworkFatal("Native HTTP user-agent discovery capacity exceeded");return;}
      network_calls_.emplace(id,NetworkCall{"",[this](bool ok,const std::string&json){
        if(closing_page_)return;
        const auto value=ok?CefParseJSON(json,JSON_PARSER_RFC):nullptr;const auto result=value?value->GetDictionary():nullptr;
        const auto agent=result?result->GetString("userAgent").ToString():"";
        if(agent.empty()||agent.size()>8192){NetworkFatal("Native browser user-agent discovery failed");return;}
        engine_.http_user_agent_=agent;MaybeReady();
      },Now()+30000});WakeDeadlines();
      if(!SubmitDevTools(id,"Browser.getVersion",CefDictionaryValue::Create(),"")){
        network_calls_.erase(id);NetworkFatal("Native browser user-agent discovery dispatch failed");
      }return;
    }
    if(!PrepareInitialHistory())return;
    if(!engine_.StartupReady(page_id_))return;
    native_creation_handler_receipt_.reset();
    ready_ = true;
    auto header = Header("ready"); header->SetDouble("timestampMs", Now()); header->SetString("cefVersion", CEF_VERSION);header->SetInt("captureWidth",capture_width_);header->SetInt("captureHeight",capture_height_);auto viewport=CefDictionaryValue::Create();viewport->SetInt("width",width_);viewport->SetInt("height",height_);if(context_settings_&&context_settings_->no_fixed_viewport)header->SetNull("viewport");else header->SetDictionary("viewport",viewport);
    auto capabilities=suitecut::NativeBackendCapabilities();capabilities->SetBool("originalWidgetInputReceipts",native_widget_mouse_&&native_widget_mouse_->available());
    header->SetDictionary("capabilities",capabilities);
    if(const auto video=engine_.VideoMetadata(page_id_))header->SetDictionary("video",video);
    header->SetDictionary("mainFrame", FrameJson(frame->second)); SendPacket(header);
    FlushDownloadEvents();
    if (page_id_ == 1) reader_ = std::thread([this] { ReadCommands(); });
    EmitFrame();
  }
  void DetachFrame(const std::string& root) {
    std::set<std::string> detached{root};
    for (size_t pass = 0; pass < frame_states_.size(); ++pass) {
      const auto before = detached.size();
      for (const auto& [id, frame] : frame_states_) if (detached.contains(frame.parent)) detached.insert(id);
      if (detached.size() == before) break;
    }
    for(const auto& frame:detached){const auto known=frame_states_.find(frame);if(known!=frame_states_.end())NativeFrameDebuggerFrameInvalidated(frame,known->second.session);NativePristineFrameDetached(frame);}
    if(element_states_)for(const auto& frame:detached)element_states_->FrameRemoved(frame);
    if(snapshot_references_){for(const auto& frame:detached)snapshot_references_->InvalidateFrame(frame,Now());WakeDeadlines();}
    if(network_)for(const auto&frame:detached)network_->FrameRemoved(frame);
    std::vector<int> canceled;
    for (const auto& [id, frame] : operation_frames_) if (detached.contains(frame)) canceled.push_back(id);
    for (const int id : canceled) {
      const auto input = input_work_.find(id);
      // A synchronous click handler may remove its own frame. Once the final
      // release has actually entered Chromium, retain the original operation
      // until its input ACK and renderer barrier; detachment is not that ACK.
      if (input != input_work_.end() && input->second->final_release_submitted) operation_frames_.erase(id);
      else if (count_queries_.contains(id)) CompleteCountZero(id);
      else if (visibility_snapshots_.Contains(id)) CompleteVisibilityEmpty(id);
      else if (lazy_queries_.contains(id)) RetryContext(id);
      else Complete(id, false, "Native frame detached");
    }
    for (auto it = utility_worlds_.begin(); it != utility_worlds_.end();) {
      if (detached.contains(it->second.frame)) it = utility_worlds_.erase(it); else ++it;
    }
    for (const auto& id : detached) {
      if (bindings_) bindings_->FrameRemoved(id);
      InvalidateFrameHandles(id);
      upload_store_.DropFrame(id);
      const auto found = frame_states_.find(id);
      if (found != frame_states_.end()) { NetworkActivity(found->second.parent); FrameEvent("framedetached", found->second); frame_states_.erase(found); }
    }
  }
  void InvalidateFrameHandles(const std::string& frame) {
    NativeHighlightsFrameChanged(frame);
    ConsoleFrameRemoved(frame);
    for (auto it = handles_.begin(); it != handles_.end();) {
      if (it->second.frame_id != frame) { ++it; continue; }
      auto params = CefDictionaryValue::Create(); params->SetString("objectGroup", it->second.group);
      if (browser_) SendDevTools("Runtime.releaseObjectGroup", params, it->second.session);
      it = handles_.erase(it);
    }
  }
  std::string FrameId(Dict message) {
    return message->GetType("frame") == VTYPE_STRING ? message->GetString("frame").ToString() : main_frame_id_;
  }
  void EnsureUtilityWorld(const std::string& id) {
    const auto frame = frame_states_.find(id); if (frame == frame_states_.end() || !frame->second.context || frame->second.utility_context) return;
    for (const auto& [call, world] : utility_worlds_) if (world.frame == id && world.session == frame->second.session && world.loader == frame->second.loader) return;
    if (utility_worlds_.size() >= 1024) { Error("Native DOM execution world limit exceeded"); return; }
    const int call = AllocateDevToolsId(); if (!call) { Error("Native DOM execution world command unavailable"); return; }
    auto params = CefDictionaryValue::Create(); params->SetString("frameId", id); params->SetString("worldName", "__suitecut_dom__");
    utility_worlds_[call] = {id, frame->second.session, frame->second.loader};
    if (!SubmitDevTools(call, "Page.createIsolatedWorld", params, frame->second.session)) {
      utility_worlds_.erase(call); Error("Native DOM execution world dispatch failed");
    }
  }
  void WithFrame(int owner, const std::string& frame, std::function<void(int)> action, bool utility = false) {
    const auto operation = operations_.Get(owner);
    if (!operation || !browser_) return;
    if(!ReferenceFrameOperationCurrent(owner))return;
    if(element_bounds_operations_.contains(owner)&&operations_.IsExpired(owner,operation->generation,Now())){Complete(owner,false,"boundingBox: Native operation timed out");return;}
    if(const auto bounds=element_bounds_work_.find(owner);bounds!=element_bounds_work_.end())if(const auto work=bounds->second;work&&!ElementBoundsCurrent(work))return;
    if(locator_predicate_operations_.contains(owner)&&operations_.IsExpired(owner,operation->generation,Now())){Complete(owner,false,"waitForFunction: Native operation timed out");return;}
    if(const auto predicate=locator_predicate_work_.find(owner);predicate!=locator_predicate_work_.end())if(!LocatorPredicateCurrent(predicate->second))return;
    if(aria_json_operations_.contains(owner)&&operations_.IsExpired(owner,operation->generation,Now())){Complete(owner,false,"ariaSnapshotJSON: Native operation timed out");return;}
    if(const auto snapshot=aria_json_work_.find(owner);snapshot!=aria_json_work_.end())if(!AriaJSONCurrent(snapshot->second))return;
    const auto found = frame_states_.find(frame);
    if (found == frame_states_.end()) {
      if (count_queries_.contains(owner)) CompleteCountZero(owner);
      else if (visibility_snapshots_.Contains(owner)) CompleteVisibilityEmpty(owner);
      else if (lazy_queries_.contains(owner)) RetryContext(owner);
      else Complete(owner, false, "Native frame detached or unavailable");
      return;
    }
    operation_frames_[owner] = frame;
    operation_sessions_[owner] = found->second.session;
    if (utility) EnsureUtilityWorld(frame);
    // Count observes the current document once; it does not wait for a new one.
    const int context = utility && (!count_queries_.contains(owner) || found->second.utility_context)
      ? found->second.utility_context : found->second.context;
    if(visibility_snapshots_.Contains(owner)){
      const auto identity=context==found->second.utility_context?found->second.utility_identity:found->second.context_identity;
      if(!visibility_snapshots_.Bind(owner,operation->generation,frame,found->second.session,found->second.loader,context,identity)){Complete(owner,false,"Native visibility snapshot scope unavailable");return;}
      // A live document can be waiting for its utility world to initialize.
      // Absence of that utility world alone is not proof of an empty document.
      if(!context&&!found->second.context){CompleteVisibilityEmpty(owner);return;}
    }
    if (context > 0) {
      if (count_queries_.contains(owner)) count_contexts_[owner] = {found->second.session, context};
      action(context); return;
    }
    if (count_queries_.contains(owner)) { CompleteCountZero(owner); return; }
    CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), owner, generation = operation->generation, frame, action = std::move(action), utility] {
      if (self->operations_.IsCurrent(owner, generation)) self->WithFrame(owner, frame, action, utility);
    }), 10);
  }
  struct ArgumentWork {
    int owner, context;
    std::uint64_t generation;
    size_t index = 0;
    std::vector<ObjectHandle> handles;
    Dict params;
    CefRefPtr<CefListValue> arguments;
    std::function<void()> done;
  };
  void ArgumentStep(std::shared_ptr<ArgumentWork> work) {
    if (!operations_.IsCurrent(work->owner, work->generation)) return;
    if (work->index == work->handles.size()) {
      work->params->SetList("arguments", work->arguments);
      auto done = std::move(work->done); done(); return;
    }
    const auto handle = work->handles[work->index];
    const auto destination = frame_states_.find(operation_frames_[work->owner]);
    if (destination == frame_states_.end() || destination->second.context != work->context) {
      Complete(work->owner, false, "Native execution context destroyed during argument adoption"); return;
    }
    if (handle.frame_id == destination->first) {
      auto argument = CefDictionaryValue::Create(); argument->SetString("objectId", handle.object_id);
      work->arguments->SetDictionary(work->index++, argument); ArgumentStep(work); return;
    }
    if (handle.node_id.empty()) {
      Complete(work->owner, false, "JSHandles can be evaluated only in the context they were created! Native handle belongs to a different frame context"); return;
    }
    const auto source = frame_states_.find(handle.frame_id);
    if (source == frame_states_.end() || source->second.context != handle.context ||
        handle.session != destination->second.session || source->second.origin.empty() ||
        source->second.origin != destination->second.origin) {
      Complete(work->owner, false, "Unable to adopt element handle from a different document"); return;
    }
    // Resolve the actual DOM backend node in the destination world. Each
    // temporary wrapper belongs to this operation's group, including failures
    // and cancellation while a later argument is being adopted.
    evaluation_groups_[work->owner].insert(operation_sessions_[work->owner]);
    auto describe = CefDictionaryValue::Create(); describe->SetString("objectId", handle.node_id);
    DevTools(work->owner, "DOM.describeNode", describe, [this, work](bool, const std::string& json) {
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto node = result ? result->GetDictionary("node") : nullptr;
      if (!node || node->GetType("backendNodeId") != VTYPE_INT) {
        Complete(work->owner, false, "Unable to adopt element handle from a different document"); return;
      }
      auto resolve = CefDictionaryValue::Create(); resolve->SetInt("backendNodeId", node->GetInt("backendNodeId"));
      resolve->SetInt("executionContextId", work->context);
      resolve->SetString("objectGroup", "suitecut-automation-" + std::to_string(work->owner));
      DevTools(work->owner, "DOM.resolveNode", resolve, [this, work](bool, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto node = result ? result->GetDictionary("object") : nullptr;
        if (!node || node->GetType("objectId") != VTYPE_STRING) {
          Complete(work->owner, false, "Unable to adopt element handle from a different document"); return;
        }
        auto wrap = CefDictionaryValue::Create(); wrap->SetString("objectId", node->GetString("objectId"));
        wrap->SetString("functionDeclaration", "function(){return {value:this};}");
        wrap->SetString("objectGroup", "suitecut-automation-" + std::to_string(work->owner));
        DevTools(work->owner, "Runtime.callFunctionOn", wrap, [this, work](bool, const std::string& json) {
          auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
          auto wrapper = result ? result->GetDictionary("result") : nullptr;
          if (!wrapper || result->HasKey("exceptionDetails") || wrapper->GetType("objectId") != VTYPE_STRING) {
            Complete(work->owner, false, "Unable to adopt element handle from a different document"); return;
          }
          auto argument = CefDictionaryValue::Create(); argument->SetString("objectId", wrapper->GetString("objectId"));
          work->arguments->SetDictionary(work->index++, argument); ArgumentStep(work);
        });
      });
    });
  }
  std::string HandleGuid(int id) { return "suitecut-native-" + handle_namespace_ + ":" + std::to_string(id); }
  std::string MissingHandle(int id) { return "no object with guid " + HandleGuid(id) + " (native handle is disposed or its execution context was destroyed by navigation)"; }
  void HandleArguments(int id, Dict message, Dict params, bool destination_receiver, std::function<void()> done) {
    if (MaterializeConsoleArguments(id,message,[this,id,message,params,destination_receiver,done]{HandleArguments(id,message,params,destination_receiver,done);}))return;
    auto ids = message->GetList("handles");
    if (!ids || ids->GetSize() > 1024) { Complete(id, false, "Invalid native handle arguments"); return; }
    std::vector<ObjectHandle> handles;
    for (size_t index = 0; index < ids->GetSize(); ++index) {
      if (ids->GetType(index) != VTYPE_INT) { Complete(id, false, "Invalid native handle argument"); return; }
      const auto found = handles_.find(ids->GetInt(index));
      if (found == handles_.end()) { Complete(id, false, MissingHandle(ids->GetInt(index))); return; }
      handles.push_back(found->second);
    }
    WithFrame(id, FrameId(message), [this, id, params, destination_receiver, handles = std::move(handles), done = std::move(done)](int context) mutable {
      const auto entry = operations_.Get(id); if (!entry) return;
      if (destination_receiver) params->SetInt("executionContextId", context);
      auto work = std::make_shared<ArgumentWork>(); work->owner = id; work->context = context; work->generation = entry->generation;
      work->params = params; work->arguments = CefListValue::Create(); work->handles = std::move(handles); work->done = std::move(done);
      ArgumentStep(work);
    });
  }
  void Handle(int id, Dict message, bool frame_ready = false) {
    const auto action = message->GetString("action").ToString();
    if (action == "frameElement") { FrameElementHandle(id,message); return; }
    if (action != "create" && action != "dispose" && action != "disposeMany" && console_arguments_.contains(message->GetInt("handle"))) {
      MaterializeConsoleArgument(id,message->GetInt("handle"),[this,id,message,frame_ready]{Handle(id,message,frame_ready);});return;
    }
    if (action == "ownerFrame") { ElementOwnerFrame(id,message); return; }
    if (action == "create" && message->GetType("query") == VTYPE_LIST && !message->HasKey("expression")) {
      const auto path = message->GetList("framePath");
      if (path && path->GetSize()) {
        const auto entry = operations_.Get(id); if (!entry) return;
        ResolveFramePath(id, entry->generation, message, 0, FrameId(message),
          [this,id](Dict resolved) { Handle(id,resolved); });
        return;
      }
      if(!message->HasKey("nativeQueryNodes")) {
        if(message->GetType("queryFunction")==VTYPE_STRING)context_retries_[id]=[this,id,message]{Handle(id,message);};
        QueryNodesInMainWorld(id,message,[this,id,message](const std::string& array){auto config=message->Copy(false);config->SetString("nativeQueryNodes",array);Handle(id,config);});return;
      }
      auto config = message->Copy(false);
      if (message->GetType("queryFunction") == VTYPE_STRING) {
        config->Remove("query"); config->Remove("queryFunction");
        config->SetString("function", "async function(...handles){const nodes=" + (message->HasKey("nativeQueryNodes") ? std::string("this") : QueryExpression(message)) +
          ";if(nodes.length>1)throw new Error('strict mode violation: locator matched multiple elements');" +
          "if(!nodes.length)return {suitecutQueryMissing:true};return {value:await (" +
          message->GetString("queryFunction").ToString() + ")(nodes[0],...handles)};}");
        config->SetBool("queryHandle", true);
        // Keep the original query for deadline-preserving retries on absence.
        if(!message->HasKey("nativeQueryNodes"))context_retries_[id] = [this,id,message] { Handle(id,message); };
        Handle(id,config); return;
      }
      const auto expression = "(()=>{const nodes=" + QueryExpression(message) + ";" +
        (message->GetBool("all") ? std::string("return nodes;") :
         std::string(message->GetBool("strict") ? "if(nodes.length>1)throw new Error('strict mode violation: locator matched multiple elements');" : "") + "return nodes[0]||null;") + "})()";
      if(message->HasKey("nativeQueryNodes")) {
        config->Remove("query");config->SetString("function","function(){const nodes=this;"+
          (message->GetBool("all")?std::string("return {value:nodes};"):
          std::string(message->GetBool("strict")?"if(nodes.length>1)throw new Error('strict mode violation: locator matched multiple elements');":"")+"return {value:nodes[0]||null};")+"}");
        if(!config->HasKey("handles"))config->SetList("handles",CefListValue::Create());
      } else config->SetString("expression",expression);
      Handle(id,config); return;
    }
    if ((action == "create" || action == "focus") && !frame_ready) {
      WithFrame(id, FrameId(message), [this, id, message](int) { Handle(id, message, true); }, action == "focus"); return;
    }
    if (action == "create" || action == "property") {
      if (handles_.size() + pending_handle_groups_.size() >= 1024 || next_handle_ == std::numeric_limits<int>::max()) { Complete(id, false, "Native object handle limit exceeded"); return; }
      if (action == "create" && message->GetType("expression") != VTYPE_STRING && message->GetType("function") != VTYPE_STRING) { Complete(id, false, "Handle creation requires an expression or function"); return; }
      const auto group = "suitecut-handle-" + std::to_string(++next_handle_);
      pending_handle_groups_.emplace(id, group);
      const auto frame_id = FrameId(message);
      const auto frame = frame_states_.find(frame_id);
      if (frame == frame_states_.end()) { Complete(id, false, "Native frame detached"); return; }
      const int context = frame->second.context;
      const auto context_identity=frame->second.context_identity;const auto renderer_epoch=native_pristine_renderer_epoch_;
      if(message->HasKey("bindingArgumentContext")&&(message->GetInt("bindingArgumentContext")!=context||message->GetString("bindingArgumentSession")!=frame->second.session)){Complete(id,false,"Native binding argument context was destroyed");return;}
      operation_sessions_[id] = frame->second.session;
      operation_frames_[id] = frame_id;
      auto params = CefDictionaryValue::Create();
      std::string method = "Runtime.evaluate";
      if (action == "property") {
        const auto found = handles_.find(message->GetInt("handle"));
        if (found == handles_.end() || message->GetType("name") != VTYPE_STRING) { Complete(id, false, "Invalid native property handle"); return; }
        if (found->second.frame_id != frame_id) { Complete(id, false, "Native handle belongs to a different frame context"); return; }
        method = "Runtime.callFunctionOn";
        params->SetString("objectId", found->second.object_id);
        params->SetString("functionDeclaration", "function(name){return {value:this.value[name]};}");
        auto arguments = CefListValue::Create(); auto name = CefDictionaryValue::Create(); name->SetString("value", message->GetString("name"));
        arguments->SetDictionary(0, name); params->SetList("arguments", arguments);
      } else if (message->GetType("function") == VTYPE_STRING) {
        method = "Runtime.callFunctionOn";
        params->SetString("functionDeclaration", message->GetString("function"));
        if(message->GetType("nativeQueryNodes")==VTYPE_STRING)params->SetString("objectId",message->GetString("nativeQueryNodes"));
      } else {
        params->SetString("expression", "(async()=>({value:await(" + message->GetString("expression").ToString() + ")}))()");
        params->SetInt("contextId", context);
      }
      params->SetBool("awaitPromise", true);
      params->SetBool("returnByValue", false);
      params->SetString("objectGroup", group);
      const int handle_id = next_handle_;
      auto dispatch = [this, id, method, params, handle_id, group, frame_id, context, context_identity, renderer_epoch, message] {
      DevTools(id, method, params, [this, id, handle_id, group, frame_id, context, context_identity, renderer_epoch, message](bool ok, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
        auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto remote = result ? result->GetDictionary("result") : nullptr;
        if (!ok || !remote || result->HasKey("exceptionDetails") || remote->GetType("objectId") != VTYPE_STRING) {
          auto release = CefDictionaryValue::Create(); release->SetString("objectGroup", group);
          SendDevTools("Runtime.releaseObjectGroup", release, operation_sessions_[id]);
          const auto details = result ? result->GetDictionary("exceptionDetails") : nullptr;
          const auto exception = details ? details->GetDictionary("exception") : nullptr;
          const std::string detail = exception && exception->GetType("description") == VTYPE_STRING
            ? exception->GetString("description").ToString() : details ? details->GetString("text").ToString() : "";
          Complete(id, false, "Native handle creation failed" + (detail.empty() ? std::string("") : ": " + detail)); return;
        }
        const auto object_id = remote->GetString("objectId").ToString();
        auto properties = CefDictionaryValue::Create(); properties->SetString("objectId", object_id); properties->SetBool("ownProperties", true);
        DevToolsCallback metadata=[this, id, handle_id, group, object_id, frame_id, context, context_identity, renderer_epoch, message](bool ok, const std::string& json) {
          auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
          auto result = parsed ? parsed->GetDictionary() : nullptr;
          auto fields = result ? result->GetList("result") : nullptr;
          Dict value;
          bool query_missing = false;
          if (ok && fields) for (size_t i = 0; i < fields->GetSize(); ++i) {
            auto field = fields->GetDictionary(i);
            if (field && field->GetString("name") == "suitecutQueryMissing") {
              const auto marker = field->GetDictionary("value");
              query_missing = marker && marker->GetBool("value");
            }
            if (field && field->GetString("name") == "value") { value = field->GetDictionary("value"); break; }
          }
          if (message->GetBool("queryHandle") && query_missing) {
            auto release = CefDictionaryValue::Create(); release->SetString("objectGroup",group);
            SendDevTools("Runtime.releaseObjectGroup",release,operation_sessions_[id]);
            pending_handle_groups_.erase(id); RetryContext(id); return;
          }
          if (!value) { Complete(id, false, "Native handle metadata unavailable"); return; }
          if (message->GetBool("predicate")) {
            const auto type = value->GetString("type").ToString();
            const auto special = value->GetString("unserializableValue").ToString();
            const bool truthy = type != "undefined" && value->GetString("subtype") != "null" &&
              !(type == "boolean" && !value->GetBool("value")) &&
              !(type == "string" && suitecut::DevToolsString(value->GetValue("value")).empty()) &&
              !(type == "number" && (special == "NaN" || special == "-0" || (special.empty() && Number(value, "value") == 0))) &&
              !(type == "bigint" && (special == "0n" || special == "-0n"));
            if (!truthy) {
              auto release = CefDictionaryValue::Create(); release->SetString("objectGroup", group);
              SendDevTools("Runtime.releaseObjectGroup", release, operation_sessions_[id]); pending_handle_groups_.erase(id);
              const auto entry = operations_.Get(id); if (!entry) return;
              const int interval = message->GetType("pollInterval") == VTYPE_INT ? message->GetInt("pollInterval") : 0;
              CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation = entry->generation, message] {
                if(!self->operations_.IsCurrent(id,generation))return;
                // Array snapshots from a planned selector must never become
                // the root of later predicate attempts. Re-run the original
                // owned query, preserving its deadline and frame path.
                if(message->HasKey("nativeQueryNodes"))self->RetryContext(id);
                else self->Handle(id,message);
              }), interval);
              return;
            }
          }
          std::string preview;
          if (value->HasKey("unserializableValue")) preview = value->GetString("unserializableValue");
          else if (value->GetString("type")=="string"&&value->HasKey("value")) preview = suitecut::DevToolsString(value->GetValue("value"));
          else if (value->HasKey("value")) { auto primitive = value->GetValue("value"); preview = CefWriteJSON(primitive, JSON_WRITER_DEFAULT); }
          else if (value->HasKey("description")) preview = suitecut::DevToolsString(value->GetValue("description"));
          else preview = value->GetString("type");
          const bool node = value->GetString("subtype") == "node";
          const auto node_id = node ? value->GetString("objectId").ToString() : "";
          auto finish = [this,id,handle_id,object_id,group,frame_id,context,context_identity,renderer_epoch,node,node_id,message](std::string preview) {
            if(message->HasKey("nativeFrameOwnerChild")){
              const auto child=frame_states_.find(message->GetString("nativeFrameOwnerChild").ToString());
              if(child==frame_states_.end()||child->second.parent!=frame_id){Complete(id,false,"Native frame detached before frame owner delivery");return;}
            }
            const auto frame = frame_states_.find(frame_id);
            if (frame == frame_states_.end() || frame->second.context != context ||
                frame->second.session != operation_sessions_[id] || frame->second.context_identity!=context_identity || native_pristine_renderer_epoch_!=renderer_epoch) {
              Complete(id, false, "Native handle context destroyed by navigation or detachment"); return;
            }
            preview=suitecut::DevToolsTruncateWTF8(preview,8192);
            auto response = CefDictionaryValue::Create(); response->SetInt("handle", handle_id); response->SetString("guid", HandleGuid(handle_id));
            suitecut::DevToolsSetString(response,"preview",node ? "JSHandle@" + preview : preview); response->SetBool("node", node);
            response->SetString("frame",frame_id);
            auto typed=CefValue::Create();typed->SetDictionary(response->Copy(false));std::string json;
            if(!suitecut::DevToolsLosslessJSON(typed,json,65536)){Complete(id,false,"Native handle metadata limit exceeded");return;}
            const auto delivery_owner=operations_.Get(id);if(!delivery_owner)return;
            if(operations_.IsExpired(id,delivery_owner->generation,Now())){Complete(id,false,"Native handle metadata operation timed out");return;}
            if(selector_waits_)selector_waits_->Keep(id,group,operation_sessions_[id]);
            handles_.emplace(handle_id, ObjectHandle{object_id, group, frame_id, context, frame->second.session, node_id, context_identity, renderer_epoch});
            pending_handle_groups_.erase(id);
            Complete(id,true,"",json);
          };
          if (!node) { finish(std::move(preview)); return; }
          if (node_id.empty()) { Complete(id, false, "Native DOM handle object ID is unavailable"); return; }
          const auto preview_owner=operations_.Get(id);if(!preview_owner)return;
          const auto preview_generation=preview_owner->generation;
          if(operations_.IsExpired(id,preview_generation,Now())){Complete(id,false,"Native handle preview operation timed out");return;}
          auto metadata = CefDictionaryValue::Create(); metadata->SetString("objectId", node_id);metadata->SetInt("depth",1);
          // Read genuine native node fields, not author properties or serialized
          // descendants. The caption never supplies handle/backend identity.
          DevTools(id, "DOM.describeNode", metadata, [this,id,preview_generation,finish=std::move(finish)](bool ok,const std::string& json) mutable {
            if(!operations_.IsCurrent(id,preview_generation))return;
            if(operations_.IsExpired(id,preview_generation,Now())){Complete(id,false,"Native handle preview operation timed out");return;}
            auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
            auto result = parsed ? parsed->GetDictionary() : nullptr;
            std::string preview;
            if (!ok || !result || !suitecut::DOMHandlePreviewFromInspector(result->GetDictionary("node"),preview)) {
              Complete(id, false, "Native DOM handle preview metadata is unavailable"); return;
            }
            if(operations_.IsExpired(id,preview_generation,Now())){Complete(id,false,"Native handle preview operation timed out");return;}
            finish(std::move(preview));
          },std::nullopt,false,kMaxResponseBytes,true);
        };
        if(message->HasKey("bindingArgumentContext"))BindingArgumentMetadata(id,object_id,group,std::move(metadata));
        else DevTools(id,"Runtime.getProperties",properties,std::move(metadata),std::nullopt,false,kMaxResponseBytes,true);
      });
      };
      if (action == "create" && message->GetType("function") == VTYPE_STRING)
        HandleArguments(id, message, params, !message->HasKey("nativeQueryNodes"), std::move(dispatch));
      else dispatch();
      return;
    }
    const int handle_id = message->GetInt("handle");
    const auto found = handles_.find(handle_id);
    if (action == "dispose" || action == "disposeMany") { DisposeNativeHandles(id,message); return; }
    if (found == handles_.end()) { Complete(id, false, MissingHandle(handle_id)); return; }
    if (found->second.frame_id != FrameId(message)) { Complete(id, false, "Native handle belongs to a different frame context"); return; }
    operation_frames_[id] = found->second.frame_id;
    operation_sessions_[id] = found->second.session;
    if (action == "boundingBox") {
      if (found->second.node_id.empty()) { Complete(id, false, "Native handle is not a DOM node"); return; }
      ElementBoundingBox(id, found->second.node_id, found->second.frame_id); return;
    }
    if (action == "contentFrame") {
      if (found->second.node_id.empty()) { Complete(id, false, "Native handle is not a DOM node"); return; }
      auto params = CefDictionaryValue::Create(); params->SetString("objectId", found->second.node_id);
      DevTools(id, "DOM.describeNode", params, [this, id](bool ok, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto node = result ? result->GetDictionary("node") : nullptr;
        if (!ok || !node) { Complete(id, false, "Native DOM node unavailable"); return; }
        const auto frame = node->GetString("frameId").ToString();
        Complete(id, true, "", frame.empty() || !frame_states_.contains(frame) ? "null" : Literal(frame));
      }); return;
    }
    if (action == "focus") {
      if (found->second.node_id.empty()) { Complete(id, false, "Native handle is not a DOM node"); return; }
      const auto frame = frame_states_.find(found->second.frame_id);
      if (frame == frame_states_.end() || !frame->second.utility_context) { Complete(id, false, "Native DOM context unavailable"); return; }
      const int context = frame->second.utility_context;
      evaluation_groups_[id].insert(found->second.session);
      auto describe = CefDictionaryValue::Create(); describe->SetString("objectId", found->second.node_id);
      DevTools(id, "DOM.describeNode", describe, [this, id, context](bool, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto node = result ? result->GetDictionary("node") : nullptr;
        if (!node) { Complete(id, false, "Native DOM node unavailable"); return; }
        auto resolve = CefDictionaryValue::Create(); resolve->SetInt("backendNodeId", node->GetInt("backendNodeId"));
        resolve->SetInt("executionContextId", context); resolve->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        DevTools(id, "DOM.resolveNode", resolve, [this, id](bool, const std::string& json) {
          auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
          auto node = result ? result->GetDictionary("object") : nullptr;
          if (!node || node->GetType("objectId") != VTYPE_STRING) { Complete(id, false, "Native DOM node could not be resolved"); return; }
          auto params = CefDictionaryValue::Create(); params->SetString("objectId", node->GetString("objectId"));
          params->SetString("functionDeclaration", "function(){if(this.nodeType!==1)throw new Error('Node is not an HTMLElement');if(typeof this.focus==='function')this.focus();}");
          params->SetBool("returnByValue", true); params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
          DevTools(id, "Runtime.callFunctionOn", params, [this, id](bool, const std::string& json) {
            auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
            if (!result || result->HasKey("exceptionDetails")) { Complete(id, false, "Native element focus failed"); return; }
            Complete(id, true);
          });
        });
      }); return;
    }
    if (action != "evaluate" || message->GetType("function") != VTYPE_STRING) {
      Complete(id, false, "Invalid native handle operation"); return;
    }
    auto params = CefDictionaryValue::Create();
    params->SetString("objectId", found->second.object_id);
    params->SetString("functionDeclaration", message->GetString("function"));
    params->SetBool("awaitPromise", true);
    params->SetBool("returnByValue", true);
    const auto group = "suitecut-automation-" + std::to_string(id);
    evaluation_groups_[id].insert(operation_sessions_[id]);
    params->SetString("objectGroup", group);
    auto dispatch = [this, id, params] {
      DevTools(id, "Runtime.callFunctionOn", params, [this, id](bool ok, const std::string& json) {
        Complete(id, ok, ok ? "" : "Native handle evaluation failed", json);
      });
    };
    if (message->HasKey("handles")) HandleArguments(id, message, params, false, std::move(dispatch));
    else dispatch();
  }
  #include "input_work.inc"
  #include "widget_mouse_source.inc"
  double Number(Dict message, const char* key) {
    return message->GetType(key) == VTYPE_INT ? message->GetInt(key) : message->GetDouble(key);
  }
  Dict KeyPacket(const std::string& name, bool down) {
    auto key = SuiteCutDescribeKey(name, key_modifiers_); if (!key) return nullptr;
    const bool repeat = down && pressed_keys_.contains(key->code);
    if (down) pressed_keys_[key->code] = *key; else pressed_keys_.erase(key->code);
    key_modifiers_ = 0; for (const auto& [code, held] : pressed_keys_) key_modifiers_ |= held.modifier;
    key = SuiteCutDescribeKey(name, key_modifiers_);
    auto params = CefDictionaryValue::Create();
    params->SetString("type", down ? (key->text.empty() ? "rawKeyDown" : "keyDown") : "keyUp");
    params->SetString("key", key->key); params->SetString("code", key->code);
    params->SetInt("windowsVirtualKeyCode", key->virtual_key); params->SetInt("nativeVirtualKeyCode", key->virtual_key);
    params->SetInt("modifiers", key_modifiers_); params->SetBool("autoRepeat", repeat);
    params->SetInt("location", key->location); params->SetBool("isKeypad", key->location == 3);
    if (down && !key->text.empty()) { params->SetString("text", key->text); params->SetString("unmodifiedText", key->unmodified_text); }
    return params;
  }
  #include "input_retirement.inc"
  #include "input_queue.inc"
  Dict MousePacket(const std::string& type, const std::string& button, int count) {
    const int bit = button == "left" ? 1 : button == "right" ? 2 : button == "middle" ? 4 : 0;
    if (type == "mousePressed") mouse_buttons_ |= bit;
    if (type == "mouseReleased") mouse_buttons_ &= ~bit;
    auto params = CefDictionaryValue::Create(); params->SetString("type", type);
    params->SetDouble("x", mouse_x_); params->SetDouble("y", mouse_y_); params->SetInt("buttons", mouse_buttons_);
    params->SetInt("modifiers", key_modifiers_); params->SetDouble("force",mouse_buttons_?0.5:0.0);
    if (button != "none") { params->SetString("button", button); params->SetInt("clickCount", count); }
    else if (type == "mouseMoved" && mouse_buttons_)
      params->SetString("button", mouse_buttons_ & 1 ? "left" : mouse_buttons_ & 2 ? "right" : "middle");
    return params;
  }
  void DispatchCefMouse(Dict params) {
    if (!browser_) return;
    CefMouseEvent event; event.x = static_cast<int>(std::lround(Number(params,"x"))); event.y = static_cast<int>(std::lround(Number(params,"y")));
    const int keys = params->GetInt("modifiers"), buttons = params->GetInt("buttons");
    event.modifiers = (keys & 1 ? EVENTFLAG_ALT_DOWN : 0) | (keys & 2 ? EVENTFLAG_CONTROL_DOWN : 0) |
      (keys & 4 ? EVENTFLAG_COMMAND_DOWN : 0) | (keys & 8 ? EVENTFLAG_SHIFT_DOWN : 0) |
      (buttons & 1 ? EVENTFLAG_LEFT_MOUSE_BUTTON : 0) | (buttons & 2 ? EVENTFLAG_RIGHT_MOUSE_BUTTON : 0) |
      (buttons & 4 ? EVENTFLAG_MIDDLE_MOUSE_BUTTON : 0);
    const auto type = params->GetString("type").ToString();
    if (type == "mouseMoved") browser_->GetHost()->SendMouseMoveEvent(event, false);
    else if (type == "mouseWheel") browser_->GetHost()->SendMouseWheelEvent(event,
      static_cast<int>(std::lround(Number(params,"deltaX"))), static_cast<int>(std::lround(Number(params,"deltaY"))));
    else {
      const auto button = params->GetString("button").ToString();
      browser_->GetHost()->SendMouseClickEvent(event, button == "right" ? MBT_RIGHT : button == "middle" ? MBT_MIDDLE : MBT_LEFT,
        type == "mouseReleased", params->GetInt("clickCount"));
    }
  }
  void InputCall(std::shared_ptr<InputWork> work, const std::string& method, Dict params, std::function<void()> done) {
    TargetedKeyboardSubmitted(work, method, params);
    if(work->message->GetString("backend")=="widget"){NativeWidgetMouseCall(work,method,params,std::move(done));return;}
    if (NativeManualInput(work, method, params, done)) return;
    if (method == "Input.dispatchMouseEvent" && work->message->GetString("backend") == "cef") {
      work->busy = true; DispatchCefMouse(params);
      CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), work, done = std::move(done)] {
        if (self->NativeManualCurrent(work)) { work->busy = false; done(); }
      }));
      return;
    }
    work->busy = true; operation_sessions_[work->owner] = "";
    DevTools(work->owner, method, params, [this, work, done = std::move(done)](bool ok, const std::string&) {
      if(!NativeManualCurrent(work))return;
      work->busy = false;
      if (!ok) { Complete(work->owner, false, "Native input dispatch failed"); return; }
      done();
    });
  }
  void ContinueInput(std::shared_ptr<InputWork> work, int delay = 0) {
    if (delay > 0) { work->due = Now() + delay; return; }
    CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this), work] { self->InputStep(work); }));
  }
  void PumpInput() {
    for (const int id : {mouse_input_, keyboard_input_}) {
      const auto found = input_work_.find(id);
      if (found != input_work_.end() && !found->second->busy && found->second->due > 0 && Now() >= found->second->due) {
        auto work = found->second; work->due = 0; InputStep(work);
      }
    }
  }
  void InputStep(std::shared_ptr<InputWork> work) {
    const suitecut::NativeWorkOriginFrame handler_origin(work->native_origin);
    if (!browser_ || !operations_.IsCurrent(work->owner, work->generation) || work->busy || work->widget_completing) return;
    // Recheck at queue head: another backend may have been admitted while the
    // original widget's earlier press was still awaiting its actual ACK.
    if(work->kind=="mouse"&&native_widget_mouse_&&native_widget_mouse_->held()&&work->message->GetString("backend")!="widget"){
      Complete(work->owner,false,"Native mouse buttons are owned by the original-widget backend");return;
    }
    if(work->message->GetString("backend")=="widget"&&mouse_buttons_&&(!native_widget_mouse_||!native_widget_mouse_->held())){
      Complete(work->owner,false,"Native mouse buttons are owned by the legacy input backend");return;
    }
    if(work->message->GetString("backend")=="widget"&&operations_.IsExpired(work->owner,work->generation,Now())){
      Complete(work->owner,false,"Native original-widget operation timed out before dispatch");return;
    }
    if (PrepareTargetedKeyboard(work)) return;
    browser_->GetHost()->SetFocus(true);
    const int id = work->owner;
    if (work->kind == "mouse") {
      if(work->action=="tap"){
        work->busy=true;BeginTouch(id,work->message);return;
      }
      const auto button = work->message->GetType("button") == VTYPE_STRING ? work->message->GetString("button").ToString() : "left";
      const int total = work->message->GetType("clickCount") == VTYPE_INT ? work->message->GetInt("clickCount") : 1;
      const int delay = work->message->GetInt("delay");
      if (work->action == "move") {
        const int steps = work->message->GetType("steps") == VTYPE_INT ? work->message->GetInt("steps") : 1;
        mouse_x_ = work->start_x + (Number(work->message, "x") - work->start_x) * work->step / steps;
        mouse_y_ = work->start_y + (Number(work->message, "y") - work->start_y) * work->step / steps;
        InputCall(work, "Input.dispatchMouseEvent", MousePacket("mouseMoved", "none", 0), [this, work, steps] {
          if (work->step++ >= steps) Complete(work->owner, true); else ContinueInput(work);
        }); return;
      }
      if (work->action == "click") {
        if (work->phase == 0) {
          const int steps = work->message->GetType("steps") == VTYPE_INT ? work->message->GetInt("steps") : 1;
          mouse_x_ = work->start_x + (Number(work->message, "x") - work->start_x) * work->step / steps;
          mouse_y_ = work->start_y + (Number(work->message, "y") - work->start_y) * work->step / steps;
          if (work->step++ >= steps) work->phase = 1;
          InputCall(work, "Input.dispatchMouseEvent", MousePacket("mouseMoved", "none", 0), [this, work] { ContinueInput(work); }); return;
        }
        if (work->phase == 1) {
          if (work->pointer_target && operations_.IsExpired(id,work->generation,Now())) {
            Complete(id,false,"Native pointer original deadline expired before press"); return;
          }
          // Locator geometry was collected before queueing and hovering. Focus,
          // scrolling or a mousemove handler can invalidate it before the press.
          // Reacquire only before the first down; never replay delivered input.
          if (work->pointer_target && !work->pointer_verified) {
            work->busy = true;
            LocatorAction(id, work->generation, "pointer target", "click", "",
                          work->pointer_target, work);
            return;
          }
          work->phase = 2; work->held_mouse = true;
          InputCall(work, "Input.dispatchMouseEvent", MousePacket("mousePressed", button, work->count), [this, work, delay] { ContinueInput(work, delay); }); return;
        }
        InputCall(work, "Input.dispatchMouseEvent", MousePacket("mouseReleased", button, work->count), [this, work, total] {
          work->held_mouse = false;
          if (work->count++ >= total) FinishActionInput(work);
          else { work->phase = 1; ContinueInput(work); }
        }); return;
      }
      auto params = MousePacket(work->action == "down" ? "mousePressed" : work->action == "up" ? "mouseReleased" : "mouseWheel", work->action == "wheel" ? "none" : button, total);
      if (work->action == "wheel") { params->SetDouble("deltaX", Number(work->message, "deltaX")); params->SetDouble("deltaY", Number(work->message, "deltaY")); }
      InputCall(work, "Input.dispatchMouseEvent", params, [this, id] { Complete(id, true); }); return;
    }
    if (work->action == "prepareSendKeys") {
      work->busy = true;
      LocatorAction(id, work->generation, suitecut::InboundDisplay(suitecut::InboundString(work->message,"selector")), "sendKeys", work->text, work->message);
      return;
    }
    if (work->action == "insertText") {
      auto params = CefDictionaryValue::Create(); params->SetString("text", work->text);
      InputCall(work, "Input.insertText", params, [this, id] { Complete(id, true); }); return;
    }
    if (work->action == "down" || work->action == "up") {
      auto params = KeyPacket(work->key, work->action == "down");
      if (!params) { Complete(id, false, "Unknown key: " + Literal(work->key)); return; }
      InputCall(work, "Input.dispatchKeyEvent", params, [this, id] { Complete(id, true); }); return;
    }
    const int delay = work->message->GetInt("delay");
    if (work->action == "type") {
      if (work->message->GetBool("caretEnd") && !work->message->GetBool("caretPrepared")) {
        if (work->phase == 0) {
          work->phase = 1; work->held_keys.push_back("End");
          InputCall(work, "Input.dispatchKeyEvent", KeyPacket("End", true), [this, work] { ContinueInput(work); }); return;
        }
        auto params = KeyPacket("End", false); work->held_keys.clear(); work->phase = 0;
        work->message->SetBool("caretPrepared", true);
        InputCall(work, "Input.dispatchKeyEvent", params, [this, work] { ContinueInput(work); }); return;
      }
      if (work->standards_send_keys) { SendKeysSequenceStep(work); return; }
      if (work->cursor >= work->text.size()) { Complete(id, true); return; }
      if (work->phase == 0) {
        const auto first = static_cast<unsigned char>(work->text[work->cursor]);
        work->character_bytes = first < 128 ? 1 : first < 224 ? 2 : first < 240 ? 3 : 4;
        if (work->cursor + work->character_bytes > work->text.size()) { Complete(id, false, "Invalid native typing text"); return; }
        work->key = work->text.substr(work->cursor, work->character_bytes);
        auto params = KeyPacket(work->key, true);
        if (!params) {
          params = CefDictionaryValue::Create(); params->SetString("text", work->key);
          work->cursor += work->character_bytes;
          InputCall(work, "Input.insertText", params, [this, work, delay] { ContinueInput(work, delay); }); return;
        }
        work->held_keys.push_back(work->key); work->phase = 1;
        InputCall(work, "Input.dispatchKeyEvent", params, [this, work, delay] { ContinueInput(work, delay); }); return;
      }
      auto params = KeyPacket(work->key, false); work->held_keys.clear(); work->phase = 0; work->cursor += work->character_bytes;
      InputCall(work, "Input.dispatchKeyEvent", params, [this, work] { ContinueInput(work); }); return;
    }
    // Chords preserve already-held modifiers and release temporary keys in
    // reverse order, including cancellation while a delay is outstanding.
    if (work->phase == 0) {
      const auto name = work->chord[work->cursor];
      const auto key = SuiteCutDescribeKey(name, key_modifiers_);
      if (work->cursor + 1 < work->chord.size() && pressed_keys_.contains(key->code)) {
        ++work->cursor; ContinueInput(work); return;
      }
      auto params = KeyPacket(name, true); work->held_keys.push_back(name);
      if (++work->cursor == work->chord.size()) work->phase = 1;
      InputCall(work, "Input.dispatchKeyEvent", params, [this, work, delay] { ContinueInput(work, work->phase == 1 ? delay : 0); }); return;
    }
    if (work->held_keys.empty()) { Complete(id, true); return; }
    const auto name = work->held_keys.back(); work->held_keys.pop_back();
    InputCall(work, "Input.dispatchKeyEvent", KeyPacket(name, false), [this, work] { ContinueInput(work); });
  }
  #include "input_admission.inc"
  void SetupLocatorModifiers(int id, Dict message) {
    const auto modifiers = message->GetList("modifiers"); if (!modifiers) return;
    std::vector<std::string> before;
    for (const auto& [code, key] : pressed_keys_) if (key.modifier) before.push_back(code);
    locator_modifiers_[id] = before;
    int wanted = 0;
    for (size_t index = 0; index < modifiers->GetSize(); ++index) {
      auto key = SuiteCutDescribeKey(modifiers->GetString(index), 0);
      if (key) wanted |= key->modifier;
    }
    for (const auto& code : before) {
      auto key = SuiteCutDescribeKey(code, 0);
      if (!(wanted & key->modifier)) SendDevTools("Input.dispatchKeyEvent", KeyPacket(code, false));
    }
    for (const char* name : {"Alt", "Control", "Meta", "Shift"}) {
      auto key = SuiteCutDescribeKey(name, 0);
      if ((wanted & key->modifier) && !(key_modifiers_ & key->modifier)) SendDevTools("Input.dispatchKeyEvent", KeyPacket(name, true));
    }
  }
  void RestoreLocatorModifiers(int id) {
    const auto found = locator_modifiers_.find(id); if (found == locator_modifiers_.end()) return;
    const auto before = found->second; locator_modifiers_.erase(found);
    std::vector<std::string> current;
    for (const auto& [code, key] : pressed_keys_) if (key.modifier) current.push_back(code);
    for (const auto& code : current) if (std::find(before.begin(), before.end(), code) == before.end()) SendDevTools("Input.dispatchKeyEvent", KeyPacket(code, false));
    for (const auto& code : before) if (!pressed_keys_.contains(code)) SendDevTools("Input.dispatchKeyEvent", KeyPacket(code, true));
  }
  void CompleteVisibilityEmpty(int id,uint64_t generation=0){
    const auto scope=visibility_snapshots_.Get(id);if(!scope||(generation&&scope->generation!=generation)||!operations_.IsCurrent(id,scope->generation))return;
    if(operations_.IsExpired(id,scope->generation,Now())){Complete(id,false,"Native operation timed out");return;}
    if(!scope->validated){Complete(id,false,"Native visibility selector validation was interrupted by document destruction");return;}
    Complete(id,true,"","{\"result\":{\"type\":\"boolean\",\"value\":false}}");
  }
  void CompleteVisibilityInFrame(const std::string& frame){for(const auto&[id,generation]:visibility_snapshots_.InFrame(frame))CompleteVisibilityEmpty(id,generation);}
  void CompleteVisibilityInContext(const std::string& session,int context=0,const std::string& identity=""){for(const auto&[id,generation]:visibility_snapshots_.InContext(session,context,identity))CompleteVisibilityEmpty(id,generation);}
  bool VisibilityScopeGone(int id)const{
    const auto scope=visibility_snapshots_.Get(id);if(!scope)return false;
    const auto frame=frame_states_.find(scope->frame);if(frame==frame_states_.end())return suitecut::VisibilitySnapshotControls::Gone(*scope,{});
    const auto& current=frame->second;
    return suitecut::VisibilitySnapshotControls::Gone(*scope,suitecut::VisibilitySnapshotControls::Current{current.session,current.loader,current.context_identity,current.utility_identity,current.context,current.utility_context});
  }
  void CompleteCountZero(int id) {
    Complete(id, true, "", "{\"result\":{\"type\":\"object\",\"value\":[\"primitive\",0]}}");
  }
  void CompleteCountsInFrame(const std::string& frame) {
    CompleteVisibilityInFrame(frame);
    std::vector<int> ids;
    for (const auto& [id, current] : operation_frames_)
      if (current == frame && count_queries_.contains(id)) ids.push_back(id);
    for (const int id : ids) CompleteCountZero(id);
  }
  void CompleteCountsInContext(const std::string& session, int context = 0,const std::string& identity="") {
    CompleteVisibilityInContext(session,context,identity);
    std::vector<int> ids;
    for (const auto& [id, current] : count_contexts_)
      if (current.first == session && (!context || current.second == context)) ids.push_back(id);
    for (const int id : ids) CompleteCountZero(id);
  }
  void Complete(int id, bool ok, const std::string& error = "", const std::string& json = "") {
    engine_.websocket_routes().Cancel(id);
    const auto handler_owner=operations_.Get(id);
    const suitecut::NativeWorkOriginFrame handler_origin(engine_.LocatorHandlerCommandOrigin(id,page_id_,handler_owner?handler_owner->generation:0));
    if(NativeWidgetMouseComplete(id,ok,error,json))return;
    if(ScreenshotAssertionCompleteRequested(id,ok,error,json))return;
    if(ok&&!AssertionDeadlinePublicationCurrent(id))return;
    if(ok&&!ReferenceFrameOperationCurrent(id))return;
    if(ok)if(const auto bounds=element_bounds_work_.find(id);bounds!=element_bounds_work_.end()){const auto work=bounds->second;if(!ElementBoundsCurrent(work))return;}
    if(ok)if(const auto predicate=locator_predicate_work_.find(id);predicate!=locator_predicate_work_.end()){const auto work=predicate->second;if(!LocatorPredicateCurrent(work))return;}
    if(ok)if(const auto suggestion=locator_suggestion_work_.find(id);suggestion!=locator_suggestion_work_.end())if(!LocatorSuggestionCurrent(suggestion->second))return;
    if(ok)if(const auto snapshot=aria_json_work_.find(id);snapshot!=aria_json_work_.end())if(!AriaJSONCurrent(snapshot->second))return;
    const auto artifact_operation=operations_.Get(id);
    const uint64_t reference_generation=artifact_operation?artifact_operation->generation:0;
    if(ok&&artifact_input_configs_.contains(id)&&artifact_operation&&operations_.IsExpired(id,artifact_operation->generation,Now())){Complete(id,false,"Native artifact original deadline expired during rendering");return;}
    Dict committed_navigation;
    const auto navigation = navigations_.find(id);
    if (ok && navigation != navigations_.end()) {
      const auto frame = frame_states_.find(navigation->second.frame);
      if (frame != frame_states_.end()) committed_navigation = FrameJson(frame->second);
    }
    if (operations_.Contains(id) && std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && command_diagnostic_messages_++ < 4000)
      std::fprintf(stderr, "native completion page=%d id=%d ok=%d closing=%d navigation=%d observation=%d\n",
          page_id_, id, ok, closing_page_, navigations_.contains(id), expect_results_.contains(id));
    const auto form = form_clicks_.find(id);
    if (ok && form != form_clicks_.end() && operations_.Contains(id)) {
      auto done = std::move(form->second); form_clicks_.erase(form);
      CleanupInput(id); context_retries_.erase(id); done(); return;
    }
    if(ok&&!closing_page_&&network_&&network_publication_operations_.contains(id)){
      const auto operation=operations_.Get(id);const auto cursor=network_->QueuedPublication();
      if(operation&&cursor>network_->PublishedPublication()){
        const auto admitted=network_publication_results_.Hold(id,operation->generation,cursor,json);
        if(admitted!=suitecut::NativeNetworkPublicationResults::Admission::Full){WakeDeadlines();return;}
        Complete(id,false,"Native deferred network-publication results exceed 64 owners or 16 MiB");return;
      }
    }
    network_publication_results_.Cancel(id);network_publication_operations_.erase(id);
    if(ok&&!PublishReferenceFrameBatch(id))return;
    AssertionDeadlineForget(id);
    if (!operations_.Finish(id)) return;
    engine_.ForgetLocatorHandlerCommand(id);
    LocatorHandlerCancelRemoval(id);
    ScreenshotAssertionFinished(id);
    locator_suggestion_work_.erase(id);
    if(!ok&&native_highlights_)native_highlights_->Cancel(id,reference_generation);
    if(snapshot_references_)snapshot_references_->Cancel(id,reference_generation,Now());
    reference_frame_batches_.erase(id);
    if(native_virtual_time_)native_virtual_time_->Cancel(id,Now());
    native_virtual_time_close_fence_.Finished(id,reference_generation);NativeVirtualTimePumpClose();
    NativeTraceEnd(id,ok,error,json);
    engine_.recordings()->CancelCommand(id);
#if defined(OS_LINUX)
    engine_.streaming()->CancelCommand(id);
#endif
    if(native_coverage_)native_coverage_->Cancel(id);
    NativeDeveloperCancel(id);
    if(native_touch_)native_touch_->Cancel(id);
    WorkerCancel(id);
    ax_captures_.erase(id);
    ScreenshotComparisonCancel(id);
    artifact_configs_.erase(id); artifact_attempts_.erase(id); artifact_input_configs_.erase(id);
    FormCancelFolder(id);
    if(element_states_)element_states_->Cancel(id);
    if(selector_waits_)selector_waits_->Cancel(id);
    CancelConsoleMaterialization(id);
    action_pauses_.Cancel(id);
    LocatorHandlerCancel(id);
    url_predicates_.Cancel(id);
    url_waits_.erase(id);
    event_predicates_.Cancel(id);
    websocket_predicates_.erase(id);
    crash_wait_owners_.erase(id);
    action_navigations_.erase(id);
    NativeDropCancel(id);
    NativeManualCancel(id, !ok);
    NativeDragCancel(id);
    form_clicks_.erase(id); form_uploads_.erase(id);
    if(native_idle_)native_idle_->Cancel(id,reference_generation);
    engine_.downloads()->Cancel(id);
    if (artifacts_) artifacts_->Cancel(id);
    if(sockets_)sockets_->Cancel(id);
    if (network_) network_->Cancel(id);
    engine_.har_files().Cancel(id);
    if(const auto pending=har_pending_.find(id);pending!=har_pending_.end()){const auto route=pending->second;har_pending_.erase(pending);if(!ok&&!closing_page_&&network_)network_->UnbindReplay(route,[this](bool success,const std::string&reason){if(!success&&!closing_page_)NetworkFatal(reason);});}
    if (bindings_) { bindings_->ArgumentFinished(id,ok);bindings_->Cancel(id); }
    CleanupInput(id);
    RestoreLocatorModifiers(id);
    context_retries_.erase(id);
    locator_attempts_.erase(id);
    locator_logs_.erase(id); locator_waits_.erase(id);
    expect_results_.erase(id);
    first_observations_.erase(id); lazy_queries_.erase(id); context_retry_queued_.erase(id);
    count_queries_.erase(id); count_contexts_.erase(id);
    for (auto it = devtools_.begin(); it != devtools_.end();) {
      if (it->second.owner == id && !it->second.retain_after_cancel) it = devtools_.erase(it); else ++it;
    }
    navigation_waiters_.erase(id);
    navigations_.erase(id);
    body_waiters_.erase(id);
    native_dom_getters_.erase(id);
    element_bounds_operations_.erase(id);
    element_bounds_work_.erase(id);
    locator_predicate_work_.erase(id);locator_predicate_operations_.erase(id);
    aria_json_work_.erase(id);aria_json_operations_.erase(id);
    if(const auto scope=visibility_snapshots_.Get(id))visibility_snapshots_.Remove(id,scope->generation);
    operation_frames_.erase(id);
    const auto session = operation_sessions_[id];
    operation_sessions_.erase(id);
    event_waiters_.erase(id);
    if (navigation_ == id) {
      navigation_ = 0;
      if (!ok && browser_) browser_->StopLoad();
    }
    const auto pending_handle = pending_handle_groups_.find(id);
    if (pending_handle != pending_handle_groups_.end()) {
      auto params = CefDictionaryValue::Create(); params->SetString("objectGroup", pending_handle->second);
      pending_handle_groups_.erase(pending_handle);
      if (browser_) SendDevTools("Runtime.releaseObjectGroup", params, session);
    }
    const auto groups = evaluation_groups_.find(id);
    if (groups != evaluation_groups_.end()) {
      const auto sessions = groups->second; evaluation_groups_.erase(groups);
      for (const auto& session : sessions) if (browser_) {
        auto params = CefDictionaryValue::Create(); params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        SendDevTools("Runtime.releaseObjectGroup", params, session);
      }
    }
    const auto& failure_reason=(!ok&&network_fatal_&&network_fatal_->pending()&&!closed_reason_.empty())?closed_reason_:error;
    writer_.Reply(id, ok, failure_reason, std::vector<uint8_t>(json.begin(), json.end()), committed_navigation);
  }
  void WakeDeadlines() {
    if (!deadline_timer_active_) { deadline_timer_active_ = true;
      CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()), 10);
    }
  }
  bool BindingCommandsNeedTick() const {
    for (const auto& [id, call] : binding_calls_) if (!call.expired) return true;
    return false;
  }
  void Deadlines() {
    if(handle_disposals_)handle_disposals_->Tick(Now());
    if(snapshot_references_)snapshot_references_->Tick(Now());
    ScreenshotAssertionTick();
    NativeHighlightTick();
    NativeTraceTick();
    NativePristineTick();
    NativeDebuggerTick();
    NativeDropTick();
    if (!NativeDropBusy()) { StartInput("mouse"); StartInput("keyboard"); }
    NativeDeveloperTick();
    if(native_coverage_)native_coverage_->Tick(Now());
    if(page_errors_)page_errors_->Tick();
    if(element_states_)element_states_->Tick();
    if(selector_waits_)selector_waits_->Tick();
    AssertionDeadlineTick();
    OwnerFrameDocumentsTick();
    CheckNavigations(); CheckActionNavigations(); PumpInput(); PumpFormUploads();
    LocatorHandlerTick();
    if(sockets_)sockets_->Tick(Now());
    native_websocket_explicit_payloads_.Tick();
    NativeWebSocketRoutePump();
    PumpWebSocketPredicates();
    if(request_reads_)request_reads_->Tick(Now());
    if(private_response_)private_response_->Tick(Now());
    if(image_streams_)image_streams_->Tick(Now());
    if (network_) network_->Tick(Now());
    if(network_){auto ready=network_publication_results_.Ready(network_->PublishedPublication(),[this](int owner,uint64_t generation){return operations_.IsCurrent(owner,generation)&&!operations_.IsExpired(owner,generation,Now());});
      for(auto& result:ready){if(!operations_.IsCurrent(result.owner,result.generation))continue;network_publication_operations_.erase(result.owner);Complete(result.owner,true,"",result.json);}
    }
    if(network_fatal_)network_fatal_->Tick();
    bool network_expired=false;
    for(auto&[id,call]:network_calls_)if(!call.expired&&Now()>=call.deadline){call.expired=true;network_expired=true;}
    if(network_expired)NetworkFatal("Native network command deadline expired");
    WorkerTick();
    if(native_virtual_time_)native_virtual_time_->Tick(Now());
    if (bindings_) bindings_->Tick(Now());
    // Native command deadlines cancel logical registration ownership. Keep
    // physical installation callbacks until Chromium acknowledges them so a
    // late document script identifier can still be removed after cancellation.
    for (auto& [id, call] : binding_calls_) if (Now() >= call.deadline) call.expired = true;
    if (dialogs_) dialogs_->Tick(Now());
    for (const int id : operations_.Expired(Now())) {
      if(screenshot_assertion_retirement_.Contains(id)||assertion_deadlines_.Contains(id))continue;
      if(engine_.screenshot_comparisons()->Contains(page_id_,id)){engine_.screenshot_comparisons()->Deadline(page_id_,id);continue;}
      // A retrying assertion always gets its first DOM observation. Tiny user
      // deadlines bound retries; the initial transport round trip has a bounded
      // one-second watchdog and remains immediately cancellable.
      const auto first = first_observations_.find(id);
      if (first != first_observations_.end() && Now() < first->second) continue;
      const auto observed = expect_results_.find(id);
      if (observed != expect_results_.end()) { const auto snapshot = observed->second; Complete(id, true, "", snapshot); continue; }
      const auto url_wait=url_waits_.find(id);
      if(url_wait!=url_waits_.end()){Complete(id,false,url_wait->second.api+": Timeout exceeded while waiting for URL");continue;}
      const auto nav = navigations_.find(id);
      if (nav != navigations_.end()) {
        const auto error = nav->second.api + ": Timeout " + std::to_string(nav->second.timeout) +
          "ms exceeded.\nnavigating to \"" + nav->second.url + "\", waiting until \"" + nav->second.wait + "\"";
        Complete(id, false, error);
      } else if (!ArtifactTimeoutMessage(id).empty()) {
        Complete(id, false, ArtifactTimeoutMessage(id));
      } else if (locator_logs_.contains(id)) {
        Complete(id, false, "Native operation timed out (Timeout exceeded)\nCall log:\n - " + locator_logs_[id] +
          (locator_waits_.contains(id) ? "\n - waiting for " + locator_waits_[id] : ""));
      } else Complete(id, false, "Native operation timed out");
    }
    NativeVirtualTimePumpClose();
    RendererRecoveryHandlerTick();
    const bool non_network_deadlines_ready = (operations_.Size() == 0 || LocatorHandlerLogicalReady()) && !LocatorHandlerNeedsSourceTick() && !RendererRecoveryHandlerPending() && (!handle_disposals_ || !handle_disposals_->Size()) && (!snapshot_references_ || !snapshot_references_->NeedsTick()) && !NativeHighlightNeedsTick() && !NativeVirtualTimeNeedsTick() && !NativeTraceNeedsTick() && !NativePristineNeedsTick() && !NativeDebuggerNeedsTick() && !NativeDropNeedsTick() && !NativeDeveloperNeedsTick() && (!native_coverage_||!native_coverage_->NeedsTick()) && !WorkerNeedsTick() && pending_uploads_.empty() && network_calls_.empty() && (!dialogs_ || !dialogs_->Size()) && (!sockets_ || !sockets_->pending()) && native_routed_events_.empty() && !native_routed_waiter_ && !native_websocket_explicit_payloads_.Pending() && (!private_response_ || !private_response_->NeedsTick()) && (!image_streams_ || !image_streams_->NeedsTick()) && !BindingCommandsNeedTick() && (!page_errors_||!page_errors_->NeedsTick()) && (!element_states_||!element_states_->NeedsTick()) && (!selector_waits_||!selector_waits_->NeedsTick()) && !screenshot_assertion_retirement_.Size() && !OwnerFrameDocumentsNeedTick() && (!bindings_ || !bindings_->NeedsTick());
    if (!browser_ || (non_network_deadlines_ready && (!network_ || !network_->NeedsTick()))) { deadline_timer_active_ = false; return; }
    const bool passive_network_monitor = non_network_deadlines_ready && network_ &&
      network_->NeedsTick() && !network_->NeedsTick(true) && AcceptedWorkReady(true);
    // One short-lived timer for the whole registry. Completed operations must
    // not retain one delayed Source reference per command until their timeout.
    CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
      self->Deadlines();
    },passive_network_monitor ? suitecut::AcceptedTaskWork::Kind::PassiveNetworkMonitor : suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()), 10);
  }
  int AllocateDevToolsId() {
    return !browser_ || next_devtools_ == std::numeric_limits<int>::max() ? 0 : ++next_devtools_;
  }
  bool SubmitDevTools(int id, const std::string& method, Dict params, const std::string& session) {
    if (!id || !browser_) return false;
    const bool initial_tree=native_frame_debugger_trees_.Contains(id);
    const auto tree_attachment=native_frame_debugger_trees_.ExpectedAttachment(id,session);
    auto message = CefDictionaryValue::Create(); message->SetInt("id", id); message->SetString("method", method);
    if (params) message->SetDictionary("params", params->Copy(false));
    if (!session.empty()) message->SetString("sessionId", session);
    const auto json = Json(message);
    const bool final_release = method == "Input.dispatchMouseEvent" && params && params->GetString("type") == "mouseReleased";
    NativeInspectorReserve(id,session);
    if (devtools_observer_depth_) {const bool queued=QueueDeferredDevTools(id,json,final_release,session,initial_tree,tree_attachment);if(!queued)NativeInspectorNeverDispatched(id);return queued;}
    if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && !session.empty() && diagnostic_messages_++ < 2000) {
      if(method=="Fetch.continueWithAuth")std::fprintf(stderr,"native sent session command: Fetch.continueWithAuth (credentials redacted)\n");
      else std::fprintf(stderr, "native sent session command: %s\n", json.substr(0, 2048).c_str());
    }
    // Passive receipt provenance only. Root startup submissions reach this
    // direct dispatch path; queued commands are not claimed by this marker.
    if (session.empty() && method == "Network.enable" &&
        std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000) {
      // Only the first actual direct root dispatch owns this diagnostic scalar.
      // A refused send produces no reply marker; no logical readiness uses it.
      if(!diagnostic_network_enable_)diagnostic_network_enable_=id;
      std::fprintf(stderr,"native Network.enable dispatch page=%d browser=%d id=%d session=root\n",page_id_,browser_->GetIdentifier(),id);
    }
    if (final_release) MarkFinalReleaseSubmitted(id);
    if(!NativeFrameDebuggerBeforeSend(id,session,initial_tree,tree_attachment)){NativeInspectorNeverDispatched(id);return false;}
    const auto actual_browser=browser_;if(!actual_browser){NativeInspectorNeverDispatched(id);return false;}
    return NativeInspectorCall(id,session,[actual_browser,&json]{return actual_browser->GetHost()->SendDevToolsMessage(json.data(),json.size());});
  }
  void MarkFinalReleaseSubmitted(int command) {
    const auto tool = devtools_.find(command); if (tool == devtools_.end()) return;
    const auto input = input_work_.find(tool->second.owner); if (input == input_work_.end()) return;
    const auto& work = input->second;
    work->held_mouse = false;
    const int total = work->message->GetType("clickCount") == VTYPE_INT ? work->message->GetInt("clickCount") : 1;
    if (work->kind == "mouse" && work->action == "click" && work->phase == 2 && work->count == total)
      work->final_release_submitted = true;
  }
  int SendDevTools(const std::string& method, Dict params, const std::string& session = "") {
    const int id = AllocateDevToolsId();
    return SubmitDevTools(id, method, params, session) ? id : 0;
  }
  void EnableSession(const std::string& session) {
    SendDevTools("Page.enable", nullptr, session);
    auto life = CefDictionaryValue::Create(); life->SetBool("enabled", true);
    SendDevTools("Page.setLifecycleEventsEnabled", life, session);
    auto network = CefDictionaryValue::Create(); network->SetInt("maxTotalBufferSize", 128 * 1048576);
    network->SetInt("maxResourceBufferSize", 128 * 1048576);network->SetInt("maxPostDataSize",2097152);
    if(session.empty())NativeHARPrimaryEnable(network);else SendDevTools("Network.enable",network,session);
    if (network_ && session.empty()) network_->SessionAttached(session);
    if (bindings_ && session.empty()) bindings_->SessionAttached(session);
    SendDevTools("Runtime.enable", nullptr, session); SendDevTools("Log.enable", nullptr, session);
    if (session.empty()) {
      auto focus = CefDictionaryValue::Create(); focus->SetBool("enabled", true);
      SendDevTools("Emulation.setFocusEmulationEnabled", focus);
    }
  }
  void AutoAttach(const std::string& session) {
    auto params = CefDictionaryValue::Create(); params->SetBool("autoAttach", true);
    params->SetBool("waitForDebuggerOnStart", true); params->SetBool("flatten", true);
    SendDevTools("Target.setAutoAttach", params, session);
  }
  void DevTools(int owner, const std::string& method, Dict params, DevToolsCallback callback, std::optional<std::string> target_session = std::nullopt, bool retained_control = false, size_t response_limit = kMaxResponseBytes, bool lossless_scalars = false) {
    const auto handler_owner=operations_.Get(owner);
    auto handler_native_origin=engine_.LocatorHandlerCommandOrigin(owner,page_id_,handler_owner?handler_owner->generation:0);
    if(const auto input=input_work_.find(owner);input!=input_work_.end())handler_native_origin=input->second->native_origin;
    const suitecut::NativeWorkOriginFrame handler_origin(handler_native_origin);
    if (!browser_ || !operations_.Contains(owner)) return;
    if(ScreenshotAssertionRoute(owner,method,params,SuiteCutArtifactController::ResponseStage::Unknown,callback,target_session))return;
    if(const auto entry=operations_.Get(owner);entry&&AssertionDeadlineExpire(owner,entry->generation))return;
    if(!ReferenceFrameOperationCurrent(owner))return;
    if(native_dom_getters_.contains(owner)||element_bounds_operations_.contains(owner)||locator_predicate_operations_.contains(owner)||aria_json_operations_.contains(owner)){
      const auto entry=operations_.Get(owner);
      if(!entry||operations_.IsExpired(owner,entry->generation,Now())){Complete(owner,false,"Native operation timed out");return;}
    }
    if(const auto bounds=element_bounds_work_.find(owner);bounds!=element_bounds_work_.end())if(const auto work=bounds->second;work&&!ElementBoundsCurrent(work))return;
    if(const auto predicate=locator_predicate_work_.find(owner);predicate!=locator_predicate_work_.end())if(!LocatorPredicateCurrent(predicate->second))return;
    if(const auto snapshot=aria_json_work_.find(owner);snapshot!=aria_json_work_.end())if(!AriaJSONCurrent(snapshot->second))return;
    // DOM node description/resolution carry native DOM schema fields. Node
    // strings may contain isolated units even when only backend IDs are used.
    if(method=="DOM.describeNode"||method=="DOM.resolveNode")lossless_scalars=true;
    if(element_states_&&element_states_->Contains(owner)&&method=="Runtime.callFunctionOn"){
      const auto entry=operations_.Get(owner);auto physical=element_states_->Begin(owner);
      if(!entry||!physical){Complete(owner,false,"Native element-state physical capacity exceeded");return;}
      retained_control=true;
      callback=[this,owner,generation=entry->generation,physical,original=std::move(callback)](bool ok,const std::string& json)mutable{
        physical->Settle();if(operations_.IsCurrent(owner,generation))original(ok,json);
      };
    }
    if(selector_waits_&&selector_waits_->Contains(owner)){
      const auto wait_session=target_session.value_or(operation_sessions_[owner]);
      if(!selector_waits_->Group(owner,"suitecut-automation-"+std::to_string(owner),wait_session)||
         (params&&params->GetType("objectGroup")==VTYPE_STRING&&!selector_waits_->Group(owner,params->GetString("objectGroup").ToString(),wait_session))){Complete(owner,false,"Native selector wait object-group capacity exceeded");return;}
      // This family transfers the duplicate generic eval tracker before the
      // actual call is submitted, so cancellation cannot bypass the held
      // selector lease with a fire-and-forget release of the same group.
      if((locator_suggestion_work_.contains(owner)||element_bounds_operations_.contains(owner)||locator_predicate_operations_.contains(owner)||aria_json_operations_.contains(owner)||assertion_deadlines_.Contains(owner))&&!ReferenceTemporaryGroups(owner)){Complete(owner,false,"Native element observation temporary-group ownership unavailable");return;}
      const auto entry=operations_.Get(owner);auto physical=selector_waits_->Begin(owner);
      if(!entry||!physical){Complete(owner,false,"Native selector-wait physical capacity exceeded: "+method);return;}
      retained_control=true;
      callback=[this,owner,generation=entry->generation,physical,original=std::move(callback)](bool ok,const std::string& json)mutable{
        physical->Settle();if(operations_.IsCurrent(owner,generation)&&(!selector_waits_->Retrying(owner)||assertion_deadlines_.Get(owner,generation)))original(ok,json);
      };
    }
    const auto session = target_session.value_or(operation_sessions_[owner]);
    const int assigned = AllocateDevToolsId();
    const bool artifact_physical = artifacts_ && artifacts_->PhysicalPending(owner) && (method == "Page.captureScreenshot" || method == "Page.printToPDF");
    uint64_t artifact_generation=0;
    if(const auto config=artifact_input_configs_.find(owner);config!=artifact_input_configs_.end()){
      const auto entry=operations_.Get(owner);
      if(!entry||operations_.IsExpired(owner,entry->generation,Now())){
        if(artifact_physical)artifacts_->PhysicalDispatchFailed(owner);
        Complete(owner,false,"Native artifact original deadline expired before dispatch");return;
      }
      retained_control=true;
      artifact_generation=entry->generation;
      callback=suitecut::RetainArtifactConfiguration(config->second,
        [this,owner,generation=entry->generation]{return operations_.IsCurrent(owner,generation);},
        [this,owner,generation=entry->generation]{return operations_.IsExpired(owner,generation,Now());},
        [this,owner]{Complete(owner,false,"Native artifact original deadline expired during rendering");},
        std::move(callback),artifact_physical);
    }
    const bool send_keys_preparation = RetainsKeyboardCall(owner,method);
    if (send_keys_preparation) retained_control = true;
    const bool physical=artifact_physical||retained_control;
    if (!assigned) { if (artifact_physical) artifacts_->PhysicalDispatchFailed(owner); Complete(owner, false, "Could not allocate DevTools command"); return; }
    if (send_keys_preparation && !RetainKeyboardCall(owner,method,params,callback)) return;
    // CEF may deliver a same-document result synchronously during submission.
    // Register ownership before calling into it, including nested callbacks.
    devtools_.emplace(assigned, DevToolsCall{owner, std::move(callback), session, physical, std::min(response_limit,kMaxResponseBytes), retained_control, lossless_scalars, artifact_generation, artifact_physical&&artifact_generation!=0});
    if (!SubmitDevTools(assigned, method, params, session)) {
      devtools_.erase(assigned); if (send_keys_preparation) SendKeysPhysicalFinished(owner); if (artifact_physical) artifacts_->PhysicalDispatchFailed(owner); Complete(owner, false, "Could not dispatch DevTools command");
    }
  }
  void Evaluate(int owner, const std::string& expression, DevToolsCallback callback, const std::string& frame = "", bool utility = false, const std::string& assertion_matcher = "") {
    WithFrame(owner, frame.empty() ? main_frame_id_ : frame, [this, owner, expression, frame, assertion_matcher, callback = std::move(callback)](int context) {
    if(!assertion_matcher.empty()){const auto entry=operations_.Get(owner);if(!entry||!AssertionDeadlineDispatch(owner,entry->generation,assertion_matcher,frame.empty()?main_frame_id_:frame))return;}
    evaluation_groups_[owner].insert(operation_sessions_[owner]);
    auto params = CefDictionaryValue::Create();
    params->SetString("expression", expression);
    params->SetInt("contextId", context);
    params->SetBool("awaitPromise", true);
    params->SetBool("returnByValue", true);
    params->SetBool("userGesture", true);
    params->SetString("objectGroup", "suitecut-automation-" + std::to_string(owner));
    DevTools(owner, "Runtime.evaluate", params, std::move(callback));
    }, utility);
  }
  void ResolveFramePath(int id, uint64_t generation, Dict message, size_t index, const std::string& frame, std::function<void(Dict)> done, std::function<void()> unavailable={}) {
    if (!operations_.IsCurrent(id, generation)) return;
    auto path = message->GetList("framePath");
    if (!path || index == path->GetSize()) {
      auto resolved = message->Copy(false); resolved->SetString("frame", frame); resolved->Remove("framePath"); done(resolved); return;
    }
    if (path->GetSize() > 32 || path->GetType(index) != VTYPE_LIST) { Complete(id, false, "Invalid native frame locator path"); return; }
    auto stage = CefDictionaryValue::Create(); stage->SetList("query", message->GetList("framePath")->GetList(index)->Copy());
    const bool utility = !QueryNeedsMainWorld(stage);
    WithFrame(id, frame, [this, id, generation, message, index, frame, stage, done = std::move(done),unavailable](int context) {
      auto params = CefDictionaryValue::Create(); params->SetInt("contextId", context);
      params->SetString("expression", "(()=>{const nodes=" + QueryExpression(stage) + ";if(!nodes.length)return null;if(nodes.length>1)throw new Error('Strict frame locator matched multiple elements');if(!['iframe','frame'].includes(nodes[0].localName))throw new Error('Frame locator resolved to an element that is not an iframe');return nodes[0];})()");
      params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
      evaluation_groups_[id].insert(operation_sessions_[id]);
      auto callback=[this, id, generation, message, index, frame, done,unavailable](bool, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto node = result ? result->GetDictionary("result") : nullptr;
        if (!node || result->HasKey("exceptionDetails")) { Complete(id, false, "Native frame locator resolution failed"); return; }
        if (node->GetString("subtype") == "null") {
          if(unavailable){unavailable();return;}
          if (count_queries_.contains(id)) { CompleteCountZero(id); return; }
          if (visibility_snapshots_.Contains(id)) { CompleteVisibilityEmpty(id); return; }
          CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation, message, index, frame, done,unavailable] { self->ResolveFramePath(id, generation, message, index, frame, done,unavailable); }), 20); return;
        }
        if (node->GetType("objectId") != VTYPE_STRING) { Complete(id, false, "Native iframe owner unavailable"); return; }
        auto params = CefDictionaryValue::Create(); params->SetString("objectId", node->GetString("objectId"));
        DevTools(id, "DOM.describeNode", params, [this, id, generation, message, index, frame, done,unavailable](bool, const std::string& json) {
          auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
          auto node = result ? result->GetDictionary("node") : nullptr;
          const auto child = node ? node->GetString("frameId").ToString() : "";
          if (child.empty() || !frame_states_.contains(child)) {
            if(unavailable){unavailable();return;}
            if (count_queries_.contains(id)) { CompleteCountZero(id); return; }
            if (visibility_snapshots_.Contains(id)) { CompleteVisibilityEmpty(id); return; }
            CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation, message, index, frame, done,unavailable] { self->ResolveFramePath(id, generation, message, index, frame, done,unavailable); }), 20); return;
          }
          ResolveFramePath(id, generation, message, index + 1, child, done,unavailable);
        });
      };
      if(QueryUsesMultipleWorlds(stage)){auto config=stage->Copy(false);config->SetString("frame",frame);QueryInspect(id,config,"if(!nodes.length)return null;if(nodes.length>1)throw new Error('Strict frame locator matched multiple elements');if(!['iframe','frame'].includes(nodes[0].localName))throw new Error('Frame locator resolved to an element that is not an iframe');return nodes[0];",true,false,std::move(callback));}
      else DevTools(id,"Runtime.evaluate",params,std::move(callback),std::nullopt,false,kMaxResponseBytes,true);
    }, utility);
  }
  void MapFramePoint(int id, const std::string& frame, double x, double y, double width, double height, std::function<void(double,double)> done) {
    if (!operations_.Contains(id)) return;
    const auto current = frame_states_.find(frame);
    if (current == frame_states_.end()) { Complete(id, false, "Native frame detached"); return; }
    if (current->second.parent.empty()) { done(x, y); return; }
    const auto parent = frame_states_.find(current->second.parent);
    if (parent == frame_states_.end() || width <= 0 || height <= 0) { Complete(id, false, "Native frame geometry unavailable"); return; }
    const auto parent_session = parent->second.session;
    const auto root = parent_session.empty() ? main_frame_id_ : sessions_[parent_session];
    operation_sessions_[id] = parent_session;
    auto params = CefDictionaryValue::Create(); params->SetString("frameId", frame);
    DevTools(id, "DOM.getFrameOwner", params, [this, id, root, x, y, width, height, done](bool, const std::string& json) {
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
      if (!result || result->GetType("backendNodeId") != VTYPE_INT) { Complete(id, false, "Native frame owner geometry unavailable"); return; }
      auto params = CefDictionaryValue::Create(); params->SetInt("backendNodeId", result->GetInt("backendNodeId"));
      DevTools(id, "DOM.getBoxModel", params, [this, id, root, x, y, width, height, done](bool, const std::string& json) {
        auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
        auto model = result ? result->GetDictionary("model") : nullptr; auto quad = model ? model->GetList("content") : nullptr;
        if (!quad || quad->GetSize() != 8) { Complete(id, false, "Native frame content quad unavailable"); return; }
        double q[8]; for (size_t index = 0; index < 8; ++index) q[index] = quad->GetType(index) == VTYPE_INT ? quad->GetInt(index) : quad->GetDouble(index);
        const double dx1=q[2]-q[4], dx2=q[6]-q[4], dx3=q[0]-q[2]+q[4]-q[6];
        const double dy1=q[3]-q[5], dy2=q[7]-q[5], dy3=q[1]-q[3]+q[5]-q[7];
        const double determinant=dx1*dy2-dx2*dy1;
        double g=0,h=0;
        if (std::abs(dx3)>1e-9 || std::abs(dy3)>1e-9) {
          if (std::abs(determinant)<1e-12) { Complete(id, false, "Native frame transform is degenerate"); return; }
          g=(dx3*dy2-dx2*dy3)/determinant; h=(dx1*dy3-dx3*dy1)/determinant;
        }
        const double u=x/width,v=y/height,divisor=g*u+h*v+1;
        if (std::abs(divisor)<1e-12) { Complete(id, false, "Native frame transform is degenerate"); return; }
        const double mapped_x=((q[2]-q[0]+g*q[2])*u+(q[6]-q[0]+h*q[6])*v+q[0])/divisor;
        const double mapped_y=((q[3]-q[1]+g*q[3])*u+(q[7]-q[1]+h*q[7])*v+q[1])/divisor;
        if (root == main_frame_id_) { done(mapped_x,mapped_y); return; }
        Evaluate(id, "({width:innerWidth,height:innerHeight})", [this,id,root,mapped_x,mapped_y,done](bool,const std::string& json) {
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
          auto remote=result?result->GetDictionary("result"):nullptr;auto viewport=remote?remote->GetDictionary("value"):nullptr;
          if (!viewport) { Complete(id,false,"Native parent viewport unavailable");return; }
          MapFramePoint(id,root,mapped_x,mapped_y,Number(viewport,"width"),Number(viewport,"height"),done);
        },root,true);
      });
    });
  }
  Dict QueryOptions(bool metadata = false) {
    auto options = CefDictionaryValue::Create(); auto engines = CefDictionaryValue::Create();
    for (const auto& [name, entry] : engine_.selectors().entries()) {
      auto engine = CefDictionaryValue::Create(); engine->SetString("source",entry.source);
      engine->SetBool("contentScript",entry.content_script); engines->SetDictionary(name,engine);
    }
    options->SetDictionary("engines",engines);
    options->SetString("registryKey","__suitecut_selectors_"+handle_namespace_);
    if (metadata) options->SetBool("metadata",true);
    return options;
  }
  bool QueryNeedsSelectorWorld(Dict message, bool isolated) {
    if (engine_.selectors().entries().empty()) return false;
    std::vector<CefRefPtr<CefValue>> pending;
    if (const auto query = message->GetValue("query")) pending.push_back(query);
    if ((message->HasKey("selector") || message->HasKey("selectorJSON"))) {
      const auto selector = suitecut::InboundString(message,"selector");const auto names=SuiteCutQueryRealms::Engines(selector);
      for (const auto& [name, entry] : engine_.selectors().entries()) if (entry.content_script == isolated && std::find(names.begin(),names.end(),name)!=names.end()) return true;
    }
    size_t visited = 0;
    while (!pending.empty() && ++visited <= 100000) {
      const auto value = pending.back(); pending.pop_back();
      if (value->GetType() == VTYPE_LIST) {
        const auto list = value->GetList(); for (size_t index = 0; index < list->GetSize(); ++index) pending.push_back(list->GetValue(index));
      } else if (value->GetType() == VTYPE_DICTIONARY) {
        const auto node = value->GetDictionary();
        if (node->GetString("engine") == "custom" && node->GetType("name") == VTYPE_STRING) {
          const auto entry = engine_.selectors().entries().find(node->GetString("name").ToString());
          if (entry != engine_.selectors().entries().end() && entry->second.content_script == isolated) return true;
        }
        if ((node->HasKey("selector") || node->HasKey("selectorJSON"))) {
          const auto selector = suitecut::InboundString(node,"selector");const auto names=SuiteCutQueryRealms::Engines(selector);
          for (const auto& [name, entry] : engine_.selectors().entries()) if (entry.content_script == isolated && std::find(names.begin(),names.end(),name)!=names.end()) return true;
        }
        CefDictionaryValue::KeyList keys; node->GetKeys(keys);
        for (const auto& key : keys) pending.push_back(node->GetValue(key));
      }
    }
    return false;
  }
  bool QueryNeedsMainWorld(Dict message) { return QueryNeedsSelectorWorld(message,false); }
  bool QueryNeedsIsolatedWorld(Dict message) { return QueryNeedsSelectorWorld(message,true); }
  bool QueryUsesMultipleWorlds(Dict message) { return QueryHasSnapshotReferences(message)||(QueryNeedsMainWorld(message)&&QueryNeedsIsolatedWorld(message)); }
  void QueryNodesInMainWorld(int id,Dict message,std::function<void(const std::string&)> done) {QueryNodesInWorld(id,message,false,std::move(done));}
  void QueryNodesInWorld(int id,Dict message,bool to_utility,std::function<void(const std::string&)> done) {
    if(!PrepareSnapshotReferenceScope(id,message))return;
    const auto entry=operations_.Get(id);if(!entry)return;
    if(!SelectorWaits().Admit(id,entry->generation)){Complete(id,false,"Native selected-node physical preparation capacity exceeded");return;}
    if(QueryBuiltinNodesInWorld(id,message,to_utility,done))return;
    const auto frame_id=FrameId(message);
    WithFrame(id,frame_id,[this,id,generation=entry->generation,frame_id,message,to_utility,done=std::move(done)](int utility)mutable{
      const auto frame=frame_states_.find(frame_id);if(frame==frame_states_.end()||!frame->second.context){Complete(id,false,"Native query main context unavailable");return;}
      const int main=frame->second.context,destination=to_utility?utility:main;const auto loader=frame->second.loader,session=frame->second.session;
      const auto group="suitecut-automation-"+std::to_string(id);evaluation_groups_[id].insert(session);
      const auto reference_document=QueryHasSnapshotReferences(message)?ReferenceDocument(frame_id):std::optional<SnapshotReferenceDocument>{};
      if(QueryHasSnapshotReferences(message)&&!reference_document){Complete(id,false,"Native snapshot reference plan document unavailable");return;}
      auto current=[this,id,generation,frame_id,main,utility,loader,session,reference_document]{const auto frame=frame_states_.find(frame_id);return (!reference_document||ReferencePlanCurrent(id,generation,*reference_document))&&operations_.IsCurrent(id,generation)&&frame!=frame_states_.end()&&frame->second.context==main&&frame->second.utility_context==utility&&frame->second.loader==loader&&frame->second.session==session;};
      auto normalize=[this,id,generation,frame_id,message,current,main,utility,destination,group,session,done=std::move(done)](const std::string& root)mutable{
      auto query=message->GetValue("query");
      if(!query||query->GetType()!=VTYPE_LIST){auto steps=CefListValue::Create();auto step=CefDictionaryValue::Create();step->SetString("engine","css");suitecut::InboundCopyString(message,step,"selector");steps->SetDictionary(0,step);query=CefValue::Create();query->SetList(steps);}
      auto options=QueryOptions();options->SetBool("planOnly",true);auto literal=CefValue::Create();literal->SetString(Json(options));
      auto params=CefDictionaryValue::Create();params->SetInt("contextId",utility);params->SetBool("returnByValue",true);
      params->SetString("expression","("+std::string(kSuiteCutDomQuery)+")("+CefWriteJSON(query,JSON_WRITER_DEFAULT).ToString()+",document,JSON.parse("+CefWriteJSON(literal,JSON_WRITER_DEFAULT).ToString()+"))");
      if(message->HasKey("nativeSelectorWaitState")){
        const auto state=message->GetString("nativeSelectorWaitState").ToString();
        const auto expression=params->GetString("expression").ToString();
        params->SetString("expression","(()=>{const plan="+expression+";plan.push({engine:'selector-wait',state:'"+state+"',strict:"+std::string(message->GetBool("strict")?"true":"false")+"});return plan;})()");
      }
      DevTools(id,"Runtime.evaluate",params,[this,id,generation,current,main,utility,destination,group,session,root,grouped=message->HasKey("nativeSelectorWaitState"),done=std::move(done)](bool ok,const std::string& json)mutable{
        if(!current()){if(operations_.IsCurrent(id,generation)){if(grouped&&frame_states_.contains(operation_frames_[id]))RetryContext(id);else Complete(id,false,"Native query document changed during plan normalization");}return;}
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
        if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetType("value")!=VTYPE_LIST){auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto exception=details?details->GetDictionary("exception"):nullptr;Complete(id,false,exception?exception->GetString("description").ToString():"Native selector plan normalization failed");return;}
        SuiteCutQueryWorldPlan::Run(
          [this,id,session](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result result){const bool metadata=params&&params->HasKey("serializationOptions");DevTools(id,method,params,std::move(result),session,false,metadata?16*1048576:kMaxResponseBytes,metadata||method=="Runtime.getProperties");},current,
          [this,id,generation,current,grouped](const std::string& error){if(operations_.IsCurrent(id,generation)){if(grouped&&!current()&&frame_states_.contains(operation_frames_[id]))RetryContext(id);else Complete(id,false,error);}},main,utility,group,kSuiteCutDomQuery,QueryOptionsExpression(),remote->GetList("value"),
          [this](const std::string& name){const auto found=engine_.selectors().entries().find(name);return found!=engine_.selectors().entries().end()&&found->second.content_script;},
          [this,id,generation,current,grouped,destination,group,session,done=std::move(done)](SuiteCutQueryWorldPlan::Nodes nodes)mutable{
            if(!current()){if(operations_.IsCurrent(id,generation))Complete(id,false,"Native query document changed before selected-node publication");return;}
            if(nodes.context==destination){done(nodes.object);return;}
            SuiteCutQueryNodeTransfer::Transfer([this,id,session](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result result){const bool metadata=params&&params->HasKey("serializationOptions");DevTools(id,method,params,std::move(result),session,false,metadata?16*1048576:kMaxResponseBytes,metadata||method=="Runtime.getProperties");},current,[this,id,generation,current,grouped](const std::string& error){if(operations_.IsCurrent(id,generation)){if(grouped&&!current()&&frame_states_.contains(operation_frames_[id]))RetryContext(id);else Complete(id,false,error);}},nodes.object,destination,group,std::move(done));
          },grouped,root,[this,id](const std::string& token,SuiteCutQueryWorldPlan::Nodes scope,std::function<void(std::string)> done){ResolveSnapshotReference(id,token,scope,std::move(done));});
      });
      };
      if(message->HasKey("nativeRootHandle")){
        if(message->GetType("nativeRootHandle")!=VTYPE_INT){Complete(id,false,"Invalid native selector root handle");return;}
        const auto found=handles_.find(message->GetInt("nativeRootHandle"));
        if(found==handles_.end()){Complete(id,false,MissingHandle(message->GetInt("nativeRootHandle")));return;}
        const auto& handle=found->second;
        if(handle.node_id.empty()||handle.frame_id!=frame_id||handle.context!=main||handle.session!=session){Complete(id,false,"Native selector root belongs to a different or destroyed DOM context");return;}
        SuiteCutQueryNodeTransfer::AdoptNode(
          [this,id,session](const std::string& method,Dict params,SuiteCutQueryNodeTransfer::Result callback){DevTools(id,method,params,std::move(callback),session);},current,
          [this,id](const std::string& error){Complete(id,false,error);},handle.node_id,utility,group,
          [normalize=std::move(normalize)](Dict node)mutable{normalize(node->GetString("objectId").ToString());});
      }else normalize("");
    },true);
  }
  void QueryInspect(int id,Dict message,const std::string& body,bool utility,bool by_value,DevToolsCallback callback) {
    QueryNodesInWorld(id,message,utility,[this,id,body,by_value,callback=std::move(callback)](const std::string& nodes)mutable{
      auto params=CefDictionaryValue::Create();params->SetString("objectId",nodes);params->SetString("functionDeclaration","async function(){const nodes=this;"+body+"}");
      params->SetBool("awaitPromise",true);params->SetBool("returnByValue",by_value);params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
      DevTools(id,"Runtime.callFunctionOn",params,std::move(callback),std::nullopt,false,kMaxResponseBytes,!by_value);
    });
  }
  std::string QueryOptionsExpression(bool metadata = false) {
    auto literal = CefValue::Create(); literal->SetString(Json(QueryOptions(metadata)));
    return "JSON.parse(" + CefWriteJSON(literal, JSON_WRITER_DEFAULT).ToString() + ")";
  }
  // All callers execute this expression over the already selected utility
  // nodes. Candidate aliases use only authored builtin engines and a single
  // aggregate work budget; registered selector factories are never reentered.
  std::string QueryDiagnosticExpression(Dict message,const std::string& nodes) {
    const auto expression=message->GetType("diagnosticExpressionJSON")==VTYPE_STRING&&message->GetString("diagnosticExpressionJSON").length()<=16384
      ?message->GetString("diagnosticExpressionJSON").ToString():std::string("\"\"");
    auto query=message->GetList("query");if(!query)query=CefListValue::Create();
    auto value=CefValue::Create();value->SetList(query->Copy());
    const auto original=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();
    return "(()=>{const query=("+std::string(kSuiteCutDomQuery)+"),budget={remaining:40000},options={nativeDiagnosticsBudget:budget},tools=query([],document,{...options,accessibilityTools:true});return ("+std::string(kSuiteCutQueryDiagnostics)+")("+nodes+",JSON.parse("+Literal(expression)+"),{originalQuery:JSON.parse("+Literal(original)+"),query:steps=>query(steps,document,options),role:tools.role,name:tools.name,remaining:()=>budget.remaining});})()";
  }
  std::string QueryExpression(Dict message,const std::string& native_query_intrinsics="") {
    if(QueryNeedsMainWorld(message)&&QueryNeedsIsolatedWorld(message))return "(()=>{throw new Error('Mixed main and isolated native custom selector worlds require native step execution');})()";
    auto query = message->GetValue("query");
    if (!query || query->GetType() != VTYPE_LIST) {
      auto list = CefListValue::Create(); auto step = CefDictionaryValue::Create();
      step->SetString("engine", "css"); suitecut::InboundCopyString(message,step,"selector"); list->SetDictionary(0, step);
      query = CefValue::Create(); query->SetList(list);
    }
    // The optional fourth argument is Source-authored, never an IPC/options
    // property that author code can inherit or provide.
    return "(" + std::string(kSuiteCutDomQuery) + ")(" + CefWriteJSON(query, JSON_WRITER_DEFAULT).ToString() + ",document," + QueryOptionsExpression() +
      (native_query_intrinsics.empty()?std::string():","+native_query_intrinsics) + ")";
  }
  void EvaluateFixedNode(int id, Dict config, const std::string& expression, DevToolsCallback callback) {
    const int handle = config->GetInt("nativeHandle"); const auto found = handles_.find(handle);
    if (found == handles_.end()) { Complete(id,false,MissingHandle(handle));return; }
    if (found->second.node_id.empty()) { Complete(id,false,"Native handle is not a DOM node");return; }
    const auto frame_id=found->second.frame_id; const auto node_id=found->second.node_id;
    WithFrame(id,frame_id,[this,id,node_id,expression,callback=std::move(callback)](int context) mutable {
      evaluation_groups_[id].insert(operation_sessions_[id]);
      auto params=CefDictionaryValue::Create();params->SetString("objectId",node_id);
      DevTools(id,"DOM.describeNode",params,[this,id,context,expression,callback=std::move(callback)](bool,const std::string& json) mutable {
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto node=result?result->GetDictionary("node"):nullptr;
        if(!node){Complete(id,false,"Native DOM node unavailable");return;}
        auto params=CefDictionaryValue::Create();params->SetInt("backendNodeId",node->GetInt("backendNodeId"));params->SetInt("executionContextId",context);
        params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
        DevTools(id,"DOM.resolveNode",params,[this,id,expression,callback=std::move(callback)](bool,const std::string& json) mutable {
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
          auto node=result?result->GetDictionary("object"):nullptr;
          if(!node||node->GetType("objectId")!=VTYPE_STRING){Complete(id,false,"Native DOM node resolution failed");return;}
          auto params=CefDictionaryValue::Create();params->SetString("objectId",node->GetString("objectId"));
          params->SetString("functionDeclaration","async function(){return await("+expression+");}");params->SetBool("awaitPromise",true);params->SetBool("returnByValue",true);
          params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
          DevTools(id,"Runtime.callFunctionOn",params,std::move(callback));
        });
      });
    },true);
  }
  #include "send_keys.inc"
  #include "targeted_keyboard.inc"
  #include "send_keys_sequence.inc"
  #include "scroll_controller.inc"
  #include "select_text.inc"
  #include "blur.inc"
  #include "pristine_dispatch_source.inc"
  #include "dispatch_event.inc"
  #include "pristine_dispatch_actions.inc"
  #include "input_value.inc"
  void LocatorAction(int id, std::uint64_t generation, const std::string& selector,
                     const std::string& action, const std::string& text, Dict config,
                     std::shared_ptr<InputWork> pointer_work = nullptr) {
    if (!operations_.IsCurrent(id, generation) || !browser_) return;
    if (pointer_work && operations_.IsExpired(id,generation,Now())) {
      Complete(id,false,"Native pointer original deadline expired before press"); return;
    }
    // A renderer/context retry can race the delayed preparation retry. Once a
    // pointer owner is queued, its final recheck owns preparation; never admit
    // a second gesture for the same still-live operation.
    if (!pointer_work && action=="click" && input_work_.contains(id)) return;
    if (action == "press" || action == "type") {
      if (operations_.IsExpired(id,generation,Now())) { Complete(id,false,"Native targeted keyboard original deadline expired"); return; }
      const auto found=input_work_.find(id);
      if(found==input_work_.end()){BeginTargetedKeyboard(id,generation,config,action,text);return;}
      if(found->second->generation!=generation||!found->second->keyboard_target||found->second->action!=action){Complete(id,false,"Native targeted keyboard owner mismatch");return;}
      if(keyboard_input_!=id)return;
    }
    if (action == "selectText") { SelectTextAction(id,generation,config); return; }
    if (action == "blur") { BlurAction(id,generation,config); return; }
    if (action == "dispatchEvent") { DispatchEventAction(id,generation,config); return; }
    if (action == "inputValue") { InputValueAction(id,generation,config); return; }
    if (action == "boundingBox") { ElementBoundsAction(id,generation,config); return; }
    if (action == "waitForFunction") { LocatorPredicateAction(id,generation,config); return; }
    if (action == "ariaSnapshotJSON") { AriaJSONAction(id,generation,config); return; }
    if (action == "scrollIntoViewIfNeeded") { ScrollAction(id,generation,config); return; }
    if(action=="fill"||action=="clear"){
      if(operations_.IsExpired(id,generation,Now())){Complete(id,false,action+": Native operation timed out");return;}
      if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native editable action physical capacity exceeded");return;}
    }
    if(config->HasKey("diagnosticExpressionJSON")){
      if(operations_.IsExpired(id,generation,Now())){Complete(id,false,action+": Native operation timed out");return;}
      if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native action diagnostic physical capacity exceeded");return;}
    }
    if (action_pauses_.Contains(id)) return;
    if (action == "sendKeys") {
      if (!input_work_.contains(id)) { BeginSendKeys(id, generation, config); return; }
      const auto work = input_work_.at(id);
      if (work->generation != generation) { Complete(id, false, "Native send-keys owner ID is physically retained"); return; }
      if (keyboard_input_ != id) return;
    }
    auto path = config->GetList("framePath");
    if (path && path->GetSize()) {
      ResolveFramePath(id,generation,config,0,FrameId(config),[this,id,generation,selector,action,text,pointer_work](Dict resolved) { LocatorAction(id,generation,selector,action,text,resolved,pointer_work); }); return;
    }
    if(!pointer_work&&!locator_handlers_.empty()&&(action=="click"||action=="tap"||action=="hover"||action=="fill"||action=="clear")&&!drag_work_.contains(id)&&!config->HasKey("formNode")&&
       LocatorHandlerBefore(id,generation,config,[this,id,generation,selector,action,text,config]{LocatorAction(id,generation,selector,action,text,config);}))return;
    browser_->GetHost()->SetFocus(true);
    const bool pause_before_stable = !pointer_work && config->GetBool("pauseBeforeStable") && !config->GetBool("resumeBeforeStable");
    config->Remove("resumeBeforeStable");
    const char* alignments[] = {"'center'", "'end'", "'start'", "'nearest'"};
    const auto alignment = pointer_work ? "'nearest'" : alignments[locator_attempts_[id]++ % 4];
    // Authored DOM logic: inspect current state on each attempt. C++ owns the
    // operation, deadline, retry scheduling and input dispatch.
    const auto expression = std::string("(async()=>{const list=") + (config->HasKey("nativeHandle") || config->HasKey("formNode") ? "[this]" : (QueryNeedsMainWorld(config)||QueryHasSnapshotReferences(config))?"this":QueryExpression(config)) + ";"
      "if(list.length>1&&" + std::string(config->GetType("strict") == VTYPE_BOOL && !config->GetBool("strict") ? "false" : "true") + ")" +
      (config->HasKey("diagnosticExpressionJSON")?"return {strictError:"+QueryDiagnosticExpression(config,"list")+"};":std::string("throw new Error('Strict locator matched multiple elements');")) +
      "if(!list.length)return {ready:false,reason:'element to be attached'};const selected=list[0],textNode=selected.nodeType===3&&['click','tap','hover'].includes(" + Literal(action) + ");let e=textNode?selected.parentElement:selected;"
      "if(!selected.isConnected||!e)throw new Error('Element is not attached to the DOM');if(e.nodeType!==1)throw new Error('Node is not an Element');"
      "if(['fill','clear'].includes(" + Literal(action) + "))e=("+std::string(kSuiteCutEditableTarget)+")(e);"
      "if(['click','tap'].includes(" + Literal(action) + ")&&e instanceof HTMLInputElement&&e.labels&&e.labels.length){const r=e.getBoundingClientRect();if(!r.width||!r.height)e=e.labels[0];}"
      "if(!" + std::string(config->GetBool("formCheck") ? "true" : "false") + "&&['click','tap'].includes(" + Literal(action) + "))e=e.closest('button,a[href],[role=button],[role=link]')||e.closest('label')||e;"
      "if(" + Literal(action) + "==='sendKeys')return ("+std::string(kSuiteCutSendKeysDom)+")(e,"+Literal(text)+");"
      "if(" + Literal(action) + "==='type')return ("+std::string(kSuiteCutTypeDom)+")(e);"
      "if(['focus','press'].includes(" + Literal(action) + ")){if(typeof e.focus==='function')e.focus();return {ready:true,focused:true};}"
      "const textRange=()=>{const range=document.createRange();range.selectNode(selected);return range;};"
      "const bounds=()=>{if(!textNode)return e.getBoundingClientRect();const range=textRange(),rect=range.getBoundingClientRect();range.detach();return rect;};"
      "const rectangles=()=>{if(!textNode)return [...e.getClientRects()];const range=textRange(),rects=[...range.getClientRects()];range.detach();return rects;};"
      "const visible=()=>{const r=bounds(),s=getComputedStyle(e);return r.width>0&&r.height>0&&s.visibility!=='hidden'&&s.visibility!=='collapse'&&s.display!=='none'};"
      "if(!visible()){if(" + std::string(config->GetBool("force") ? "true" : "false") + "){const rects=rectangles(),s=getComputedStyle(e);if(rects.length&&!rects.some(r=>r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<innerWidth&&r.top<innerHeight)&&s.visibility!=='hidden'&&s.visibility!=='collapse'&&s.display!=='none')throw new Error('Element is outside of the viewport');throw new Error('Element is not visible');}return {ready:false,reason:'element to be visible'};}"
      "if(!" + std::string(config->GetBool("force") ? "true" : "false")  + "&&" + Literal(action) + "!=='hover'&&(e.matches(':disabled')||e.closest('[aria-disabled=\"true\"]')))return {ready:false,reason:'element to be enabled'};"
      "if(textNode){if(!rectangles().some(r=>r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<innerWidth&&r.top<innerHeight))return {ready:false,textScroll:true};}else e.scrollIntoView({block:" + std::string(config->HasKey("position") ? "'start'" : alignment) + ",inline:" + std::string(config->HasKey("position") ? "'start'" : "'nearest'") + ",behavior:'instant'});"
      "const position=" + (config->GetDictionary("position") ? Json(config->GetDictionary("position")) : "null") + ";"
      "if(position){const point=()=>{const r=e.getBoundingClientRect(),s=getComputedStyle(e);return {x:r.left+parseFloat(s.borderLeftWidth)+position.x,y:r.top+parseFloat(s.borderTopWidth)+position.y}};"
      "for(let p=e.parentElement;p;p=p.parentElement){if(p===document.body||p===document.documentElement)continue;const r=p.getBoundingClientRect(),s=getComputedStyle(p),q=point(),left=r.left+parseFloat(s.borderLeftWidth),top=r.top+parseFloat(s.borderTopWidth);if(p.scrollWidth>p.clientWidth)p.scrollLeft+=q.x<left?q.x-left-4:q.x>=left+p.clientWidth?q.x-left-p.clientWidth+4:0;if(p.scrollHeight>p.clientHeight)p.scrollTop+=q.y<top?q.y-top-4:q.y>=top+p.clientHeight?q.y-top-p.clientHeight+4:0;}"
      "const q=point(),width=document.documentElement.clientWidth,height=document.documentElement.clientHeight;window.scrollBy({left:q.x<0||q.x>=width?q.x-width/2:0,top:q.y<0||q.y>=height?q.y-height/2:0,behavior:'instant'});}"
      "if(" + std::string(pause_before_stable ? "true" : "false") + ")return {ready:false,beforeStable:true};"
      // Click stability belongs after focus/hover, on the node we will actually
      // press. Trial clicks still prove stability without admitting real input.
      "const a=bounds();if(" + std::string(pointer_work || action!="click" || config->GetBool("trial") ? "true" : "false") + "&&!" + std::string(config->GetBool("force") ? "true" : "false") + "&&" + Literal(action) + "!=='fill'&&" + Literal(action) + "!=='clear'){await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);}"
      "if((textNode&&!selected.isConnected)||!e.isConnected){" + std::string(config->HasKey("nativeHandle") ? "throw new Error('Element is not attached to the DOM');" : "return {ready:false};") + "}if(!visible())return {ready:false};const b=bounds();"
      "if(!" + std::string(config->GetBool("force") ? "true" : "false") + "&&" + Literal(action) + "!=='fill'&&" + Literal(action) + "!=='clear'&&(a.x!==b.x||a.y!==b.y||a.width!==b.width||a.height!==b.height))return {ready:false,reason:'element position to be stable'};"
      "const style=getComputedStyle(e);"
      "let x=position?b.left+parseFloat(style.borderLeftWidth)+position.x:(Math.max(0,b.left)+Math.min(innerWidth,b.right))/2,y=position?b.top+parseFloat(style.borderTopWidth)+position.y:(Math.max(0,b.top)+Math.min(innerHeight,b.bottom))/2;"
      "if(!position){const rects=rectangles().filter(r=>r.width>0&&r.height>0&&r.right>0&&r.bottom>0&&r.left<innerWidth&&r.top<innerHeight);if(rects.length){const r=rects[0];x=(Math.max(0,r.left)+Math.min(innerWidth,r.right))/2;y=(Math.max(0,r.top)+Math.min(innerHeight,r.bottom))/2;}}"
      "if(x<0||y<0||x>=innerWidth||y>=innerHeight){if(" + std::string(config->GetBool("force") ? "true" : "false") + ")throw new Error('Element is outside of the viewport');return {ready:false};}"
      "if(!" + std::string(config->GetBool("force") ? "true" : "false") + "&&" + Literal(action) + "!=='fill'&&" + Literal(action) + "!=='clear'){let target=document.elementFromPoint(x,y);while(target&&target.shadowRoot){const child=target.shadowRoot.elementsFromPoint(x,y)[0];if(!child||child===target)break;target=child;}let receives=false;for(let n=target;n;n=n.assignedSlot||n.parentElement||n.getRootNode().host){if(n===e){receives=true;break;}}if(!receives)return {ready:false};}"
      "if(" + Literal(action) + "==='clear'){const result=("+std::string(kSuiteCutClearDom)+")(e);if(!result.ready||result.done)return result;}"
      "if(" + Literal(action) + "==='fill'){"
      "if(e instanceof HTMLInputElement||e instanceof HTMLTextAreaElement){"
      "if(e.readOnly)return {ready:false};const text=" + Literal(text) + ";"
      "if(e instanceof HTMLInputElement){const type=e.type;"
      "if(['color','date','time','datetime-local','month','range','week'].includes(type)){"
      "const probe=e.cloneNode();probe.value=text;if(type==='color'?probe.value.toLowerCase()!==text.toLowerCase():type==='range'?(!Number.isFinite(Number(text))||text.trim()===''||Number(probe.value)!==Number(text)):(text!==''&&probe.value===''))throw new Error('Malformed value');"
      "e.focus();e.value=probe.value;e.dispatchEvent(new Event('input',{bubbles:true,composed:true}));e.dispatchEvent(new Event('change',{bubbles:true}));return {ready:true,done:true};}"
      "if(type==='number'&&text!==''&&!Number.isFinite(Number(text)))throw new Error('Cannot type text into input[type=number]');"
      "if(!['text','search','url','tel','email','password','number'].includes(type))throw new Error('Input of type '+JSON.stringify(type)+' cannot be filled');}e.focus();e.select();"
      "}else if(e instanceof HTMLElement&&e.isContentEditable){e.focus();const r=document.createRange();r.selectNodeContents(e);const selection=getSelection();selection.removeAllRanges();selection.addRange(r);"
      "}else throw new Error('Element is not an <input>, <textarea> or [contenteditable] element');}"
      "if(['fill','clear'].includes("+Literal(action)+")&&!("+std::string(kSuiteCutEditableFocus)+")(e)){if("+std::string(config->GetBool("force")?"true":"false")+")throw new Error('Element cannot receive native text editing focus');return {ready:false,reason:'element to receive native text editing focus'};}return {ready:true,x,y,viewportWidth:innerWidth,viewportHeight:innerHeight};})()";
    DevToolsCallback callback = [this, id, generation, selector, action, text, config, pointer_work](bool ok, const std::string& json) {
      if(!operations_.IsCurrent(id,generation))return;
      if((action=="fill"||action=="clear")&&operations_.IsExpired(id,generation,Now())){Complete(id,false,action+": Native operation timed out");return;}
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC);
      auto result = parsed ? parsed->GetDictionary() : nullptr;
      if (!ok || !result || result->HasKey("exceptionDetails")) {
        auto details = result ? result->GetDictionary("exceptionDetails") : nullptr;
        auto exception = details ? details->GetDictionary("exception") : nullptr;
        std::string error = exception ? exception->GetString("description").ToString() : "Native locator evaluation failed";
        error = error.substr(0, error.find('\n'));
        Complete(id, false, action + ": " + error + "\nCall log:\n - resolving " + selector); return;
      }
      auto remote = result->GetDictionary("result");
      auto state = remote ? remote->GetDictionary("value") : nullptr;
      if (!state) { Complete(id, false, "Invalid locator evaluation result"); return; }
      if(state->GetType("strictError")==VTYPE_STRING){
        if(operations_.IsExpired(id,generation,Now()))Complete(id,false,action+": Native operation timed out");
        else Complete(id,false,action+": "+state->GetString("strictError").ToString());
        return;
      }
      if(state->GetBool("textScroll")){
        const auto handle=config->HasKey("nativeHandle")?handles_.find(config->GetInt("nativeHandle")):handles_.end();
        if(handle==handles_.end()){Complete(id,false,"Native text-node pointer owner is unavailable");return;}
        auto params=CefDictionaryValue::Create();params->SetString("objectId",handle->second.node_id);
        DevTools(id,"DOM.scrollIntoViewIfNeeded",params,[this,id,generation,selector,action,text,config,pointer_work](bool success,const std::string& json){
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);const auto result=parsed?parsed->GetDictionary():nullptr;
          if(!success||!result||result->HasKey("error")){Complete(id,false,"Native text-node pointer scrolling failed: "+json.substr(0,1024));return;}
          auto release=CefDictionaryValue::Create();release->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
          DevTools(id,"Runtime.releaseObjectGroup",release,[this,id,generation,selector,action,text,config,pointer_work](bool released,const std::string&){
            if(!released){Complete(id,false,"Native text-node pointer temporary group release failed");return;}
            LocatorAction(id,generation,selector,action,text,config,pointer_work);
          });
        });return;
      }
      if (state->GetBool("beforeStable")) { PauseActionBeforeStable(id,generation,selector,action,text,config); return; }
      if (state->GetBool("done")) { Complete(id, true); return; }
      if (!state->GetBool("ready")) {
        locator_waits_[id] = state->GetType("reason") == VTYPE_STRING ? state->GetString("reason").ToString().substr(0,1024) : "element actionability";
        if (pointer_work) {
          // No button has been pressed. Yield the mouse lane so an overlay's
          // locator handler (or another queued gesture) can run before retrying
          // this same operation with its original deadline and generation.
          action_navigations_.erase(id);
          CleanupInput(id);
          context_retries_[id] = [this,id,generation,selector,action,text,config] {
            LocatorAction(id,generation,selector,action,text,config);
          };
          CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation,selector,action,text,config] {
            self->LocatorAction(id,generation,selector,action,text,config);
          }),20);
          return;
        }
        if(!pointer_work&&selector_waits_&&selector_waits_->Contains(id)){RetryContext(id);return;}
        CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation, selector, action, text, config, pointer_work] {
          self->LocatorAction(id, generation, selector, action, text, config, pointer_work);
        }), 20);
        return;
      }
      if (action == "focus") { Complete(id, true); return; }
      if (action == "sendKeys") { SendKeysPrepared(id, state); return; }
      if (action == "press" || action == "type") {
        TargetedKeyboardPrepared(id,generation); return;
      }
      if (action == "fill" || action == "clear") {
        if(!operations_.IsCurrent(id,generation))return;
        if(operations_.IsExpired(id,generation,Now())){Complete(id,false,action+": Native operation timed out");return;}
        CefKeyEvent key;
        key.type = KEYEVENT_RAWKEYDOWN;
        key.windows_key_code = 8;
        key.native_key_code = 0xff08;
        if (text.empty()) browser_->GetHost()->SendKeyEvent(key);
        key.type = KEYEVENT_KEYUP;
        if (text.empty()) browser_->GetHost()->SendKeyEvent(key);
        operation_sessions_[id] = "";
        auto params = CefDictionaryValue::Create();
        params->SetString("text", text);
        DevTools(id, "Input.insertText", params, [this, id,generation](bool success, const std::string&) {
          if(!operations_.IsCurrent(id,generation))return;
          if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native text input operation timed out");return;}
          Complete(id, success, success ? "" : "Native text input failed");
        });
      } else {
        if (config->GetBool("trial")) { Complete(id, true); return; }
        auto input = CefDictionaryValue::Create(); input->SetString("kind",action=="tap"?"touch":"mouse"); input->SetString("action",action=="tap"?"tap":action == "click" ? "click" : "move");
        const auto target_frame = FrameId(config);
        const double local_x = Number(state,"x"), local_y = Number(state,"y");
        input->SetString("button", config->GetType("button") == VTYPE_STRING ? config->GetString("button") : CefString("left"));
        input->SetInt("clickCount", config->GetType("clickCount") == VTYPE_INT ? config->GetInt("clickCount") : 1);
        input->SetInt("delay", config->GetInt("delay")); input->SetInt("steps", config->GetType("steps") == VTYPE_INT ? config->GetInt("steps") : 1);
        input->SetBool("waitForNavigation", (action == "click"||action=="tap") && !config->GetBool("noWaitAfter"));
        MapFramePoint(id,target_frame,local_x,local_y,Number(state,"viewportWidth"),Number(state,"viewportHeight"),[this,id,generation,input,target_frame,config,action,pointer_work](double x,double y) {
          if (pointer_work) {
            const auto current=input_work_.find(id);
            if (!operations_.IsCurrent(id,generation) || mouse_input_!=id ||
                current==input_work_.end() || current->second!=pointer_work) return;
            if (operations_.IsExpired(id,generation,Now())) { Complete(id,false,"Native pointer original deadline expired before press"); return; }
            const bool moved = std::abs(x-mouse_x_)>.01 || std::abs(y-mouse_y_)>.01;
            pointer_work->message->SetDouble("x",x);pointer_work->message->SetDouble("y",y);
            pointer_work->pointer_verified=!moved;
            if(moved){pointer_work->phase=0;pointer_work->step=1;pointer_work->start_x=mouse_x_;pointer_work->start_y=mouse_y_;}
            pointer_work->busy=false;ContinueInput(pointer_work);return;
          }
          // Two renderer/context retries can have evaluated concurrently before
          // either admitted input. Only the first may acquire this gesture.
          if (input_work_.contains(id)) return;
          input->SetDouble("x",x);input->SetDouble("y",y);operation_frames_[id]=target_frame;
          context_retries_.erase(id);BeginInput(id,generation,input,nullptr,
            action=="click"&&!config->GetBool("force")?config->Copy(false):nullptr);
        });
      }
    };
    if (config->HasKey("formNode")) FormNodeEvaluate(id,config,expression,std::move(callback));
    else if (config->HasKey("nativeHandle")) EvaluateFixedNode(id,config,expression,std::move(callback));
    else if(QueryNeedsMainWorld(config)||QueryHasSnapshotReferences(config))QueryInspect(id,config,"return await("+expression+");",true,true,std::move(callback));
    else Evaluate(id,expression,std::move(callback),FrameId(config),!QueryNeedsMainWorld(config));
  }
  #include "binding_argument_handle.inc"
#include "ax_controller.inc"
#include "dom_semantic_controller.inc"
#include "snapshot_reference_dispatch.inc"
#include "snapshot_reference_controller.inc"
#include "locator_normalization_controller.inc"
#include "locator_suggestion_controller.inc"
#include "locator_pick_controller.inc"
#include "native_highlight_source.inc"
#include "trace_source.inc"
#include "native_debugger_controller.inc"
#include "coverage_source.inc"
#include "developer_debugger_source.inc"
#include "developer_debugger_documents_source.inc"
#include "developer_debugger_frames_source.inc"
#include "worker_controller.inc"
#include "developer_debugger_worker_documents_source.inc"
#include "developer_debugger_workers_source.inc"
#include "worker_session_retirement.inc"
#include "worker_console.inc"
#include "worker_target_lifetime.inc"
#include "media_emulation.inc"
#include "touch_input.inc"
  #include "form_controller.inc"
  #include "drag_controller.inc"
  #include "file_chooser_controller.inc"
#include "download_controller.inc"
#include "recording_controller.inc"
#include "streaming_controller.inc"
#include "element_geometry.inc"
#include "locator_predicate.inc"
#include "aria_json.inc"
  #include "screenshot_assertion_scope.inc"
  #include "screenshot_assertion_capture.inc"
  #include "screenshot_assertion_sender.inc"
  #include "screenshot_assertion_view.inc"
  #include "artifact_view_lease.inc"
  #include "screenshot_comparison.inc"
  #include "artifact_config_inputs.inc"
  #include "artifact_navigation.inc"
  #include "artifact_documents.inc"
  #include "artifact_before_screenshot.inc"
  #include "artifact_geometry.inc"
  #include "artifact_masks.inc"
  #include "screenshot_assertion_cleanup.inc"
  #include "screenshot_assertion_finalization.inc"
  #include "action_hooks.inc"
  #include "locator_handler_references.inc"
  #include "locator_handler_source.inc"
  #include "websocket_predicates.inc"
  #include "websocket_routing.inc"
  #include "websocket_policy_source.inc"
  #include "event_predicates.inc"
  #include "virtual_time_source.inc"
  #include "crash_controller.inc"
  #include "recovery_controller.inc"
  #include "accepted_inspector_owner.inc"
  #include "devtools_dispatch.inc"
  #include "url_predicates.inc"
  #include "action_navigation.inc"
  bool Descendant(const std::string& child, const std::string& root) {
    auto id = child;
    for (size_t step = 0; step <= frame_states_.size(); ++step) {
      if (id == root) return true;
      const auto frame = frame_states_.find(id);
      if (frame == frame_states_.end() || frame->second.parent.empty()) return false;
      id = frame->second.parent;
    }
    return false;
  }
  void NetworkActivity(const std::string& id) {
    auto current = id;
    for (size_t step = 0; step <= frame_states_.size(); ++step) {
      const auto frame = frame_states_.find(current); if (frame == frame_states_.end()) break;
      frame->second.network_activity = Now(); current = frame->second.parent;
      if (current.empty()) break;
    }
  }
  #include "har_byte_source.inc"
  void NativeNetworkSend(const std::string&session,const std::string&method,Dict params,suitecut::NetworkControls::Result callback,double deadline){
    const int id=AllocateDevToolsId();if(!id||network_calls_.size()>=1024){callback(false,"Native network command capacity exceeded");return;}
    if(deadline&&Now()>=deadline){callback(false,"Native network original command deadline exceeded");return;}
    network_calls_.emplace(id,NetworkCall{session,suitecut::BindNativeLocatorHandlerAsync(std::move(callback)),deadline?deadline:Now()+30000});WakeDeadlines();
    if(!SubmitDevTools(id,method,params,session)){const auto found=network_calls_.find(id);if(found!=network_calls_.end()){auto done=std::move(found->second.callback);network_calls_.erase(found);done(false,"Native network command dispatch failed");}}
  }
  void NativeFrameRequestSession(const std::string& request,const std::string& previous,const std::string& session) {
    if(image_streams_&&!image_streams_->Move(previous,session,request)){image_streams_->Discard(previous,request);image_streams_->Discard(session,request);}
    const auto old_key=previous+"\n"+request,new_key=session+"\n"+request;
    const auto original=documents_.find(old_key);
    if(documents_.contains(new_key)){NetworkFatal("Conflicting native frame response session handoff");return;}
    if(original!=documents_.end()){auto document=documents_.extract(original);document.key()=new_key;document.mapped().session=session;documents_.insert(std::move(document));}
    for(auto&key:document_order_)if(key==old_key)key=new_key;
    auto active=active_requests_.extract(old_key);if(!active.empty()){
      if(active_requests_.contains(new_key)){NetworkFatal("Conflicting native frame active request handoff");return;}
      active.key()=new_key;active_requests_.insert(std::move(active));
    }
    for(auto&[owner,wait]:body_waiters_)if(wait.key==old_key)wait.key=new_key;
  }
  void NativeWorkerInitialRequestSession(const std::string& target,const std::string& parent,const std::string& child,const std::string& url) {
    if(target.empty()||child.empty())return;
    const auto old_key=parent+"\n"+target,new_key=child+"\n"+target;
    const auto original=documents_.find(old_key);
    if(original==documents_.end()||original->second.finished||original->second.request!=target||
       original->second.session!=parent||original->second.url!=url||documents_.contains(new_key))return;
    auto document=documents_.extract(original);document.key()=new_key;document.mapped().session=child;
    documents_.insert(std::move(document));
    for(auto& key:document_order_)if(key==old_key)key=new_key;
    auto active=active_requests_.extract(old_key);
    if(!active.empty()){
      if(active_requests_.contains(new_key)){Error("Conflicting native worker script request ownership");return;}
      active.key()=new_key;active_requests_.insert(std::move(active));
    }
  }
  void AttributeInitialPopupRequest(Dict data) {
    if(ready_ || popup_initial_navigation_observed_ || !engine_.contexts()->PopupOpener(page_id_) || data->GetString("type")!="Document")return;
    popup_initial_navigation_observed_=true;data->SetBool("suitecutNavigationFrameUnavailable",true);
  }
  void NetworkEvent(const std::string& method, Dict data, const std::string& session) {
    const auto request = data->GetString("requestId").ToString();
    if (request.empty() || request.size() > 256) return;
    const auto key = session + "\n" + request;
    if (method == "Network.requestWillBeSent") {
      const auto frame = data->GetString("frameId").ToString();
      if (active_requests_.size() >= 8192 && !active_requests_.contains(key)) { Error("Native active network request limit exceeded"); return; }
      active_requests_[key] = {frame, Now()}; NetworkActivity(frame);
      {
        auto info = data->GetDictionary("request"); if (!info) return;
        if (!documents_.contains(key) && documents_.size() >= 1024) {
          auto oldest = std::find_if(document_order_.begin(), document_order_.end(), [this](const std::string& candidate) {
            const auto found = documents_.find(candidate); return found != documents_.end() && found->second.finished;
          });
          if (oldest == document_order_.end()) { Error("Native document response limit exceeded"); return; }
          documents_.erase(*oldest); document_order_.erase(oldest);
        }
        if (!documents_.contains(key)) document_order_.push_back(key);
        documents_[key] = {frame, request, session, data->GetString("loaderId").ToString(),
          info->GetString("method").ToString(), info->GetString("url").ToString(), nullptr, false, "", data->GetString("type") == "Document"};
      }
    } else if (method == "Network.responseReceived") {
      const auto document = documents_.find(key);
      if (document != documents_.end()) {
        auto response = data->GetDictionary("response");
        // This nested dictionary is owned by the event payload. Retain an
        // independent copy after the callback's parent value is destroyed.
        document->second.response = response ? response->Copy(false) : nullptr;
      }
    } else {
      const auto active = active_requests_.find(key);
      if (active != active_requests_.end()) { NetworkActivity(active->second.frame); active_requests_.erase(active); }
      const auto document = documents_.find(key);
      if (document != documents_.end()) {
        document->second.finished = true;
        if (method == "Network.loadingFailed") {
          document->second.error = data->GetString("errorText").ToString();
          std::vector<int> failed;
          for (const auto& [id, nav] : navigations_) if (document->second.document && nav.frame == document->second.frame &&
              (!nav.dispatched || nav.loader == document->second.loader)) failed.push_back(id);
          for (const int id : failed) DownloadNavigationAbort(id,document->second.error,document->second.url);
        }
      }
    }
    CheckNavigations(); CheckBodies();
  }
  void CheckBodies() {
    std::vector<int> ready;
    for (const auto& [id, wait] : body_waiters_) {
      const auto document = documents_.find(wait.key);
      if (!wait.dispatched && (document == documents_.end() || document->second.finished)) ready.push_back(id);
    }
    for (const int id : ready) {
      const auto wait = body_waiters_.find(id); if (wait == body_waiters_.end()) continue;
      const auto document = documents_.find(wait->second.key);
      if (document == documents_.end()) { Complete(id, false, "Native response is no longer retained"); continue; }
      if (!document->second.error.empty()) { Complete(id, false, document->second.error); continue; }
      wait->second.dispatched = true;
      wait->second.frame = document->second.frame; wait->second.loader = document->second.loader;
      operation_sessions_[id] = document->second.session;
      auto params = CefDictionaryValue::Create(); params->SetString("requestId", document->second.request);
      DevTools(id, "Network.getResponseBody", params, [this, id](bool success, const std::string& json) {
        Complete(id, success, success ? "" : "Native response body failed", json);
      });
    }
  }
  void BeginNavigation(int id, Dict message, const std::string& command, int timeout) {
    const auto frame_id = FrameId(message); const auto frame = frame_states_.find(frame_id);
    if (frame == frame_states_.end()) { Complete(id, false, "Frame was detached"); return; }
    const auto wait = message->GetType("waitUntil") == VTYPE_STRING ? message->GetString("waitUntil").ToString() : "load";
    if (wait != "commit" && wait != "domcontentloaded" && wait != "load" && wait != "networkidle") { Complete(id, false, "Invalid native navigation load state"); return; }
    Navigation nav; nav.frame = frame_id; nav.wait = wait; nav.timeout = timeout; nav.native_download_after=engine_.downloads()->NavigationSequence();
    nav.api = message->GetString("api").ToString();
    if (nav.api != "page.goto" && nav.api != "frame.goto" && nav.api != "page.reload" && nav.api != "page.goBack" && nav.api != "page.goForward" && nav.api != "page.waitForNavigation" && nav.api != "frame.waitForNavigation" && nav.api != "page.waitForLoadState" && nav.api != "frame.waitForLoadState") { Complete(id, false, "Invalid native navigation API"); return; }
    operation_frames_[id] = frame_id; operation_sessions_[id] = frame->second.session;
    if (command == "historyBack" || command == "historyForward") {
      if (frame_id != main_frame_id_) { Complete(id, false, "Native history navigation requires the main frame"); return; }
      const bool back = command == "historyBack";
      if (back ? !browser_->CanGoBack() : !browser_->CanGoForward()) { Complete(id, true, "", "null"); return; }
      navigations_[id] = nav;
      DevTools(id,"Page.getNavigationHistory",nullptr,[this,id,back](bool ok,const std::string& json){
        auto found=navigations_.find(id);if(found==navigations_.end())return;
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
        auto entries=result?result->GetList("entries"):nullptr;
        if(!ok||!entries||result->GetType("currentIndex")!=VTYPE_INT){Complete(id,false,"Native browser history is unavailable");return;}
        const int target=result->GetInt("currentIndex")+(back?-1:1);
        if(target<0||static_cast<size_t>(target)>=entries->GetSize()){Complete(id,true,"","null");return;}
        auto entry=entries->GetDictionary(target);
        if(!entry||entry->GetType("id")!=VTYPE_INT||entry->GetType("url")!=VTYPE_STRING){Complete(id,false,"Invalid native history entry");return;}
        auto frame=frame_states_.find(found->second.frame);
        if(frame==frame_states_.end()){Complete(id,false,"Frame was detached");return;}
        auto& nav=found->second;nav.next=true;nav.after=frame->second.navigation;
        nav.url=frame->second.url;nav.previous_loader=frame->second.loader;
        nav.history_entry=entry->GetInt("id");nav.history_url=entry->GetString("url").ToString();
        auto params=CefDictionaryValue::Create();params->SetInt("entryId",nav.history_entry);
        DevTools(id,"Page.navigateToHistoryEntry",params,[this,id](bool ok,const std::string&){
          auto found=navigations_.find(id);if(found==navigations_.end())return;
          if(!ok){Complete(id,false,"Native history navigation failed");return;}
          found->second.dispatched=true;CheckNavigations();
        });
      });
      return;
    }
    if (command == "reload") {
      nav.next = true; nav.after = frame->second.navigation;
      nav.url = frame->second.url; nav.previous_loader = frame->second.loader;
      navigations_[id] = nav;
      DevTools(id,"Page.reload",nullptr,[this,id](bool ok,const std::string&) {
        const auto found = navigations_.find(id); if(found == navigations_.end()) return;
        if(!ok) { Complete(id,false,"Native reload failed"); return; }
        found->second.dispatched = true; CheckNavigations();
      });
      return;
    }
    if (command != "navigateFrame") {
      nav.wait_only = command == "waitLoadState"; nav.next = !nav.wait_only;
      nav.dispatched = true;
      if (message->GetType("afterNavigation") == VTYPE_INT) nav.after = message->GetInt("afterNavigation");
      else nav.after = frame->second.navigation;
      nav.url = frame->second.url; nav.previous_loader = frame->second.loader; navigations_[id] = nav; CheckNavigations(); return;
    }
    nav.url = message->GetString("url").ToString();
    if (nav.url.empty() || nav.url.size() > 8192 || nav.url.find('\0') != std::string::npos) { Complete(id, false, "Invalid native navigation URL"); return; }
    CefURLParts parts;
    if (!CefParseURL(nav.url, parts)) { Complete(id, false, "Invalid native navigation URL"); return; }
    const auto scheme = CefString(&parts.scheme).ToString();
    if (scheme != "http" && scheme != "https" && scheme != "about" && scheme != "data" && scheme != "file") { Complete(id, false, "Unsupported native navigation URL scheme"); return; }
    std::vector<int> superseded;
    for (const auto& [owner, old] : navigations_) if (old.frame == frame_id && !old.wait_only && (!old.next||old.replace_initial)) superseded.push_back(owner);
    for (const int owner : superseded) Complete(owner, false, "Native navigation was interrupted by another navigation");
    nav.after = frame->second.navigation; navigations_[id] = nav;
    auto params = CefDictionaryValue::Create(); params->SetString("url", nav.url); params->SetString("frameId", frame_id);
    if (message->GetType("referer") == VTYPE_STRING) params->SetString("referrer", message->GetString("referer"));
    if(!ReplaceInitialNavigation(id,params))DispatchFrameNavigation(id,params);
  }
  void CheckNavigations() {
    CheckRendererRecovery();
    std::vector<int> history_checks;
    std::vector<std::pair<int, std::string>> complete;
    for (auto& [id, nav] : navigations_) {
      if (!nav.dispatched||!nav.download_abort.empty()||(nav.recovery && renderer_failed_)) continue;
      const auto frame = frame_states_.find(nav.frame); if (frame == frame_states_.end()) continue;
      if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && diagnostic_messages_++ < 2000)
        std::fprintf(stderr, "native navigation id=%d loader=%s frameLoader=%s sameDocument=%d after=%llu frameNavigation=%llu wait=%s\n", id, nav.loader.c_str(), frame->second.loader.c_str(), nav.same_document, static_cast<unsigned long long>(nav.after), static_cast<unsigned long long>(frame->second.navigation), nav.wait.c_str());
      if(nav.history_entry>=0&&!nav.history_verified){
        // A newly committed document can still be swapping renderer endpoints.
        // Its real Runtime context marks when the page command channel is usable.
        if(!frame->second.context||frame->second.navigation<=nav.after||frame->second.url!=nav.history_url||nav.history_pending||nav.history_candidate==frame->second.navigation)continue;
        nav.history_pending=true;nav.history_candidate=frame->second.navigation;
        history_checks.push_back(id);
        continue;
      }
      if (nav.next) {
        if (frame->second.navigation <= nav.after) continue;
        if (nav.loader.empty()) {
          if (frame->second.loader == nav.previous_loader) { complete.emplace_back(id, "null"); continue; }
          nav.loader = frame->second.loader;
        }
      }
      if (nav.same_document) {
        if (frame->second.navigation > nav.after) complete.emplace_back(id, "null");
        continue;
      }
      if (!nav.wait_only && frame->second.loader != nav.loader) {
        if (!nav.next && frame->second.navigation > nav.after && frame->second.milestones.contains("commit")) nav.loader = frame->second.loader;
        else continue;
      }
      const auto milestone = nav.wait == "domcontentloaded" ? "DOMContentLoaded" : nav.wait;
      bool done = nav.wait == "commit" ? frame->second.milestones.contains("commit") : frame->second.milestones.contains(milestone);
      if (nav.wait == "networkidle") {
        double last = frame->second.network_activity;
        for (const auto& [child, state] : frame_states_) if (Descendant(child, nav.frame)) last = std::max(last, state.network_activity);
        done = Now() - last >= 500;
        for (const auto& [key, request] : active_requests_) if (Descendant(request.frame, nav.frame)) { done = false; break; }
      }
      if (!done) continue;
      if (nav.wait_only) { complete.emplace_back(id, "null"); continue; }
      const Document* document = nullptr;
      for (const auto& [key, candidate] : documents_) if (candidate.document && candidate.loader == frame->second.loader && candidate.frame == nav.frame && candidate.response) { document = &candidate; break; }
      if (!document) { complete.emplace_back(id, "null"); continue; }
      auto response = suitecut::NativeResponseWireInfo(document->response, document->request,
                                                       document->session, document->frame, document->method);
      complete.emplace_back(id, Json(response));
    }
    for(const int id:history_checks){
      if(!navigations_.contains(id))continue;
        DevTools(id,"Page.getNavigationHistory",nullptr,[this,id](bool ok,const std::string& json){
          auto found=navigations_.find(id);if(found==navigations_.end())return;
          found->second.history_pending=false;
          auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;
          auto entries=result?result->GetList("entries"):nullptr;
          if(!ok||!entries||result->GetType("currentIndex")!=VTYPE_INT){Complete(id,false,"Native history commit identity is unavailable");return;}
          const int current=result->GetInt("currentIndex");
          if(current<0||static_cast<size_t>(current)>=entries->GetSize()){Complete(id,false,"Invalid committed native history entry");return;}
          auto entry=entries->GetDictionary(current);auto frame=frame_states_.find(found->second.frame);
          if(entry&&entry->GetInt("id")==found->second.history_entry&&frame!=frame_states_.end()&&frame->second.url==found->second.history_url)found->second.history_verified=true;
          CheckNavigations();
        });
    }
    for (const auto& [id, json] : complete) {
      const auto nav=navigations_.find(id);const auto operation=operations_.Get(id);
      if(nav!=navigations_.end()&&nav->second.recovery&&operation&&operations_.IsExpired(id,operation->generation,Now()))Complete(id,false,"Native renderer recovery original action deadline exceeded");
      else Complete(id,true,"",json);
    }
  }
  void EmitFrame(double timestamp = Now()) {
    if (!ready_ || view_.empty()) return;
    const bool recording_viewport = engine_.recordings()->NeedsFrame(page_id_, true);
    const bool recording_canvas = engine_.recordings()->NeedsFrame(page_id_);
#if defined(OS_LINUX)
    const bool publishing = engine_.streaming()->NeedsFrame(page_id_);
#else
    const bool publishing = false;
#endif
    const bool needs_canvas = frame_transport_enabled_ || recording_canvas || publishing;
    // OnPaint still retains the genuine view and feeds traces/screenshots.
    // Compose, fit and convert pixels only for an actual downstream consumer.
    if (!needs_canvas && !recording_viewport) return;
    std::vector<uint8_t> output;
    const std::vector<uint8_t>* composed = &view_;
    if (show_popup_ && !popup_.empty()) {
      output = view_;
      composed = &output;
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
    }
    // Automatic video consumes the genuine composed paint before the legacy
    // stream/manual-recording fit. Its worker owns scaling and current CSS fit.
    if (recording_viewport && frame_css_width_ && frame_css_height_)
      engine_.recordings()->ViewportFrame(page_id_, *composed, frame_width_, frame_height_,
          frame_css_width_, frame_css_height_, timestamp);
    if (!needs_canvas) return;
    // Recording dimensions are fixed at launch. Resizing the browser viewport
    // fits its current capture into that canvas, preserving aspect ratio. In
    // particular, odd CSS viewport sizes never reach an even-size I420 writer.
    SuiteCutCaptureRect content_rect;
    if (!SuiteCutCaptureContentRect(frame_width_, frame_height_, capture_width_, capture_height_, content_rect) ||
        composed->size() != static_cast<size_t>(frame_width_) * frame_height_ * 4) {
      Error("Native capture resize failed"); return;
    }
    if (frame_width_ != capture_width_ || frame_height_ != capture_height_) {
      std::vector<uint8_t> fitted;
      if (!SuiteCutFitCapture(*composed, frame_width_, frame_height_, capture_width_, capture_height_, fitted)) {
        Error("Native capture resize failed"); return;
      }
      output = std::move(fitted);
      composed = &output;
    }
    if (i420_) {
      const size_t y_size = static_cast<size_t>(capture_width_) * capture_height_;
      std::vector<uint8_t> planes(y_size * 3 / 2);
      // libyuv's ARGB name describes registers; in little-endian memory its
      // input is BGRA, matching CEF. Use explicit limited-range BT.709.
      if (libyuv::ARGBToI420Matrix(composed->data(), capture_width_ * 4,
          planes.data(), capture_width_, planes.data() + y_size, capture_width_ / 2,
          planes.data() + y_size * 5 / 4, capture_width_ / 2,
          &libyuv::kArgbH709Constants, capture_width_, capture_height_) != 0) {
        Error("Native pixel conversion failed");
        return;
      }
      output = std::move(planes);
    } else if (composed != &output) output = *composed;
    if (recording_canvas) engine_.recordings()->Frame(page_id_, output, timestamp);
#if defined(OS_LINUX)
    if (publishing) engine_.streaming()->Frame(page_id_, output, timestamp);
#endif
    if (!frame_transport_enabled_) return;
    auto header = Header("frame");
    header->SetString("pixelFormat", i420_ ? "i420" : "bgra");
    // CSS dimensions are proven only by a genuine current-viewport OnPaint.
    // A pending resize can still publish the old canvas; advertise it as unknown.
    if (frame_css_width_ && frame_css_height_) {
      auto viewport = CefDictionaryValue::Create();
      viewport->SetInt("width", frame_css_width_);
      viewport->SetInt("height", frame_css_height_);
      header->SetDictionary("viewport", viewport);
    } else header->SetNull("viewport");
    auto rect = CefDictionaryValue::Create();
    rect->SetInt("x", content_rect.x); rect->SetInt("y", content_rect.y);
    rect->SetInt("width", content_rect.width); rect->SetInt("height", content_rect.height);
    header->SetDictionary("contentRect", rect);
    header->SetDouble("timestampMs", timestamp);
    header->SetInt("width", capture_width_);
    header->SetInt("height", capture_height_);
    SendPacket(header, std::move(output));
  }
  void ValidateVisibilitySelectors(int id,uint64_t generation,Dict message){
    // Compile the complete grammar in a real pristine utility context before
    // observing an absent frame. Registered engine code is never invoked here.
    auto queries=CefListValue::Create();auto query=message->GetList("query");
    if(!query){query=CefListValue::Create();if(!message->HasKey("nativeHandle")){auto step=CefDictionaryValue::Create();step->SetString("engine","css");suitecut::InboundCopyString(message,step,"selector");query->SetDictionary(0,step);}}
    queries->SetList(0,query->Copy());
    if(const auto path=message->GetList("framePath")){
      if(path->GetSize()>32){Complete(id,false,"Invalid native frame locator path");return;}
      for(size_t index=0;index<path->GetSize();++index){if(path->GetType(index)!=VTYPE_LIST){Complete(id,false,"Invalid native frame locator path");return;}queries->SetList(index+1,path->GetList(index)->Copy());}
    }
    auto list=CefValue::Create();list->SetList(queries);
    WithFrame(id,main_frame_id_,[this,id,generation,message,list](int context){
      if(!operations_.IsCurrent(id,generation))return;
      const auto frame=frame_states_.find(main_frame_id_);if(frame==frame_states_.end()||frame->second.utility_context!=context){Complete(id,false,"Native visibility selector validation context unavailable");return;}
      const auto frame_id=frame->first,session=frame->second.session,loader=frame->second.loader,identity=frame->second.utility_identity;
      auto params=CefDictionaryValue::Create();params->SetInt("contextId",context);params->SetBool("returnByValue",true);params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
      params->SetString("expression","(()=>{const validate=("+std::string(kSuiteCutDomQuery)+"),options="+QueryOptionsExpression()+";options.validateOnly=true;for(const query of "+CefWriteJSON(list,JSON_WRITER_DEFAULT).ToString()+")validate(query,document,options);return true;})()");
      DevTools(id,"Runtime.evaluate",params,[this,id,generation,message,frame_id,session,loader,identity,context](bool ok,const std::string& json){
        if(!operations_.IsCurrent(id,generation))return;
        if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
        auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto result=parsed?parsed->GetDictionary():nullptr;auto remote=result?result->GetDictionary("result"):nullptr;
        if(!ok||!result||result->HasKey("exceptionDetails")||!remote||remote->GetType("value")!=VTYPE_BOOL||!remote->GetBool("value")){
          auto details=result?result->GetDictionary("exceptionDetails"):nullptr;auto exception=details?details->GetDictionary("exception"):nullptr;const auto error=exception?suitecut::DevToolsString(exception->GetValue("description")):result?result->GetString("message").ToString():"";
          Complete(id,false,error.empty()?"Native visibility selector validation failed":error);return;
        }
        const auto frame=frame_states_.find(frame_id);
        if(frame==frame_states_.end()||frame->second.session!=session||frame->second.loader!=loader||frame->second.utility_context!=context||frame->second.utility_identity!=identity){Complete(id,false,"Native visibility selector validation document changed");return;}
        if(!visibility_snapshots_.MarkValidated(id,generation)){Complete(id,false,"Native visibility snapshot generation unavailable");return;}
        QueryEvaluate(id,generation,message);
      },session,false,kMaxResponseBytes,true);
    },true);
  }
  #include "expectation_deadline.inc"
  #include "assertion_deadline_source.inc"
  void QueryEvaluate(int id, uint64_t generation, Dict message) {
    if (!operations_.IsCurrent(id, generation)) return;
    if (AssertionDeadlineExpire(id,generation)) return;
    if (CompleteExpiredExpectation(id, generation)) return;
    if(message->GetBool("domGetter")||message->GetBool("strictDiagnostics")){
      if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
      if(!SelectorWaits().Admit(id,generation)){Complete(id,false,"Native DOM observation physical capacity exceeded");return;}
    }
    if(visibility_snapshots_.Contains(id)&&!visibility_snapshots_.Validated(id,generation)){ValidateVisibilitySelectors(id,generation,message);return;}
    auto path = message->GetList("framePath");
    if (path && path->GetSize()) {
      ResolveFramePath(id, generation, message, 0, FrameId(message), [this,id,generation](Dict resolved) { QueryEvaluate(id,generation,resolved); }); return;
    }
    if(message->GetBool("stableState")&&!PrepareStableElementState(id,generation,message))return;
    if(message->GetBool("accessibilitySnapshot")&&!message->HasKey("nativeAccessibilitySnapshot")&&!message->HasKey("nativeSemanticSnapshotJson")){
      if(!lazy_queries_.contains(id))lazy_queries_[id]=message;
      if(message->GetBool("accessibilitySemantics")){
        CaptureDOMSemantic(id,message,[this,id,generation,message](const std::string& array,const std::string& json){
          auto config=message->Copy(false);config->SetString("nativeQueryNodes",array);config->SetString("nativeSemanticSnapshotJson",json);QueryEvaluate(id,generation,config);
        });return;
      }
      CaptureAXForest(id,message,[this,id,generation,message](const std::string& array,CefRefPtr<CefListValue> forest){
        auto config=message->Copy(false);config->SetString("nativeQueryNodes",array);config->SetList("nativeAccessibilitySnapshot",forest->Copy());QueryEvaluate(id,generation,config);
      });return;
    }
    // Custom engines retain their declared author realm for selection. Native
    // DOM metadata observes those actual backend nodes in the isolated realm,
    // so page replacements of browser intrinsics cannot forge native states.
    const bool native_metadata=message->GetBool("visibility")||message->GetBool("elementState")||message->GetBool("accessibility");
    if(((!message->GetBool("dom")&&!message->HasKey("nativeHandle"))||QueryUsesMultipleWorlds(message)||
        ((native_metadata||message->GetBool("domGetter"))&&message->GetBool("dom")&&QueryNeedsMainWorld(message))||message->HasKey("nativeRootHandle"))&&!message->HasKey("nativeQueryNodes")) {
      if(!lazy_queries_.contains(id))lazy_queries_[id]=message;
      QueryNodesInWorld(id,message,message->GetBool("dom"),[this,id,generation,message](const std::string& array){auto config=message->Copy(false);config->SetString("nativeQueryNodes",array);QueryEvaluate(id,generation,config);});return;
    }
    const bool all = message->GetBool("all");
    const bool native_getter=message->GetBool("domGetter");
    const bool immediate=native_getter&&message->GetBool("immediate");
    const bool strict=!(native_getter&&message->GetType("strict")==VTYPE_BOOL&&!message->GetBool("strict"));
    const bool diagnostic_strict=message->GetBool("dom")&&(native_getter||message->GetBool("strictDiagnostics"))&&strict&&(!all||(message->GetBool("expectation")&&message->GetBool("strictDiagnostics")));
    const auto accessibility = message->GetBool("stableState")
      ? ","+ElementStates().Expression(id,"e=>("+std::string(kSuiteCutDomQuery)+")([],e,{elementStateOnly:true})")
      : message->GetBool("visibility") || message->GetBool("elementState")
      ? ",(nodes[0]?(" + std::string(kSuiteCutDomQuery) + ")([],nodes[0],{" + std::string(message->GetBool("elementState")?"elementStateOnly:true":"visibilityOnly:true") + "}):undefined)"
      : message->GetBool("expectation") && message->GetBool("accessibility")
        ? ",(nodes[0]?(" + std::string(kSuiteCutDomQuery) + ")([],nodes[0]," + QueryOptionsExpression(true) + "):undefined)"
        : std::string("");
    std::string accessibility_snapshot;
    if(message->HasKey("nativeAccessibilitySnapshot")){
      auto value=CefValue::Create();value->SetList(message->GetList("nativeAccessibilitySnapshot")->Copy());
      std::string native_json;
      if(!suitecut::DevToolsLosslessJSON(value,native_json)){Complete(id,false,"Native accessibility snapshot exceeds 64 KiB");return;}
      accessibility_snapshot=(accessibility.empty()?",undefined,":",")+native_json;
    }
    if(message->HasKey("nativeSemanticSnapshotJson"))accessibility_snapshot=std::string(accessibility.empty()?",undefined,":",")+"JSON.parse("+DOMSemanticQuote(message->GetString("nativeSemanticSnapshotJson").ToString())+")";
    const auto body = "const nodes=" + (message->HasKey("nativeHandle")?std::string("[this]"):message->HasKey("nativeQueryNodes")?std::string("this"):QueryExpression(message)) + ";" +
      (all ? std::string("") : std::string(immediate?"":"if(!nodes.length)return {waiting:true};")) +
      (diagnostic_strict?"if(nodes.length>1)return {strictError:"+QueryDiagnosticExpression(message,"nodes")+"};":(!all&&strict?std::string("if(nodes.length>1)throw new Error('strict mode violation: locator resolved to '+nodes.length+' elements');"):std::string(""))) +
      "return {waiting:false,value:await (" + message->GetString("function").ToString() + ")(" + (all ? "nodes" : "nodes[0]") + accessibility + accessibility_snapshot + ",...handles)};";
    DevToolsCallback callback = [this, id, generation, message,diagnostic_strict](bool ok, const std::string& json) {
      if(assertion_deadlines_.Get(id,generation)){
        if(!AssertionDeadlineObserve(id,generation,ok,json)||AssertionDeadlineExpire(id,generation))return;
      }
      if(message->GetBool("domGetter")||diagnostic_strict){
        if(!operations_.IsCurrent(id,generation))return;
        if(operations_.IsExpired(id,generation,Now())){
          if(CompleteExpiredExpectation(id,generation))return;
          Complete(id,false,"Native operation timed out");return;
        }
      }
      if((message->HasKey("nativeAccessibilitySnapshot")||message->HasKey("nativeSemanticSnapshotJson"))&&message->GetType("nativeQueryNodes")==VTYPE_STRING){
        auto release=CefDictionaryValue::Create();release->SetString("objectId",message->GetString("nativeQueryNodes"));
        SendDevTools("Runtime.releaseObject",release,operation_sessions_[id]);
      }
      auto parsed = CefParseJSON(json, JSON_PARSER_RFC); auto result = parsed ? parsed->GetDictionary() : nullptr;
      auto remote = result ? result->GetDictionary("result") : nullptr;
      auto value = remote ? remote->GetDictionary("value") : nullptr;
      first_observations_.erase(id);
      if (!result || result->HasKey("exceptionDetails") || !value) { Complete(id, true, "", json); return; }
      if(diagnostic_strict&&value->GetType("strictError")==VTYPE_STRING){
        if(operations_.IsExpired(id,generation,Now()))Complete(id,false,"Native operation timed out");
        else Complete(id,false,value->GetString("strictError").ToString());
        return;
      }
      if (value->GetBool("waiting")) {
        if(selector_waits_&&selector_waits_->Contains(id)){RetryContext(id);return;}
        const auto retry = lazy_queries_.contains(id) ? lazy_queries_.at(id) : message;
        CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation, retry] { self->QueryEvaluate(id, generation, retry); }), 20); return;
      }
      auto actual = value->GetValue("value");
      if (actual) remote->SetValue("value", actual->Copy()); else remote->SetNull("value");
      if(visibility_snapshots_.Contains(id)&&!SuiteCutVisibilitySnapshotReply(result,actual)){
        Complete(id,false,"Invalid native visibility boolean observation");return;
      }
      if (message->GetBool("expectation")) {
        auto observation = remote->GetDictionary("value");
        if (!observation || observation->GetType("matches") != VTYPE_BOOL) { Complete(id, false, "Invalid native assertion observation"); return; }
        if (observation->GetBool("matches") == message->GetBool("isNot")) {
          auto snapshot = Json(result);
          if (snapshot.size() > 65536) {
            auto abbreviated = CefListValue::Create(); abbreviated->SetString(0, "primitive"); abbreviated->SetString(1, "<actual snapshot exceeds 64 KiB>");
            observation->SetList("actual", abbreviated); observation->SetBool("truncated", true); snapshot = Json(result);
          }
          if(AssertionDeadlineExpire(id,generation))return;
          expect_results_[id] = std::move(snapshot);
          if(selector_waits_&&selector_waits_->Contains(id)){RetryContext(id);return;}
          const auto retry = lazy_queries_.contains(id) ? lazy_queries_.at(id) : message;
          CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation, retry] { self->QueryEvaluate(id, generation, retry); }), 20); return;
        }
      }
      if(AssertionDeadlineExpire(id,generation))return;
      if(assertion_deadlines_.Get(id,generation))assertion_deadlines_.Admitted(id,generation);
      const auto output=Json(result);
      if(message->GetBool("domGetter")&&operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
      Complete(id, true, "", output);
    };
    const auto handles = message->GetList("handles");
    if(message->HasKey("nativeHandle"))EvaluateFixedNode(id,message,"(async()=>{const handles=[];"+body+"})()",std::move(callback));
    else if(message->HasKey("nativeQueryNodes")) {
      WithFrame(id,FrameId(message),[this,id,message,body,callback=std::move(callback)](int) mutable {
        evaluation_groups_[id].insert(operation_sessions_[id]);
        auto params=CefDictionaryValue::Create();params->SetString("objectId",message->GetString("nativeQueryNodes"));params->SetString("functionDeclaration","async function(...handles){"+body+"}");
        params->SetBool("awaitPromise",true);params->SetBool("returnByValue",true);params->SetString("objectGroup","suitecut-automation-"+std::to_string(id));
        auto config=message->Copy(false);if(!config->HasKey("handles"))config->SetList("handles",CefListValue::Create());
        if(message->GetBool("dom"))DevTools(id,"Runtime.callFunctionOn",params,std::move(callback));
        else HandleArguments(id,config,params,false,[this,id,params,callback=std::move(callback)]() mutable {DevTools(id,"Runtime.callFunctionOn",params,std::move(callback));});
      },message->GetBool("dom"));
    } else if (handles && handles->GetSize()) {
      WithFrame(id, FrameId(message), [this, id, message, body, callback = std::move(callback)](int) mutable {
        evaluation_groups_[id].insert(operation_sessions_[id]);
        auto params = CefDictionaryValue::Create(); params->SetString("functionDeclaration", "async function(...handles){" + body + "}");
        params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        HandleArguments(id, message, params, true, [this, id, params, callback = std::move(callback)]() mutable {
          DevTools(id, "Runtime.callFunctionOn", params, std::move(callback));
        });
      });
    } else Evaluate(id, "(async()=>{const handles=[];" + body + "})()", std::move(callback), FrameId(message), message->GetBool("dom") && !QueryNeedsMainWorld(message), message->GetString("nativeAssertionKind").ToString());
  }
  void RetryContext(int id) {
    if(const auto owner=operations_.Get(id);owner&&LocatorHandlerPaused(id,owner->generation))return;
    if(const auto screenshot=screenshot_assertion_work_.find(id);screenshot!=screenshot_assertion_work_.end()){ScreenshotAssertionFreeze(screenshot->second->identity,false,nullptr,"Native screenshot context changed during capture preparation");return;}
    if(const auto entry=operations_.Get(id);entry&&AssertionDeadlineExpire(id,entry->generation))return;
    if(reference_frame_batches_.contains(id)){if(ReferenceFrameOperationCurrent(id))Complete(id,false,"Native aggregate snapshot frame context unavailable");return;}
    ax_captures_.erase(id);
    if (count_queries_.contains(id)) { CompleteCountZero(id); return; }
    if(visibility_snapshots_.Contains(id)){
      if(VisibilityScopeGone(id))CompleteVisibilityEmpty(id);
      else Complete(id,false,"Native visibility snapshot context unavailable before observation");
      return;
    }
    const auto entry = operations_.Get(id); const auto retry = context_retries_.find(id);
    // The SDK owns the callback until it resumes. Its native reacquisition uses
    // the current main frame and must not emit a second pause after navigation.
    if (artifacts_ && artifacts_->HookPending(id)) return;
    if (!entry || retry == context_retries_.end()) return;
    if(selector_waits_&&selector_waits_->Contains(id)){
      // No attempt can reenter while its old document still has physical
      // commands in flight. Their callbacks settle ownership but cannot run
      // query continuations after Pause, and the whole old group is released
      // below only after all genuine ACKs have arrived.
      selector_waits_->Pause(id);
      if(!selector_waits_->Ready(id)){
        if(!context_retry_queued_.insert(id).second)return;
        CefPostDelayedTask(TID_UI,new Task([self=CefRefPtr<Source>(this),id,generation=entry->generation]{
          if(self->operations_.IsCurrent(id,generation)){self->context_retry_queued_.erase(id);self->RetryContext(id);}
        }),10);return;
      }
    }
    if(!context_retry_queued_.insert(id).second)return;
    if (artifact_configs_.contains(id)) {
      artifact_configs_.at(id)->SetBool("__artifactNavigationRetry",true);
      if (artifacts_) artifacts_->Cancel(id);
    }
    for (auto it = devtools_.begin(); it != devtools_.end();) { if (it->second.owner == id && !it->second.retain_after_cancel) it = devtools_.erase(it); else ++it; }
    const auto pending = pending_handle_groups_.find(id);
    if (pending != pending_handle_groups_.end()) {
      auto params = CefDictionaryValue::Create(); params->SetString("objectGroup", pending->second);
      SendDevTools("Runtime.releaseObjectGroup", params, operation_sessions_[id]); pending_handle_groups_.erase(pending);
    }
    const auto groups = evaluation_groups_.find(id);
    if (groups != evaluation_groups_.end()) {
      const auto sessions = groups->second; evaluation_groups_.erase(groups);
      for (const auto& session : sessions) { auto params = CefDictionaryValue::Create(); params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id)); SendDevTools("Runtime.releaseObjectGroup", params, session); }
    }
    const auto callback = retry->second;
    CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this), id, generation = entry->generation, callback] {
      if (self->operations_.IsCurrent(id, generation)) { self->context_retry_queued_.erase(id);if(self->selector_waits_)self->selector_waits_->Resume(id);callback(); }
    }), 10);
  }
  void Command(const std::string& line) {
    if (!browser_ || closing_page_) {
      auto parsed = CefParseJSON(line, JSON_PARSER_RFC); auto message = parsed ? parsed->GetDictionary() : nullptr;
      if (message) writer_.Reply(message->GetInt("id"), false, "Native page is closed"); else writer_.ReleaseReply();
      return;
    }
    auto value = CefParseJSON(line, JSON_PARSER_RFC);
    auto message = value ? value->GetDictionary() : nullptr;
    if (!message || message->GetInt("version") != 1 || message->GetInt("id") < 1) {
      writer_.ReleaseReply();
      Error("Invalid native command");
      return;
    }
    const int id = message->GetInt("id");
    const auto command = message->GetString("command").ToString();
    const auto failed_action = message->GetString("action").ToString();
    const bool failed_owner_control = (command=="debugger"&&failed_action=="close") || command=="diagnostics" || command=="recoverPage" || command=="frameSubscription" || command=="continueEventPredicate" ||
      ((command=="waitEventPredicate" || command=="waitEvent") && message->GetString("event")=="crash") || command=="cancel" || command=="download" || command=="networkBody" || command=="routeCallback" ||
      ((command=="streaming" || command=="recording") && (failed_action=="status" || failed_action=="stop" || failed_action=="cancel"));
    if(command=="recoverPage"&&native_websocket_policy_recovery_blocked_){writer_.Reply(id,false,"Native WebSocket renderer ownership retired; recreate the page before routing again");return;}
    if (renderer_failed_ && !failed_owner_control) { writer_.Reply(id,false,"Browser renderer terminated"); return; }
    if (std::getenv("SUITECUT_NATIVE_PROTOCOL_DIAGNOSTICS") && command_diagnostic_messages_++ < 4000)
      std::fprintf(stderr, "native command page=%d id=%d command=%.64s\n", page_id_, id, command.c_str());
    if (operations_.Contains(id)) { writer_.ReleaseReply(); Error("Duplicate active native command ID"); return; }
    if (command == "claimConsoleArguments") { ClaimConsoleArguments(id,message); return; }
    if (command == "acknowledgeConsole") { AcknowledgeConsoleDelivery(id,message); return; }
    // Raw-frame observation remains available at operation capacity and does
    // not acquire an authored DOM/input operation or change rendering cadence.
    if (command == "frameSubscription") {
      if (message->GetType("enabled") != VTYPE_BOOL) {
        writer_.Reply(id, false, "Invalid native frame subscription"); return;
      }
      frame_transport_enabled_ = message->GetBool("enabled");
      // Replace any queued pre-subscription frame before acknowledging a new
      // subscription. An already-writing packet precedes this ACK on stdout.
      if (frame_transport_enabled_) EmitFrame();
      writer_.Reply(id, true);
      if (frame_transport_enabled_) {
        // The cached frame may itself reach Node before the ACK. Request a
        // genuine paint after it so a static page always resumes delivery.
        browser_->GetHost()->Invalidate(PET_VIEW);
      }
      return;
    }
    if (command == "locatorHandlerRegister") { LocatorHandlerRegister(id,message); return; }
    if (command == "continueAction") { ContinueAction(id,message); return; }
    if (command == "continueURLPredicate") { ContinueURLPredicate(id,message); return; }
    if (command == "continueEventPredicate") { ContinueEventPredicate(id,message); return; }
    // Cancellation is control traffic: it remains available at operation capacity.
    if (command == "cancel") {
      if (message->GetType("target") != VTYPE_INT || message->GetInt("target") < 1 || message->GetInt("target") == id) {
        writer_.Reply(id, false, "Invalid cancellation target"); return;
      }
      Complete(message->GetInt("target"), false, "Native operation canceled");
      writer_.Reply(id, true);
      return;
    }
    double timeout = 30000;
    if (message->HasKey("timeoutMs")) {
      if (message->GetType("timeoutMs") != VTYPE_INT || message->GetInt("timeoutMs") < 0 || message->GetInt("timeoutMs") > 120000) {
        writer_.Reply(id, false, "Invalid native timeout"); return;
      }
      timeout = message->GetInt("timeoutMs");
    }
    const auto binding_action = message->GetString("action").ToString();
    const bool binding_control = command == "bindings" && (binding_action == "resolve" || binding_action == "argument" || binding_action == "stage");
    const bool recording_control=(command=="recording"||command=="streaming")&&(binding_action=="stop"||binding_action=="cancel");
    const bool handle_disposal_control = command == "handle" && (binding_action == "dispose" || binding_action == "disposeMany");
    const bool highlight_control=command=="highlight"&&(binding_action=="hide"||binding_action=="claim");
    const auto operation = (command=="locatorHandlerRemove" || highlight_control || handle_disposal_control || binding_control || (command=="debugger"&&(binding_action=="open"||binding_action=="close"||binding_action=="resume"||binding_action=="stepOver"||binding_action=="stepInto"||binding_action=="stepOut")) || command=="recoverPage" || command=="networkBody" || recording_control) ? operations_.BeginControl(id,Now(),timeout) : operations_.Begin(id, Now(), timeout);
    if (!operation) { writer_.Reply(id, false, "Too many pending native operations"); return; }
    if (!deadline_timer_active_) {
      deadline_timer_active_ = true;
      CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] { self->Deadlines(); },suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()), 10);
    }
    if(!engine_.TrackLocatorHandlerCommand(id,page_id_,operation->generation)){Complete(id,false,"Native locator handler command capacity exceeded");return;}
    if(command=="evaluate"||command=="queryEvaluate"||command=="locator"||command=="input"||command=="form")network_publication_operations_.insert(id);
    if(!PrepareSnapshotReferenceScope(id,message))return;
    if(message->HasKey("locatorHandlerOrigin")){double cap=0;if(!engine_.LocatorHandlerAdmission(message,cap)||(cap&&(!operation->deadline_ms||operation->deadline_ms>cap)&&!operations_.BindOriginalDeadline(id,operation->generation,cap))){Complete(id,false,"Native locator handler child admission is stale or expired");return;}}
    if(NativeTraceBegin(id,message,command,timeout))return;
    DispatchCommand(id,message,command,timeout);
    if(suitecut::CurrentNativeWorkOrigin().locator_handler_invocation)if(const auto retry=context_retries_.find(id);retry!=context_retries_.end()){
      auto callback=retry->second;const auto origin=suitecut::CurrentNativeWorkOrigin();
      retry->second=[origin,callback=std::move(callback)]{const suitecut::NativeWorkOriginFrame scope(origin);callback();};
    }
  }
#include "page_activation_source.inc"
#include "sdk_browser_control.inc"
  void DispatchCommand(int id,Dict message,const std::string& command,double timeout){
    const auto operation=operations_.Get(id);if(!operation||!browser_||closing_page_)return;
    if(SDKBrowserControl(id,operation->generation,message,command,timeout))return;
    if(command=="locatorHandlerRemove"){LocatorHandlerRemove(id,message);return;}
    if(command=="highlight"){NativeHighlightCommand(id,message);return;}
    if(command=="pickLocator"){BeginLocatorPick(id,message);return;}
    if(command=="suggestLocator"){BeginLocatorSuggestion(id,message);return;}
    if(command=="normalizeLocator"){
      context_retries_[id]=[this,id,message]{BeginLocatorNormalization(id,message);};lazy_queries_[id]=message;BeginLocatorNormalization(id,message);return;
    }
    if(command=="snapshotReferences"){
      context_retries_[id]=[this,id,message]{BeginReferenceSnapshot(id,message);};lazy_queries_[id]=message;BeginReferenceSnapshot(id,message);return;
    }
    if(command=="recoverPage"){
      if(NativeVirtualTimeOwned()){Complete(id,false,"Renderer recovery is unsupported while native current-document virtual time owns this target");return;}
      BeginRendererRecovery(id,message,static_cast<int>(timeout));return;
    }
    if(command=="waitTimeout"){
      if((message->GetType("ms")!=VTYPE_INT&&message->GetType("ms")!=VTYPE_DOUBLE)||!std::isfinite(Number(message,"ms"))||Number(message,"ms")<0){Complete(id,false,"Invalid native wait duration");return;}
      operation_frames_[id]=FrameId(message);native_wait_until_[id]=Now()+Number(message,"ms");if(Number(message,"ms")==0)Complete(id,true);return;
    }
    if(NativeVirtualTimeOwned()&&(command=="navigate"||command=="navigateFrame"||command=="reload"||command=="historyBack"||command=="historyForward")){
      Complete(id,false,"Navigation is unsupported while native current-document virtual time is installed");return;
    }
    if(command=="clock"){NativeVirtualTimeCommand(id,operation->generation,message);return;}
    if(command=="bringToFront"){NativeBringToFront(id,operation->generation);return;}
    if(message->HasKey("nativeRootHandle")&&
       ((command!="queryEvaluate"&&command!="handle")||message->GetType("nativeRootHandle")!=VTYPE_INT||message->GetInt("nativeRootHandle")<1||
        message->GetType("query")!=VTYPE_LIST||message->HasKey("nativeHandle")||message->HasKey("framePath")||
        (command=="handle"&&(message->GetString("action")!="create"||message->HasKey("expression")||message->HasKey("function")||message->HasKey("queryFunction"))))){
      Complete(id,false,"Invalid native scoped selector request");return;
    }
    if (command == "queryEvaluate") {
      if(message->HasKey("nativeAssertionKind")&&(message->GetType("nativeAssertionKind")!=VTYPE_STRING||!suitecut::AssertionDeadlineControls::Supported(message->GetString("nativeAssertionKind").ToString())||!message->GetBool("dom")||!message->GetBool("expectation")||!message->GetBool("all"))){Complete(id,false,"Invalid native assertion deadline entry");return;}
      if((message->HasKey("diagnosticExpressionJSON")&&(message->GetType("diagnosticExpressionJSON")!=VTYPE_STRING||message->GetString("diagnosticExpressionJSON").length()>16384))||
         (message->HasKey("strictDiagnostics")&&(message->GetType("strictDiagnostics")!=VTYPE_BOOL||(message->GetBool("strictDiagnostics")&&(!message->GetBool("dom")||!message->GetBool("expectation")))))){
        Complete(id,false,"Invalid native query diagnostic request");return;
      }
      if(message->HasKey("visibilitySnapshot")&&(message->GetType("visibilitySnapshot")!=VTYPE_BOOL||
          (message->GetBool("visibilitySnapshot")&&(!message->GetBool("domGetter")||!message->GetBool("dom")||!message->GetBool("immediate")||!message->GetBool("visibility")||message->GetBool("all")||message->GetBool("count")||message->GetBool("expectation")||message->GetBool("elementState")||message->GetBool("stableState")||message->GetBool("accessibilitySnapshot")||message->HasKey("handles"))))){
        Complete(id,false,"Invalid native visibility snapshot request");return;
      }
      if(message->HasKey("domGetter")){
        if(message->GetType("domGetter")!=VTYPE_BOOL||(message->GetBool("domGetter")&&(!message->GetBool("dom")||message->GetBool("count")||message->GetBool("expectation")))){Complete(id,false,"Invalid native DOM getter request");return;}
        if(message->GetBool("domGetter")){
          if((message->HasKey("strict")&&message->GetType("strict")!=VTYPE_BOOL)||(message->HasKey("immediate")&&message->GetType("immediate")!=VTYPE_BOOL)||(message->GetBool("immediate")&&message->GetBool("all"))){Complete(id,false,"Invalid native DOM getter strictness or immediate mode");return;}
          native_dom_getters_.insert(id);
        }
      }
      if(message->HasKey("nativeAccessibilitySnapshot")||message->HasKey("nativeSemanticSnapshotJson")||message->HasKey("nativeQueryNodes")||
         (message->HasKey("accessibilitySnapshot")&&message->GetType("accessibilitySnapshot")!=VTYPE_BOOL)||
         (message->HasKey("accessibilityBoxes")&&message->GetType("accessibilityBoxes")!=VTYPE_BOOL)||
         (message->HasKey("accessibilitySemantics")&&message->GetType("accessibilitySemantics")!=VTYPE_BOOL)){
        Complete(id,false,"Invalid native accessibility snapshot request");return;
      }
      if (message->HasKey("count") && message->GetType("count") != VTYPE_BOOL) {
        Complete(id, false, "Invalid native locator count snapshot"); return;
      }
      const bool count = message->GetBool("count");
      if (!count && message->GetType("function") != VTYPE_STRING) { Complete(id, false, "Native locator evaluation requires a function"); return; }
      auto config = message->Copy(false);
      if(message->GetBool("visibilitySnapshot")){
        if(!visibility_snapshots_.Admit(id,operation->generation)){Complete(id,false,"Native visibility snapshot capacity exceeded");return;}
        // This native mode has a fixed boolean result; arbitrary author
        // callbacks cannot be run against a replacement or absent document.
        config->SetString("function","function(element,state){return !!state?.visible;}");
      }
      if (count) {
        if (message->GetBool("expectation") || message->HasKey("handles")) { Complete(id, false, "Invalid native locator count snapshot"); return; }
        count_queries_.insert(id);
        config->SetBool("all", true); config->SetBool("dom", true);
        config->SetString("function", "function(nodes){return ['primitive',nodes.length];}");
      }
      // First slice: a direct native utility-world scalar assertion with an
      // already admitted full document identity. Mixed/custom/fixed/frame-path
      // and accessibility captures retain the existing hard-error contract.
      const auto assertion_path=config->GetList("framePath");
      if(config->HasKey("nativeAssertionKind")&&((assertion_path&&assertion_path->GetSize())||FrameId(config)!=main_frame_id_||config->HasKey("nativeHandle")||config->HasKey("nativeRootHandle")||config->HasKey("handles")||config->GetBool("accessibility")||config->GetBool("accessibilitySnapshot")||QueryNeedsMainWorld(config)||QueryNeedsIsolatedWorld(config)||QueryUsesMultipleWorlds(config)))config->Remove("nativeAssertionKind");
      if (config->GetBool("expectation")&&!config->HasKey("nativeAssertionKind")) first_observations_[id] = Now() + 1000;
      if (const auto path = config->GetList("framePath"); path && path->GetSize()) lazy_queries_[id] = config;
      context_retries_[id] = [this, id, generation = operation->generation, config] { QueryEvaluate(id, generation, config); };
      QueryEvaluate(id, operation->generation, config);
    } else if (command == "streaming") {
      StreamingCommand(id, operation->generation, message);
    } else if (command == "recording") {
      RecordingCommand(id, operation->generation, message);
    } else if (command == "console") {
      ConsoleCommand(id,message);
    } else if (command == "dialog") {
      const auto action = message->GetString("action").ToString();
      if (action == "subscribe" && message->GetType("enabled") == VTYPE_BOOL) {
        dialogs_->Subscribe(message->GetBool("enabled")); Complete(id, true); return;
      }
      if (action != "answer" || message->GetType("dialogId") != VTYPE_STRING ||
          message->GetType("accept") != VTYPE_BOOL || message->GetType("promptText") != VTYPE_STRING) {
        Complete(id, false, "Invalid native dialog command"); return;
      }
      std::string error;
      const bool ok = dialogs_->Answer(message->GetString("dialogId"), message->GetBool("accept"), message->GetString("promptText"), error);
      Complete(id, ok, error);
    } else if (command == "bindings") {
      if (!bindings_) { Complete(id,false,"Native binding page unavailable"); return; }
      if(message->GetString("action")=="argument"){
        Dict config;std::string error;if(!bindings_->PrepareArgument(id,message,config,error)){Complete(id,false,error);return;}
        Handle(id,config);return;
      }
      bindings_->Command(id,message,[this,id](bool ok,const std::string& error,const std::string& json) { Complete(id,ok,error,json); });
    } else if (command == "urlPredicate") {
      StartURLPredicate(id, operation->generation, message);
    } else if (command == "waitEventPredicate") {
      StartEventPredicate(id, operation->generation, message);
    } else if (command == "networkBody") {
      const bool accepted=network_ && network_->BodyAcknowledged(message->GetString("bodyId"),message->GetString("stage"));
      Complete(id,accepted,accepted?"":"Invalid native network body acknowledgement","{}");
    } else if(command=="websocket") {
      const auto websocket_action=message->GetString("action").ToString();
      if(websocket_action.starts_with("routePayload")&&message->GetString("payloadScope")=="explicit"){NativeWebSocketExplicitPayloadCommand(id,operation->generation,message);return;}
      if(websocket_action=="routeRegister"||websocket_action=="routeUnregister"||websocket_action=="routeUnregisterAll"||websocket_action=="routePolicyCommand"||websocket_action.starts_with("routePayload")){NativeWebSocketPolicyCommand(id,operation->generation,message);return;}
      if(message->GetString("action").ToString().starts_with("route")){NativeWebSocketRouteCommand(id,operation->generation,message);return;}
      if(message->GetString("action")=="waitPredicate"){StartWebSocketPredicate(id,operation->generation,message);return;}
      if(!sockets_||message->GetString("action")!="waitEvent"||!sockets_->Wait(id,message->GetString("socketId"),message->GetString("event"),suitecut::DictionaryNumber(message,"afterSequence"),operation->deadline_ms,
        [this,id,generation=operation->generation](bool ok,Dict info,const std::vector<uint8_t>&bytes,const std::string&error){
          if(!operations_.IsCurrent(id,generation))return;
          if(!ok){Complete(id,false,error);return;}
          auto result=CefDictionaryValue::Create();result->SetDictionary("info",info->Copy(false));result->SetString("payloadBase64",CefBase64Encode(bytes.data(),bytes.size()));Complete(id,true,"",Json(result));
        }))Complete(id,false,"Invalid native WebSocket event wait or capacity exceeded");
    } else if(command=="har") {
      const auto action=message->GetString("action").ToString();
      if(action=="record"){std::string error;auto config=message->GetDictionary("options");const bool ok=engine_.har_recording().Configure(network_context_id_,config,error);Complete(id,ok,error,"{}");return;}
      if(action!="replay"){Complete(id,false,"Invalid native HAR action");return;}
      const auto registration=message->GetString("registrationId").ToString(),pattern=message->HasKey("url")?message->GetString("url").ToString():"**",not_found=message->HasKey("notFound")?message->GetString("notFound").ToString():"abort";
      if(registration.empty()||registration.size()>128||pattern.empty()||pattern.size()>8192||(not_found!="abort"&&not_found!="fallback")){Complete(id,false,"Invalid native HAR replay registration");return;}
      const auto admission_order=suitecut::NativeRouteRegistrationOrder::Reserve();if(!admission_order){Complete(id,false,"Native route registration order exhausted");return;}
      har_pending_[id]=registration;engine_.har_files().Read(id,message->GetString("path"),operation->deadline_ms,[self=CefRefPtr<Source>(this),id,generation=operation->generation,registration,pattern,not_found,admission_order=*admission_order](suitecut::NativeHARReplayFiles::Replay replay,const std::string&error){
        if(!self->operations_.IsCurrent(id,generation)||self->closing_page_)return;
        if(self->operations_.IsExpired(id,generation,Now())){self->Complete(id,false,"Native HAR original command deadline exceeded");return;}
        if(!replay){self->Complete(id,false,error);return;}
        self->network_->BindReplay(registration,pattern,std::move(replay),not_found=="fallback",[self,id,generation](bool ok,const std::string&reason){if(!self->operations_.IsCurrent(id,generation))return;if(self->operations_.IsExpired(id,generation,Now()))self->Complete(id,false,"Native HAR original command deadline exceeded");else self->Complete(id,ok,reason,"{}");},self->operations_.Get(id)->deadline_ms,false,admission_order);
      },not_found=="fallback");
    } else if (command == "network") {
      if (!network_) { Complete(id, false, "Native network session unavailable"); return; }
      network_->Command(id, message, [this, id, generation=operation->generation](bool ok, const std::string& error, const std::string& json) { if(operations_.IsCurrent(id,generation))Complete(id, ok, error, json); }, operation->deadline_ms, operation->generation, network_context_id_);
    } else if (command == "form") {
      if (message->GetString("action") == "dragTo") { NativeDragStart(id, operation->generation, message); return; }
      if (message->GetString("action") == "fileChooserSubscribe") { FileChooserSubscribe(id, message); return; }
      if (message->GetString("action") == "fileChooserElement") { FileChooserElement(id, message); return; }
      FormStart(id, operation->generation, message);
    } else if (command == "debugger") {
      NativeDeveloperCommand(id,message);
    } else if (command == "coverage") {
      NativeCoverageCommand(id,message);
    } else if (command == "artifact") {
      if (FrameId(message) != main_frame_id_) { Complete(id,false,"Native page artifacts require the main frame");return; }
      if(message->GetString("action")=="emulateIdleState"){NativeIdleBegin(id,operation->generation,message,operation->deadline_ms);return;}
      if(message->GetString("action")=="capturePrepare"){ArtifactInputPrepare(id,message);return;}
      if(message->GetString("action")=="captureCommit"){ArtifactInputCommit(id,message);return;}
      if(message->GetString("action")=="comparePrepare"){ScreenshotInputPrepare(id,message);return;}
      if(message->GetString("action")=="compareCommit"){ScreenshotInputCommit(id,message);return;}
      if(message->GetString("action")=="compareScreenshot"){ScreenshotComparisonStart(id,message);return;}
      artifact_configs_[id] = message->Copy(false);
      artifact_configs_[id]->SetInt("timeoutMs",static_cast<int>(timeout));
      context_retries_[id] = [this,id] { ArtifactStart(id); };
      ArtifactStart(id);
    } else if (command == "gc") {
      WithFrame(id, FrameId(message), [this, id](int) {
        DevTools(id, "HeapProfiler.collectGarbage", nullptr, [this, id](bool ok, const std::string&) {
          Complete(id, ok, ok ? "" : "Native garbage collection failed");
        });
      });
    } else if (command == "viewport") {
      if (artifacts_ && artifacts_->ViewportLocked()) { Complete(id,false,"Native viewport resize unavailable during screenshot rasterization"); return; }
      const int width = message->GetInt("width"), height = message->GetInt("height");
      if (message->GetType("width") != VTYPE_INT || message->GetType("height") != VTYPE_INT ||
          width < 1 || height < 1 || width > 3840 / scale_ || height > 2160 / scale_) {
        Complete(id, false, "Invalid native viewport dimensions"); return;
      }
      width_ = width; height_ = height;
      // Cached old-view bytes must not acquire the new view's CSS geometry.
      frame_css_width_ = frame_css_height_ = 0;
      browser_->GetHost()->NotifyScreenInfoChanged(); browser_->GetHost()->WasResized();
      operation_sessions_[id] = "";
      const auto params = context_settings_ ? context_settings_->ViewportMetrics({width,height},scale_) :
          suitecut::ContextSettings{}.ViewportMetrics({width,height},scale_);
      DevTools(id, "Emulation.setDeviceMetricsOverride", params, [this, id, width, height](bool ok, const std::string&) {
        auto response = CefDictionaryValue::Create(); response->SetInt("width", width); response->SetInt("height", height);
        Complete(id, ok, ok ? "" : "Native viewport resize failed", Json(response));
      });
    } else if (command == "input") {
      if(message->GetString("kind")=="touch"&&message->GetString("action")!="tap"){BeginTouch(id,message);return;}
      BeginInput(id, operation->generation, message);
    } else if (command == "navigateFrame" || command == "reload" || command == "historyBack" || command == "historyForward" || command == "waitLoadState" || command == "waitFrameNavigation") {
      BeginNavigation(id, message, command, static_cast<int>(timeout));
    } else if (command == "responseBody") {
      const auto session = message->GetString("session").ToString();
      const auto request = message->GetString("requestId").ToString();
      if (session.size() > 128 || request.empty() || request.size() > 256 ||
          (!session.empty() && !sessions_.contains(session) && !worker_sessions_.contains(session))) { Complete(id, false, "Native response context is unavailable"); return; }
      const auto key = session + "\n" + request;
      const auto document = documents_.find(key);
      if (document == documents_.end()) { Complete(id, false, "Native response is no longer retained"); return; }
      body_waiters_[id] = {key}; CheckBodies();
    } else if (command == "navigate") {
      const auto url = message->GetString("url").ToString();
      if (!url.starts_with("https://") && !url.starts_with("http://")) {
        Complete(id, false, "Navigation requires HTTP(S)"); return;
      }
      if (navigation_) { Complete(id, false, "Navigation already in progress"); return; }
      navigation_ = id;
      browser_->GetMainFrame()->LoadURL(url);
    } else if (command == "devtools") {
      const auto method=message->GetString("method").ToString();
      if (method.starts_with("Input.") && NativeDropBusy()) {
        Complete(id,false,"Raw Input command conflicts with native drag completion ownership"); return;
      }
      // Every SuiteCut page uses a CEF windowless view. Chromium's window
      // snapshot path dereferences the absent Aura platform window in OSR.
      const auto raw_params=message->GetDictionary("params");
      if(method=="Page.captureScreenshot"&&raw_params&&
          raw_params->GetType("fromSurface")==VTYPE_BOOL&&!raw_params->GetBool("fromSurface")){
        Complete(id,false,"Page.captureScreenshot fromSurface:false requires a platform window; SuiteCut CEF offscreen pages support surface capture only");return;
      }
      if(method=="Emulation.setVirtualTimePolicy"){
        if(NativeVirtualTimeOwned()){Complete(id,false,"Native virtual-time controller owns this target's policy");return;}
        native_virtual_time_raw_used_=true;
      }
      if(NativeVirtualTimeOwned()&&(method=="Performance.disable"||method=="Page.disable"||method=="Runtime.disable"||method=="Page.navigate"||method=="Page.reload"||method=="Page.navigateToHistoryEntry")){
        Complete(id,false,"This raw command conflicts with native current-document virtual-time ownership");return;
      }
      if(message->HasKey("frame")&&(message->GetType("frame")!=VTYPE_STRING||message->GetString("frame").empty()||message->GetString("frame").length()>256)){
        Complete(id,false,"Invalid native Inspector frame identity");return;
      }
      if((message->HasKey("frame")||method=="Page.getFrameTree")&&operations_.IsExpired(id,operation->generation,Now())){
        Complete(id,false,"Native operation timed out");return;
      }
      const auto dispatch=[this,id,generation=operation->generation,message,method]{
        if(!operations_.IsCurrent(id,generation))return;
        if((message->HasKey("frame")||method=="Page.getFrameTree")&&operations_.IsExpired(id,generation,Now())){
          Complete(id,false,"Native operation timed out");return;
        }
        const auto frame=frame_states_.find(FrameId(message));
        if(message->HasKey("frame")&&frame==frame_states_.end()){Complete(id,false,"Native frame detached or unavailable");return;}
        if(NativePristineRawLifecycleConflict(method,operation_sessions_[id])){
          if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
          Complete(id,false,"Raw Runtime/Page disable conflicts with native pristine document lifecycle ownership");return;
        }
        if((method=="DOM.enable"||method=="DOM.disable"||method.starts_with("Overlay."))&&operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
        if(NativeRawOverlayConflict(operation_sessions_[id],method)){Complete(id,false,"Raw Overlay mutation conflicts with native highlight ownership");return;}
        if(NativeRawDOM(id,generation,operation_sessions_[id],method,operations_.Get(id)->deadline_ms))return;
        if(method.starts_with("Overlay.")&&!method.starts_with("Overlay.get"))native_raw_overlay_sessions_.insert(operation_sessions_[id]);
        const auto loader=frame==frame_states_.end()?std::string{}:frame->second.loader;
        const auto epoch=frame==frame_states_.end()?uint64_t{}:frame->second.context_generation;
        const bool raw_debugger=method.starts_with("Debugger.");uint64_t raw_receipt=0;
        if(raw_debugger){
          if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
          const auto lease=operations_.Get(id);if(!lease)return;
          raw_receipt=NativeDebuggerDomains()->BeginRaw(operation_sessions_[id],method,lease->deadline_ms);
          if(!raw_receipt){Complete(id,false,"Raw Debugger command conflicts with native factory/coverage ownership, pending physical work or bounded admission");return;}
        }
        if(operation_sessions_[id].empty()&&(method=="Network.disable"||method=="Network.enable"))NativeHARPrimaryReset();
        DevToolsCallback actual_reply=[this,id,generation,message,method,loader,epoch,raw_receipt](bool success,const std::string& json){
          if(!operations_.IsCurrent(id,generation))return;
          if(raw_receipt&&operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
          if(method.starts_with("Overlay.")&&operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
          if(success&&method=="Page.getFrameTree"){
            if(json.size()>1048576){Complete(id,false,"Native Inspector frame tree exceeds 1 MiB");return;}
            const auto frame=frame_states_.find(FrameId(message));
            if(message->HasKey("frame")&&(frame==frame_states_.end()||frame->second.session!=operation_sessions_[id]||frame->second.loader!=loader||frame->second.context_generation!=epoch)){
              Complete(id,false,"Native frame document changed during Inspector tree capture");return;
            }
            const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);std::string output;
            if(!suitecut::InspectorFrameTreeJSON(parsed?parsed->GetDictionary():nullptr,output)){Complete(id,false,"Invalid or oversized native Inspector frame tree");return;}
            if(operations_.IsExpired(id,generation,Now())){Complete(id,false,"Native operation timed out");return;}
            Complete(id,true,"",output);return;
          }
          if(success&&method=="Overlay.disable")native_raw_overlay_sessions_.erase(operation_sessions_[id]);
          Complete(id,success,success?"":"DevTools command failed",json);
        };
        if(raw_debugger){const auto operation=operations_.Get(id);if(!operation){if(native_debugger_domains_)native_debugger_domains_->EndRaw(raw_receipt,false,false);return;}NativeDebuggerRawSend(id,raw_receipt,operation_sessions_[id],method,message->GetDictionary("params"),operation->deadline_ms,std::move(actual_reply));}
        else DevTools(id,method,message->GetDictionary("params"),std::move(actual_reply),std::nullopt,false,method=="Page.getFrameTree"?1048576:kMaxResponseBytes,method=="Page.getFrameTree");
      };
      if(message->HasKey("frame"))WithFrame(id,FrameId(message),[dispatch](int){dispatch();});
      else dispatch();
    } else if (command == "evaluate") {
      if (message->GetType("function") == VTYPE_STRING) {
        auto params = CefDictionaryValue::Create();
        params->SetString("functionDeclaration", message->GetString("function"));
        operation_frames_[id] = FrameId(message);
        const auto frame = frame_states_.find(FrameId(message));
        if (frame == frame_states_.end()) { Complete(id, false, "Native frame detached"); return; }
        operation_sessions_[id] = frame->second.session;
        params->SetBool("awaitPromise", true); params->SetBool("returnByValue", true); params->SetBool("userGesture", true);
        evaluation_groups_[id].insert(operation_sessions_[id]);
        params->SetString("objectGroup", "suitecut-automation-" + std::to_string(id));
        HandleArguments(id, message, params, true, [this, id, params] {
          DevTools(id, "Runtime.callFunctionOn", params, [this, id](bool ok, const std::string& json) {
            Complete(id, ok, ok ? "" : "Native handle argument evaluation failed", json);
          });
        });
      } else {
      if (message->GetType("expression") != VTYPE_STRING) { Complete(id, false, "Evaluation requires an expression"); return; }
      Evaluate(id, message->GetString("expression"), [this, id](bool success, const std::string& json) {
        Complete(id, success, success ? "" : "Native evaluation failed", json);
      }, FrameId(message));
      }
    } else if (command == "waitNavigation") {
      navigation_waiters_.insert(id);
    } else if (command == "waitEvent") {
      const auto event = message->GetString("event").ToString();
      if (event != "frameattached" && event != "framenavigated" && event != "framedetached" && event != "console" && event != "pageerror" && event != "crash" && event != "dialog" && event != "dialogclosed" && event != "filechooser" && event != "download" && event != "worker" && event != "workerclosed") { Complete(id, false, "Unsupported native event wait"); return; }
      if (message->HasKey("afterSequence")) {
        const double after = message->GetType("afterSequence") == VTYPE_INT
          ? message->GetInt("afterSequence") : message->GetDouble("afterSequence");
        if ((message->GetType("afterSequence") != VTYPE_INT && message->GetType("afterSequence") != VTYPE_DOUBLE) ||
            !std::isfinite(after) || after < 0 || std::floor(after) != after || after > static_cast<double>(event_sequence_)) {
          Complete(id, false, "Invalid native event sequence"); return;
        }
        if (!event_history_.empty() && after < static_cast<double>(event_history_.front().sequence - 1)) {
          Complete(id, false, "Native event journal capacity exceeded before wait registration"); return;
        }
        for (const auto& record : event_history_) if (static_cast<double>(record.sequence) > after && record.event == event) {
          Complete(id, true, "", record.json); return;
        }
      }
      event_waiters_[id] = event;
      if(event=="crash")crash_wait_owners_.insert(id);
    } else if (command == "worker") {
      WorkerDispatch(id,message);
    } else if (command == "handle") {
      if(message->HasKey("nativeSelectorWaitState")||message->HasKey("nativeQueryNodes")||message->HasKey("nativeFrameOwnerChild")){Complete(id,false,"Invalid native selector-wait internal field");return;}
      if(message->HasKey("selectorState")){
        const auto state=message->GetString("selectorState").ToString();
        if(message->GetType("selectorState")!=VTYPE_STRING||(state!="attached"&&state!="visible")||
           message->GetString("action")!="create"||message->GetType("query")!=VTYPE_LIST||
           message->HasKey("expression")||message->HasKey("function")||message->HasKey("queryFunction")||message->GetBool("all")||
           (message->HasKey("strict")&&message->GetType("strict")!=VTYPE_BOOL)){
          Complete(id,false,"Invalid native selector-wait request");return;
        }
        if(!SelectorWaits().Admit(id,operation->generation)){Complete(id,false,"Native selector-wait capacity exceeded");return;}
        auto config=message->Copy(false);config->Remove("selectorState");config->SetString("nativeSelectorWaitState",state);
        config->SetBool("predicate",true);config->SetInt("pollInterval",20);
        context_retries_[id]=[this,id,config]{Handle(id,config);};Handle(id,config);
      } else if (message->GetBool("predicate")) {
        auto config = message->Copy(false);
        context_retries_[id] = [this, id, config] { Handle(id, config); };
        Handle(id, config);
      } else Handle(id, message);
    } else if (command == "diagnostics") {
      auto response = CefDictionaryValue::Create();
      WorkerDiagnostics(response);
      if (network_) network_->Diagnostics(response);
      engine_.HTTPDiagnostics(response);
      response->SetInt("contextAuthenticationUpdates",static_cast<int>(engine_.authentication().pending()));
      response->SetInt("contextAuthenticationPhysicalFences",static_cast<int>(engine_.authentication().physical()));
      response->SetInt("contextAuthenticationTargets",static_cast<int>(engine_.authentication().targets()));
      const auto&auth_receipt=engine_.authentication().LastReceipt();response->SetInt("contextAuthenticationReceiptOwner",auth_receipt.owner);response->SetDouble("contextAuthenticationReceiptDeadline",auth_receipt.deadline);response->SetDouble("contextAuthenticationReceiptAt",auth_receipt.at);
      if(credentials_)credentials_->Diagnostics(response);
      response->SetInt("pendingNetworkPublicationResults",static_cast<int>(network_publication_results_.size()));
      response->SetDouble("pendingNetworkPublicationResultBytes",static_cast<double>(network_publication_results_.bytes()));
      response->SetInt("pendingPrivateBodyQueuedReads",private_response_?static_cast<int>(private_response_->waiting()):0);response->SetInt("pendingPrivateBodyInnerReceipts",private_response_?static_cast<int>(private_response_->physical()):0);response->SetInt("privateBodyRecords",private_response_?static_cast<int>(private_response_->records()):0);response->SetBool("privateBodySessionReady",private_response_&&private_response_->ready());if(private_response_){response->SetString("privateBodyTargetId",private_response_->target());response->SetString("privateBodyInspectorSessionId",private_response_->session());}if(!private_response_error_.empty())response->SetString("privateBodyUnavailable",private_response_error_);
      response->SetInt("pendingImageStreamCommands",image_streams_?static_cast<int>(image_streams_->physical()):0);response->SetInt("imageStreamEntries",image_streams_?static_cast<int>(image_streams_->entries()):0);response->SetDouble("imageStreamBytes",image_streams_?static_cast<double>(image_streams_->bytes()):0);
      response->SetInt("pendingRequestBodyReads",request_reads_?static_cast<int>(request_reads_->physical()):0);
      response->SetInt("pendingNetworkCommands", static_cast<int>(network_calls_.size()));
      response->SetBool("networkFatalPending",network_fatal_&&network_fatal_->pending());
      response->SetBool("networkFatalCloseRequested",network_fatal_&&network_fatal_->requested());
      size_t expired_network=0;for(const auto&[request,call]:network_calls_)if(call.expired)++expired_network;response->SetInt("expiredNetworkCommands",static_cast<int>(expired_network));
      response->SetInt("pendingAXCaptures",static_cast<int>(ax_captures_.size()));
      response->SetInt("pendingAXPhysicalCommands",static_cast<int>(ax_physical_));
      response->SetInt("pendingMediaPhysicalCommands",static_cast<int>(native_media_physical_));
      response->SetInt("queuedMediaCommands",static_cast<int>(native_media_queue_.size()));
      response->SetDouble("pendingAXCaptureBytes",static_cast<double>(ax_capture_bytes_));
      response->SetInt("pendingBindingCommands",static_cast<int>(binding_calls_.size()));
      response->SetInt("pendingPageErrorCaptures",page_errors_?static_cast<int>(page_errors_->Captures()):0);
      response->SetInt("pendingPageErrorPhysical",page_errors_?static_cast<int>(page_errors_->Physical()):0);
      response->SetInt("nativeHandleDisposals",handle_disposals_?static_cast<int>(handle_disposals_->Size()):0);
      response->SetInt("nativeHandleDisposalPhysical",handle_disposals_?static_cast<int>(handle_disposals_->Physical()):0);
      response->SetInt("nativeHandleDisposalHandles",handle_disposals_?static_cast<int>(handle_disposals_->Handles()):0);
      response->SetInt("nativeHandleDisposalFailures",handle_disposals_?static_cast<int>(handle_disposals_->Failures()):0);
      response->SetInt("pageErrorRetainedBytes",page_errors_?static_cast<int>(page_errors_->Bytes()):0);
      response->SetInt("pageErrorGroups",page_errors_?static_cast<int>(page_errors_->Groups()):0);
      response->SetInt("nativeElementStates",element_states_?static_cast<int>(element_states_->Size()):0);
      response->SetInt("nativeSelectorWaits",selector_waits_?static_cast<int>(selector_waits_->Size()):0);
      response->SetInt("nativeSelectorWaitPhysical",selector_waits_?static_cast<int>(selector_waits_->PhysicalSize()):0);
      response->SetInt("nativeOwnerFrameDocuments",static_cast<int>(owner_frame_documents_.size()));
      response->SetInt("nativeOwnerFrameGroups",static_cast<int>(OwnerFrameDocumentGroups()));
      response->SetInt("nativeOwnerFramePhysical",owner_frame_controls_?static_cast<int>(owner_frame_controls_->PhysicalSize()):0);
      response->SetInt("nativeOwnerFrameProcessPhysical",static_cast<int>(suitecut::OwnerFrameControls::ProcessPhysicalSize()));
      response->SetInt("nativeElementStatePhysical",element_states_?static_cast<int>(element_states_->PhysicalSize()):0);
      response->SetInt("nativeElementStateReleases",element_states_?static_cast<int>(element_states_->Pending()):0);
      response->SetInt("bindingControlOperations",static_cast<int>(operations_.Controls()));
      if (bindings_) bindings_->Diagnostics(response);
      response->SetInt("pendingUploadGroups",static_cast<int>(pending_uploads_.size()));
      response->SetInt("pendingUploadBytes",static_cast<int>(pending_upload_bytes_));
      response->SetInt("activeHandles", static_cast<int>(handles_.size()));
      response->SetInt("nativeDOMGetters",static_cast<int>(native_dom_getters_.size()));
      response->SetInt("nativeElementBounds",static_cast<int>(element_bounds_operations_.size()));
      response->SetInt("nativeLocatorPredicates",static_cast<int>(locator_predicate_operations_.size()));
      response->SetInt("nativeAriaJSON",static_cast<int>(aria_json_operations_.size()));
      response->SetInt("nativeVisibilitySnapshots",static_cast<int>(visibility_snapshots_.Size()));
      RendererRecoveryDiagnostics(response);
      response->SetInt("pausedActions",static_cast<int>(action_pauses_.Size()));
      response->SetInt("pendingURLWaits",static_cast<int>(url_waits_.size()));
      response->SetInt("pendingURLPredicates", static_cast<int>(url_predicates_.Size()));
      response->SetInt("pendingConsoleArguments",static_cast<int>(console_arguments_.size()));
      response->SetInt("consoleArgumentBytes",static_cast<int>(console_argument_bytes_));
      response->SetInt("consoleMessages",static_cast<int>(console_journal_.Size()));
      response->SetInt("consoleJournalBytes",static_cast<int>(console_journal_.Bytes()));
      response->SetInt("pageErrors",static_cast<int>(page_error_journal_.Size()));
      response->SetInt("pageErrorJournalBytes",static_cast<int>(page_error_journal_.Bytes()));
      response->SetInt("pendingConsoleMaterializations",static_cast<int>(console_materializations_.size()));
      response->SetInt("pendingEventPredicates", static_cast<int>(event_predicates_.Size()));
      response->SetInt("eventPredicateBytes", static_cast<int>(event_predicates_.PendingBytes()));
      response->SetInt("dialogs", dialogs_ ? static_cast<int>(dialogs_->Size()) : 0);
      response->SetInt("pendingFirstObservations", static_cast<int>(first_observations_.size()));
      response->SetInt("lazyQueries", static_cast<int>(lazy_queries_.size()));
      response->SetInt("countQueries", static_cast<int>(count_queries_.size()));
      response->SetInt("countContexts", static_cast<int>(count_contexts_.size()));
      response->SetInt("pendingChildBootstraps", static_cast<int>(pending_child_bootstraps_.size()));
      response->SetInt("contextRetryQueued", static_cast<int>(context_retry_queued_.size()));
      response->SetInt("nativeScreenshotAssertionScopes",int(screenshot_assertion_retirement_.Size()));response->SetInt("nativeScreenshotAssertionPhysical",int(screenshot_assertion_retirement_.Physical()));
      response->SetInt("artifacts",artifacts_?static_cast<int>(artifacts_->Size()):0);
      NativePristineFactories().Diagnostics(response);
      response->SetInt("pristineDispatchSourceContexts",native_pristine_contexts_.size());
      response->SetInt("pristineDispatchKnownContexts",native_pristine_seen_.Count());
      response->SetInt("pristineDispatchSourceSessions",native_pristine_sessions_.size());
      if(native_debugger_domains_)native_debugger_domains_->Diagnostics(response);
      NativeDeveloperDiagnostics(response);
      if(native_coverage_)native_coverage_->Diagnostics(response);
      response->SetInt("artifactPhysicalCapture", artifacts_ ? static_cast<int>(artifacts_->PhysicalCount()) : 0);response->SetInt("nativeTracePhaseJobs",native_trace_phases_?int(native_trace_phases_->Size()):0);response->SetDouble("nativeTracePNGReadbacks",double(native_trace_png_readbacks_));response->SetDouble("nativeTraceARIAReadbacks",double(native_trace_aria_readbacks_));
      response->SetInt("artifactDocumentJobs",static_cast<int>(artifact_document_work_.size()));
      response->SetInt("artifactDocumentContexts",static_cast<int>(ArtifactDocumentContextCount()));
      response->SetInt("formClicks",static_cast<int>(form_clicks_.size()));
      response->SetInt("temporaryUploadBytes",static_cast<int>(upload_store_.Bytes()));
      response->SetInt("temporaryUploadInputs",static_cast<int>(upload_store_.Inputs()));
      response->SetInt("pendingHandles", static_cast<int>(pending_handle_groups_.size()));
      response->SetInt("evaluationGroups", static_cast<int>(evaluation_groups_.size()));
      response->SetInt("nativeHighlights",native_highlights_?static_cast<int>(native_highlights_->Active()):0);
      response->SetInt("nativeHighlightResidents",native_highlights_?static_cast<int>(native_highlights_->Resident()):0);
      response->SetInt("nativeHighlightPhysical",static_cast<int>(native_highlight_calls_.size()));
      response->SetInt("nativeHighlightProcessPhysical",static_cast<int>(NativeHighlightProcessPhysical()));
      response->SetInt("nativeDOMDomainOwners",native_dom_domains_?static_cast<int>(native_dom_domains_->Owners()):0);
      response->SetInt("nativeDOMDomainPhysical",native_dom_domains_?static_cast<int>(native_dom_domains_->Physical()):0);
      response->SetInt("nativeDOMDomainReleases",native_dom_domains_?static_cast<int>(native_dom_domains_->PendingReleases()):0);
      response->SetInt("nativeSnapshotReferenceDispatches",static_cast<int>(reference_dispatch_.Pending()));
      response->SetInt("nativeSnapshotReferenceProcessDispatches",static_cast<int>(suitecut::SnapshotReferenceDispatch::ProcessPending()));
      response->SetInt("nativeSnapshotFrameBatches",static_cast<int>(reference_frame_batches_.size()));
      response->SetInt("nativeSnapshotReferences",snapshot_references_?static_cast<int>(snapshot_references_->Active()):0);
      response->SetInt("nativeSnapshotReferenceResidents",snapshot_references_?static_cast<int>(snapshot_references_->Resident()):0);
      response->SetInt("nativeSnapshotReferenceGroups",snapshot_references_?static_cast<int>(snapshot_references_->Groups()):0);
      response->SetInt("nativeSnapshotReferenceBytes",snapshot_references_?static_cast<int>(snapshot_references_->Bytes()):0);
      response->SetInt("nativeSnapshotReferenceInstalls",snapshot_references_?static_cast<int>(snapshot_references_->InstallPhysical()):0);
      response->SetInt("nativeSnapshotReferenceReleases",snapshot_references_?static_cast<int>(snapshot_references_->Physical()):0);
      response->SetInt("nativeSnapshotReferenceBorrows",snapshot_references_?static_cast<int>(snapshot_references_->Borrows()):0);
      response->SetInt("nativeSnapshotReferenceProcessInstalls",static_cast<int>(suitecut::SnapshotReferences::GlobalInstallPhysical()));
      response->SetInt("nativeSnapshotReferenceProcessReleases",static_cast<int>(suitecut::SnapshotReferences::GlobalReleasePhysical()));
      response->SetInt("nativeSnapshotReferenceProcessResidents",static_cast<int>(suitecut::SnapshotReferences::GlobalResident()));
      response->SetDictionary("nativeVirtualTime",NativeVirtualTimeInfo());
      response->SetInt("pendingDevTools", static_cast<int>(devtools_.size()));
      response->SetInt("queuedDevTools",static_cast<int>(devtools_dispatch_.size()));
      response->SetInt("queuedDevToolsBytes",static_cast<int>(devtools_dispatch_bytes_));
      response->SetInt("otherOperations", static_cast<int>(operations_.Size()) - 1);
      response->SetInt("frames", static_cast<int>(frame_states_.size()));
      response->SetInt("sessions", static_cast<int>(sessions_.size()));
      response->SetInt("pendingSessionTrees", static_cast<int>(session_trees_.size()));
      response->SetInt("navigations", static_cast<int>(navigations_.size()));
      response->SetInt("actionNavigations", static_cast<int>(action_navigations_.size()));
      int action_navigation_intents = 0;
      for (const auto& [owner,wait] : action_navigations_) action_navigation_intents += static_cast<int>(wait.intents.size());
      response->SetInt("actionNavigationIntents", action_navigation_intents);
      response->SetInt("bodyWaits", static_cast<int>(body_waiters_.size()));
      response->SetInt("activeRequests", static_cast<int>(active_requests_.size()));
      response->SetInt("documentResponses", static_cast<int>(documents_.size()));
      response->SetInt("eventHistoryBytes", static_cast<int>(event_history_bytes_));
      int default_contexts = 0, dom_contexts = 0;
      for (const auto& [frame, state] : frame_states_) { if (state.context) ++default_contexts; if (state.utility_context) ++dom_contexts; }
      response->SetInt("defaultContexts", default_contexts); response->SetInt("domContexts", dom_contexts);
      response->SetInt("pendingDomWorlds", static_cast<int>(utility_worlds_.size()));
      response->SetInt("inputWork", static_cast<int>(input_work_.size()));
      response->SetInt("pendingSendKeysPhysicalCommands", static_cast<int>(send_keys_physical_count_));
      response->SetInt("retainedSendKeysOwners", static_cast<int>(send_keys_physical_.size()));
      response->SetInt("queuedInput", static_cast<int>(mouse_queue_.size() + keyboard_queue_.size()));
      response->SetInt("pressedKeys", static_cast<int>(pressed_keys_.size()));
      response->SetInt("keyModifiers", key_modifiers_);
      response->SetInt("mouseButtons", mouse_buttons_);
      response->SetInt("nativeDragOperations",static_cast<int>(drag_work_.size()));
      response->SetInt("nativeDragCompletionRecords",native_drop_?static_cast<int>(native_drop_->Retained()):0);
      response->SetInt("nativeDragCompletionCallbacks",static_cast<int>(native_drop_transport_.size()));
      response->SetInt("nativeDragCompletionClosing",native_drop_?static_cast<int>(native_drop_->Closing()):0);
      response->SetBool("nativeDragCompletionAdmissionFailed",native_drop_failed_owner_!=0);
      response->SetInt("nativeDragGlobalRetainedRecords",static_cast<int>(NativeDropBudget()->records));
      response->SetDouble("nativeDragGlobalRetainedBytes",static_cast<double>(NativeDropBudget()->bytes));
      response->SetBool("nativeManualDragActive",manual_drag_!=nullptr);
      response->SetInt("pendingTouchPhysicalCommands",native_touch_?static_cast<int>(native_touch_->physical()):0);
      response->SetInt("queuedTouchCommands",native_touch_?static_cast<int>(native_touch_->queued()):0);
      response->SetInt("contextSettingsTargets",settings_page_?static_cast<int>(settings_page_->targets()):0);
      response->SetInt("contextSettingsPending",settings_page_?static_cast<int>(settings_page_->pending()):0);
      response->SetBool("contextSettingsPhysical",settings_page_&&settings_page_->physical());
      response->SetInt("geolocationTargets",static_cast<int>(engine_.geolocation().targets()));response->SetInt("geolocationUpdates",static_cast<int>(engine_.geolocation().pending()));response->SetInt("geolocationLogicalUpdates",static_cast<int>(engine_.geolocation().logical()));response->SetInt("geolocationInFlight",static_cast<int>(engine_.geolocation().inFlight()));
      response->SetString("initialDocumentURL",initial_document_url_);response->SetString("initialDocumentState",initial_document_state_);response->SetString("initialCEFFrame",initial_cef_frame_);response->SetInt("initialDocumentHistory",initial_document_history_);response->SetInt("initialExecutionContext",initial_execution_context_);response->SetBool("initialHistoryCandidate",initial_history_candidate_);response->SetInt("initialHistoryEntry",initial_history_entry_);
      response->SetBool("touchEnabled",native_touch_&&native_touch_->enabled());
      response->SetBool("touchContactHeld",native_touch_&&native_touch_->held());
      Complete(id, true, "", Json(response));
    } else if (command == "locator") {
      const auto selector = suitecut::InboundDisplay(suitecut::InboundString(message,"selector"));
      const auto action = message->GetString("action").ToString();
      if (selector.empty() || (action != "click" && action != "tap" && action != "hover" && action != "scrollIntoViewIfNeeded" && action != "selectText" && action != "fill" && action != "clear" && action != "focus" && action != "blur" && action != "dispatchEvent" && action != "inputValue" && action != "boundingBox" && action != "waitForFunction" && action != "ariaSnapshotJSON" && action != "press" && action != "type" && action != "sendKeys") ||
          ((action == "fill" || action == "type" || action == "sendKeys") && message->GetType("value") != VTYPE_STRING)) {
        Complete(id, false, "Invalid native locator action"); return;
      }
      if(action=="tap"&&(!native_touch_||!native_touch_->enabled())){Complete(id,false,"Touchscreen requires enabled native touch emulation (hasTouch)");return;}
      if((action=="waitForFunction"||action=="ariaSnapshotJSON")&&(message->HasKey("nativeQueryNodes")||message->HasKey("formNode")||message->HasKey("nativeAccessibilitySnapshot")||message->HasKey("nativeSemanticSnapshotJson"))){Complete(id,false,"Invalid native locator predicate internal field");return;}
      auto config = message->Copy(false); const auto text = action == "clear" ? std::string() : config->GetString("value").ToString();
      if(config->HasKey("diagnosticExpressionJSON")&&(config->GetType("diagnosticExpressionJSON")!=VTYPE_STRING||config->GetString("diagnosticExpressionJSON").length()>16384)){
        Complete(id,false,"Invalid native action diagnostic expression");return;
      }
      if ((config->HasKey("pauseBeforeStable") && config->GetType("pauseBeforeStable") != VTYPE_BOOL) ||
          (config->GetBool("pauseBeforeStable") && (config->GetType("clientToken") != VTYPE_STRING ||
           config->GetString("clientToken").ToString().empty() || config->GetString("clientToken").ToString().size() > 128))) {
        Complete(id,false,"Invalid native action hook identity");return;
      }
      locator_logs_[id] = action + " action" + (config->GetBool("trial") ? " (trial run)" : "") + "\n - resolving " + selector.substr(0,1024);
      if (config->HasKey("nativeHandle")) {
        if (config->GetType("nativeHandle") != VTYPE_INT) { Complete(id,false,"Invalid native element handle");return; }
        const auto handle=handles_.find(config->GetInt("nativeHandle"));
        if(handle==handles_.end()){Complete(id,false,MissingHandle(config->GetInt("nativeHandle")));return;}
        config->SetString("frame",handle->second.frame_id);
      }
      if (config->HasKey("modifiers")) {
        auto modifiers = config->GetList("modifiers");
        if (!modifiers || modifiers->GetSize() > 5) { Complete(id, false, "Invalid native input modifiers"); return; }
        for (size_t index = 0; index < modifiers->GetSize(); ++index) {
          auto key = modifiers->GetType(index) == VTYPE_STRING ? SuiteCutDescribeKey(modifiers->GetString(index), 0) : std::nullopt;
          if (!key || !key->modifier) { Complete(id, false, "Invalid native input modifier"); return; }
        }
        SetupLocatorModifiers(id, config);
      }
      context_retries_[id] = [this, id, generation = operation->generation, selector, action, text, config] { LocatorAction(id, generation, selector, action, text, config); };
      LocatorAction(id, operation->generation, selector, action, text, config);
    } else Complete(id, false, "Unknown native command");
  }
#include "accepted_source_work.inc"
  void ReadCommands() {
    SuiteCutCommandFramer framer;
    char buffer[4096];
    while (!stopped_) {
      pollfd fd{STDIN_FILENO, POLLIN, 0};
      if (poll(&fd, 1, 100) <= 0) continue;
      const auto size = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (size < 0 && errno == EINTR) continue;
      if (size <= 0) break;
      if (!framer.Push(std::string_view(buffer, static_cast<size_t>(size)), [this](std::string line) {
          // Pause the pipe reader at the UI-work bound. Load is backpressure,
          // not malformed input; the OS pipe then bounds transport buffering.
          {
            std::unique_lock<std::mutex> lock(command_mutex_);
            command_capacity_.wait(lock, [this] { return stopped_ || queued_commands_ < 64; });
            if (stopped_) return false;
            ++queued_commands_;
          }
          suitecut::AcceptedPostReceipt native_post(engine_.AcceptedReader());
          if (!writer_.ReserveReply(stopped_)) {
            { std::lock_guard<std::mutex> lock(command_mutex_); --queued_commands_; }
            command_capacity_.notify_one();
            return false;
          }
          const auto native_reader=engine_.AcceptedReader();const auto native_ticket=native_post.Ticket();
          auto task=new Task([self=CefRefPtr<Source>(this),line=std::move(line),native_reader,native_ticket]{
            { std::lock_guard<std::mutex> lock(self->command_mutex_); --self->queued_commands_; }
            self->command_capacity_.notify_one();
            {const suitecut::AcceptedRouteFrame native_route(native_reader,native_ticket,self->engine_.AcceptedSourceTicket());self->engine_.Route(line);}
            self->writer_.WorkChanged();
          });
          native_post.AboutToPost();const bool native_posted=CefPostTask(TID_UI,task);native_post.Returned(native_posted);writer_.WorkChanged();
          if(!native_posted){
            writer_.ReleaseReply();
            { std::lock_guard<std::mutex> lock(command_mutex_); --queued_commands_; }
            command_capacity_.notify_one();
            return false;
          }
          return true;
      })) { if (!stopped_) Error("Invalid or rejected native command stream"); stopped_ = true; }
    }
    if (!stopped_ && !framer.End()) Error("Truncated native command stream");
    CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
      self->engine_.LocatorHandlerChannelClosed();
      self->engine_.CloseAll();
    }));
  }
  const suitecut::NativeWorkContinuation native_creation_origin_;
  std::shared_ptr<suitecut::NativeLocatorHandlerAsyncWork> native_creation_handler_receipt_=suitecut::RetainNativeLocatorHandlerAsync();
  Writer& writer_;
  Engine& engine_;
  const int page_id_;
  bool closing_page_ = false;
  std::string closed_reason_;
  int width_, height_;
  const int capture_width_, capture_height_;
  double scale_;
  bool i420_;
  bool frame_transport_enabled_ = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-frame-transport") != "on-demand";
  int frame_width_ = 0, frame_height_ = 0, popup_width_ = 0, popup_height_ = 0, navigation_ = 0;
  int reported_view_width_ = 0, reported_view_height_ = 0;
  int frame_css_width_ = 0, frame_css_height_ = 0;
  bool renderer_failed_ = false;
  bool show_popup_ = false, ready_ = false, initial_loaded_ = false, deadline_timer_active_ = false;
  CefRect popup_rect_;
  std::vector<uint8_t> view_, popup_;
  // Damage is relative to the previous CEF paint, including paints deliberately
  // withheld during screenshot leases. Recover with one complete snapshot.
  bool view_damage_valid_ = false, popup_damage_valid_ = false;
  std::atomic<bool> stopped_{false}, audio_valid_{false};
  SuiteCutAudioClock audio_clock_;
  std::mutex command_mutex_;
  std::condition_variable command_capacity_;
  int queued_commands_ = 0;
  SuiteCutOperationRegistry operations_;
  std::map<std::string, FrameState> frame_states_;
  std::set<int> native_dom_getters_;
  suitecut::VisibilitySnapshotControls visibility_snapshots_;
  std::map<int, std::string> operation_frames_;
  std::map<int, std::string> operation_sessions_;
  std::map<std::string, std::string> sessions_;
  std::map<std::string, std::string> native_target_parents_;
  std::map<int, std::string> session_trees_;
  int next_devtools_ = 0;
  int diagnostic_network_enable_ = 0; // 0 unseen, positive real dispatch, -1 actual reply observed.
  size_t diagnostic_messages_ = 0;
  size_t command_diagnostic_messages_ = 0;
  std::map<int, std::string> event_waiters_;
  std::deque<EventRecord> event_history_;
  size_t event_history_bytes_ = 0;
  uint64_t event_sequence_ = 0;
  std::string main_frame_id_, last_cef_load_url_;
  int bootstrap_tree_ = 0;
  std::map<int, DevToolsCall> devtools_;
  std::map<int, std::function<void()>> context_retries_;
  std::map<int, Dict> artifact_configs_;
  std::map<int, uint64_t> artifact_attempts_;
  std::map<int, uint64_t> locator_attempts_;
  std::map<int, std::string> locator_logs_, locator_waits_;
  std::map<int, std::string> expect_results_;
  std::map<int, double> first_observations_;
  std::map<int, Dict> lazy_queries_;
  std::set<int> count_queries_;
  std::map<int, std::pair<std::string, int>> count_contexts_;
  std::set<int> context_retry_queued_;
  std::map<int, std::set<std::string>> evaluation_groups_;
  std::map<int, UtilityWorld> utility_worlds_;
  std::set<int> navigation_waiters_;
  std::map<int, Navigation> navigations_;
  std::map<int, BodyWait> body_waiters_;
  std::map<int,std::string>har_pending_;
  std::map<std::string, ActiveRequest> active_requests_;
  std::map<std::string, Document> documents_;
  std::deque<std::string> document_order_;

  std::map<int, ObjectHandle> handles_;
  std::unique_ptr<SuiteCutArtifactController> artifacts_;
  bool network_policy_ready_=false;
  bool popup_initial_navigation_observed_=false;
  std::unique_ptr<suitecut::NativeWebSocketControls> sockets_;
  suitecut::NativeNetworkPublicationResults network_publication_results_;
  std::set<int> network_publication_operations_;
  int network_context_id_=0;
  std::unique_ptr<suitecut::NativeRequestBodyReads> request_reads_;
  std::unique_ptr<suitecut::NativeResponseBodyReads> response_reads_;
  std::unique_ptr<suitecut::NativeResponseStreams> image_streams_;
  std::unique_ptr<suitecut::NativePrivateResponseSession> private_response_;
  bool private_response_ready_=false;std::string private_response_error_;
  std::unique_ptr<suitecut::NetworkControls> network_;
  std::unique_ptr<SuiteCutDialogControls> dialogs_;
  std::map<int, NetworkCall> network_calls_;
#include "credential_source.inc"
#include "context_geolocation_source.inc"
#include "context_settings_source.inc"
#include "idle_emulation.inc"
#include "initial_navigation.inc"
  std::map<int, BindingCall> binding_calls_;
  std::unique_ptr<suitecut::BindingControls> bindings_;
  bool file_chooser_subscribed_ = false;
  Dict manual_drag_;
  std::map<int, std::shared_ptr<NativeDragWork>> drag_work_;
  std::map<int,std::function<void()>> form_clicks_;
  std::map<int,std::shared_ptr<SuiteCutUploadFiles>> form_uploads_;
  std::map<int,FormFolderObserver> folder_observers_;
  int folder_operation_ = 0;
  uint64_t folder_generation_ = 0;
  std::string folder_path_;
  bool folder_continued_ = false;
  SuiteCutUploadStore upload_store_;
  std::map<std::string, SuiteCutPendingUpload> pending_uploads_;
  size_t pending_upload_bytes_ = 0;
  std::map<int, std::string> pending_handle_groups_;
  std::map<int, std::shared_ptr<InputWork>> input_work_;
  std::map<int, std::vector<std::string>> locator_modifiers_;
  std::deque<int> mouse_queue_, keyboard_queue_;
  int mouse_input_ = 0, keyboard_input_ = 0, mouse_buttons_ = 0, key_modifiers_ = 0;
  double mouse_x_ = 0, mouse_y_ = 0;
  std::map<std::string, SuiteCutKey> pressed_keys_;
  int next_handle_ = 0;
  const std::string handle_namespace_;
  std::thread reader_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefRegistration> observer_;
  IMPLEMENT_REFCOUNTING(Source);
};

#include "accepted_engine_work.inc"

Engine::Engine(Writer& writer, int width, int height, double scale, bool i420, int fps)
    : writer_(writer), width_(width), height_(height), fps_(fps), scale_(scale), i420_(i420), authentication_([] { return Now(); }) {
  websocket_routes_=std::make_unique<suitecut::NativeWebSocketRoutes>([]{return Now();},[this]{WakeTrace();},[this](Dict event,std::vector<uint8_t>payload){event->SetInt("version",1);event->SetString("type","websocketroute");return writer_.TryEvent(event,std::move(payload));});
  network_archive_=std::make_unique<suitecut::NativeNetworkArchives>([]{return Now();},[this](const std::string&error){writer_.Error(error.c_str());});
  context_routes_=std::make_unique<suitecut::NativeContextRoutes>([this](const std::string&owner,const std::string&error){HARPolicyFailure(owner,error);},[]{return Now();});
  context_network_=std::make_unique<suitecut::NativeContextNetworkControls>([this](Dict info){const auto json=Json(info);writer_.Send(Header("contextnetwork"),std::vector<uint8_t>(json.begin(),json.end()));});
  worker_network_=std::make_unique<suitecut::NativeContextWorkerNetwork>([this](int context,Dict worker,Dict event){if(!context_network_->ObserveWorker(context,worker,event))writer_.Error("Native context worker journal rejected actual metadata");});
  worker_network_->SetRouteEmitter([this](int context,const std::string&owner,Dict worker,Dict event){ContextRoute(context,owner,0,worker,event);});
  context_console_=std::make_unique<suitecut::NativeContextConsoleControls>([this](Dict info){auto typed=CefValue::Create();typed->SetDictionary(info->Copy(false));std::string json;if(!suitecut::DevToolsLosslessJSON(typed,json,1048576)){writer_.Error("Native context console output limit exceeded");return;}writer_.Send(Header("contextconsole"),std::vector<uint8_t>(json.begin(),json.end()));});
  suitecut::RecordingControls::Hooks recording_hooks;
  recording_hooks.dispatch=[alive=alive_](std::function<void()> task){
    CefPostTask(TID_UI,new Task([alive,task=std::move(task)]{if(*alive)task();}));
  };
  recording_hooks.event=[this](int page,const std::string& id,const std::string& phase,const suitecut::MediaPipelineResult& result){
    if(context_video_)context_video_->Observe(page,id,phase,result);
    if(page==1&&startup_operations_.Contains(1)){
      // There are no public recording subscribers before launch resolves. Keep
      // the real terminal receipt in NativeContextVideo without violating the
      // initial ready/error packet boundary if paint beats page bootstrap.
      if(phase=="failed")StartupFailed(result.failure.empty()?"Native launch recording encoder failed":result.failure);
      return;
    }
    auto header=Header("recording");header->SetInt("pageId",page);
    const auto json=Json(Source::RecordingResult(id,result,phase));
    writer_.Send(header,std::vector<uint8_t>(json.begin(),json.end()));
  };
  recording_hooks.now=[] { return Now(); };
  recording_hooks.physical_settled=[this](int,int context){contexts_->ContinueCloseContext(context);Idle();};
  recordings_=std::make_shared<suitecut::RecordingControls>(std::move(recording_hooks));
#if defined(OS_LINUX)
  suitecut::StreamingControls::Hooks streaming_hooks;
  streaming_hooks.dispatch=[alive=alive_](std::function<void()> task){
    CefPostTask(TID_UI,new Task([alive,task=std::move(task)]{if(*alive)task();}));
  };
  streaming_hooks.event=[this](int page,const std::string& id,const std::string& phase,const suitecut::StreamPipelineResult& result){
    auto header=Header("streaming");header->SetInt("pageId",page);
    const auto json=Json(Source::StreamingResult(id,result,phase,streaming_->ClientToken(id)));
    writer_.Send(header,std::vector<uint8_t>(json.begin(),json.end()));
  };
  streaming_hooks.physical_settled=[this](int owner_context,int selected_context){
    contexts_->ContinueCloseContext(owner_context);
    if(selected_context!=owner_context)contexts_->ContinueCloseContext(selected_context);
    Idle();
  };
  streaming_=std::make_shared<suitecut::StreamingControls>(std::move(streaming_hooks));
#endif
  authentication_.SetSettled([this](int context){if(contexts_)contexts_->ContinueCloseContext(context);WakeTrace();Idle();});
  SuiteCutContextControls::Hooks hooks;
  hooks.internal_cookies=[this](int owner,int context,const std::string&action,Dict params,double deadline,SuiteCutContextControls::Reply reply){return http_&&http_->InternalCookieCommand(owner,context,action,params,deadline,std::move(reply));};
  hooks.validate_internal_storage=[this](int owner,int context,Dict state,double deadline,std::string&error){return http_&&http_->ValidateInternalStorage(owner,context,state,deadline,error);};
  hooks.validate_internal_cookies=[this](int owner,int context,CefRefPtr<CefListValue>cookies,double deadline,std::string&error){return http_&&http_->ValidateInternalCookies(owner,context,cookies,deadline,error);};
  hooks.cookie_channel=[this](int owner,int context,suitecut::CookieMethod method,Dict params,double deadline,suitecut::CookieReply reply){workers_->CookieControl(owner,context,method,params,deadline,std::move(reply));};
  hooks.cookie_deadline=[this](int owner){const auto operation=operations_.Get(owner);return operation?operation->deadline_ms:0;};
  hooks.cancel_cookie=[this](int owner){workers_->CancelCookie(owner);};
  hooks.context_created=[this](int context,Dict params,std::string& error){
    if(params->HasKey("offline") && params->GetType("offline")!=VTYPE_BOOL){error="Native offline option must be a boolean";return false;}
    if(!network_policy_.Seed(context,params->GetBool("offline"))){error="Native network context policy capacity exceeded";return false;}
    auto headers=params->HasKey("extraHTTPHeaders")?params->GetDictionary("extraHTTPHeaders"):CefDictionaryValue::Create();
    if(!network_headers_.Seed(context,headers)){network_policy_.Closed(context);error="Invalid or excessive native context HTTP headers";return false;}
    if(!settings_.Seed(context,params,error)){network_headers_.Closed(context);network_policy_.Closed(context);return false;}
    const auto config=settings_.Get(context);
    if(!authentication_.Seed(context,config->http_credentials_all)){error="Native context HTTP authentication capacity exceeded";return false;}
    suitecut::NativeGeolocationValue geo;geo.configured=params->HasKey("geolocation");if(config->geolocation)geo.coordinates=suitecut::NativeGeolocationValue::Coordinates{config->geolocation->latitude,config->geolocation->longitude,config->geolocation->accuracy};
    if(!geolocation_.Seed(context,std::move(geo))){error="Native geolocation context capacity or value is invalid";return false;}
    if(!config->CaptureDimensions(width_,height_,scale_,error))return false;
    if(!context_routes_->Seed(context)){error="Native context route policy capacity exceeded";return false;}
    if(!context_har_->Seed(context)){error="Native context HAR policy capacity exceeded";return false;}
    if(params->HasKey("recordVideo")){
      const int owner=params->GetInt("internalOwner");const auto prepared=video_preparations_.find(owner);
      const auto operation=operations_.Get(owner);
      if(prepared==video_preparations_.end()||!operation||operation->generation!=prepared->second.generation){error="Native context video preparation is missing or stale";return false;}
      const auto value=prepared->second.value;video_preparations_.erase(prepared);
      if(!context_video_->Configure(context,value,error))return false;
    }
    return !params->HasKey("recordHAR")||har_recording_->Configure(context,params->GetDictionary("recordHAR"),error);
  };
  hooks.context_removed=[this](int context){websocket_routes_->Closed(context);if(context_video_)context_video_->Removed(context);const auto failure=authentication_failures_.find(context);if(failure!=authentication_failures_.end()){failure->second->Closed();authentication_failures_.erase(failure);}context_unroute_all_->Closed(context);context_routes_->Closed(context);context_har_->Closed(context);har_recording_->Removed(context);geolocation_.Closed(context);authentication_.Closed(context);network_headers_.Closed(context);network_policy_.Closed(context);settings_.Drop(context);};
  hooks.create_page = [this](int page, int context, CefRefPtr<CefRequestContext> cef) { return CreatePage(page, context, cef); };
  hooks.before_page_close=[this](int page,double deadline,std::function<void()> continuation){
    const auto recording_deadline=Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(deadline?std::max(0.0,deadline-Now()):60000));
    har_recording_->BeforePageClose(contexts_->ContextId(page),"page:"+std::to_string(page),deadline,[this,page,recording_deadline,continuation=std::move(continuation)]()mutable{
    recordings_->BeforePageClose(page,[this,page,continuation=std::move(continuation)]() mutable {
#if defined(OS_LINUX)
      streaming_->BeforePageClose(page,Clock::now()+std::chrono::seconds(60),[this,page,continuation=std::move(continuation)]() mutable {
        downloads_->BeforePageClose(page,std::move(continuation));
      });
#else
      downloads_->BeforePageClose(page,std::move(continuation));
#endif
    },recording_deadline);
    });
  };
  hooks.operation_deadline=hooks.cookie_deadline;
  hooks.operation_now=[] {return Now();};
  hooks.operation_generation=[this](int owner){const auto operation=operations_.Get(owner);return operation?operation->generation:uint64_t{0};};
  hooks.operation_current=[this](int owner,uint64_t generation){return operations_.IsCurrent(owner,generation);};
  hooks.context_cleanup_error=[this](int context){const auto error=har_recording_->Error(context);return !error.empty()?error:context_video_->Error(context);};
  hooks.page_metadata=[this](int page)->Dict{const auto video=context_video_->Metadata(page);if(!video)return nullptr;auto result=CefDictionaryValue::Create();auto descriptor=CefDictionaryValue::Create();descriptor->SetString("id",video->id);result->SetDictionary("video",descriptor);return result;};
  hooks.context_closing=[this](int context,double deadline){if(http_)http_->ContextClosing(context);geolocation_.Closing(context);authentication_.Closing(context);traces_->Closed(context);trace_sources_->Closed(context);
#if defined(OS_LINUX)
    streaming_->FinalizeContext(context,Clock::now()+std::chrono::seconds(60));
#endif
    recordings_->FinalizeContext(context,Now(),Clock::now()+std::chrono::seconds(60));downloads_->ContextClosing(context);har_recording_->BeforeClose(context,deadline,[this,context]{workers_->CloseContext(context);});};
  hooks.context_cleanup_ready=[this](int context){return authentication_.Ready(context)&&(!http_||http_->ContextReady(context))&&har_recording_->ContextReady(context)&&traces_->ContextReady(context)&&screenshot_comparisons_->ContextReady(context)&&screenshot_inputs_->ContextReady(context)&&
#if defined(OS_LINUX)
    streaming_->ContextReady(context)&&
#endif
    recordings_->ContextReady(context)&&context_video_->ContextReady(context)&&downloads_->ContextCleanupReady(context)&&workers_->ContextClosed(context);};
  hooks.context_gone=[this](int context){
    workers_->ContextGone(context);
    worker_network_->Closed(context);network_archive_->Closed(context);context_console_->Closed(context);context_network_->Closed(context);
    if(http_)http_->ContextClosed(context);
    if(downloads_)downloads_->ContextGone(context);
#if defined(OS_LINUX)
    if(streaming_)streaming_->ContextGone(context);
#endif
  };
  hooks.event=[this](Dict event){
    auto header = Header("context"); const auto json = Json(event);
    writer_.Send(header, std::vector<uint8_t>(json.begin(), json.end()));
  };
  hooks.idle = [this] { Idle(); };
  hooks.page_closing = [this](int page, const std::string& reason) {
    const auto found = sources_.find(page); if (found != sources_.end()) {found->second->closed_reason_ = reason;found->second->native_trace_page_closing_=true;found->second->context_auth_open_=false;}
  };
  hooks.seed_binding = [this](int page,Dict config,std::string& error) {
    const auto source = sources_.find(page);
    if (source == sources_.end() || !source->second->bindings_ || source->second->closing_page_) { error = "Native page closed before binding seed"; return false; }
    return source->second->bindings_->SeedInherited(suitecut::InboundString(config,"name"),config->GetString("bridge"),config->GetString("installer"),config->GetString("codec"),config->GetBool("handle"),error);
  };
  hooks.register_binding = [this](int page,int owner,Dict config,std::function<void(bool,const std::string&)> done) {
    const auto source = sources_.find(page);
    if (source == sources_.end() || !source->second->bindings_ || source->second->closing_page_) { done(false,"Native page closed during context binding registration"); return; }
    source->second->bindings_->RegisterInherited(owner,config,[done=std::move(done)](bool ok,const std::string& error,const std::string&) { done(ok,error); });
  };
  hooks.remove_binding = [this](int page,int owner,const std::string& name,const std::string& bridge) {
    const auto source = sources_.find(page); if (source == sources_.end() || !source->second->bindings_) return;
    if (owner) source->second->bindings_->Cancel(owner);
    source->second->bindings_->RemoveInherited(name,bridge);
  };
  hooks.apply_credentials=[this](int page,int owner,std::shared_ptr<const suitecut::CredentialPage::Config>config,suitecut::CredentialPage::Result done){
    const auto source=sources_.find(page);if(source==sources_.end()||source->second->closing_page_||!source->second->credentials_){done(false,"Native credential page closed");return;}
    source->second->credentials_->Apply(owner,std::move(config),std::move(done));
  };
  hooks.cancel_credentials=[this](int page,int owner){const auto source=sources_.find(page);if(source!=sources_.end()&&source->second->credentials_)source->second->credentials_->Cancel(owner);};
  hooks.adopt_credentials=[this](int page,std::shared_ptr<const suitecut::CredentialPage::Config>config){const auto source=sources_.find(page);return source!=sources_.end()&&!source->second->closing_page_&&source->second->credentials_&&source->second->credentials_->AdoptObserved(std::move(config));};
  contexts_ = std::make_shared<SuiteCutContextControls>(std::move(hooks));
  SuiteCutContextWorkers::Hooks worker_hooks;
  worker_hooks.websocket_required=[this,alive=alive_](int context){return *alive&&websocket_routes_->HasPolicy(context,0);};
  worker_hooks.websocket_failure=[this,alive=alive_](int context,const std::string&error){if(*alive)AuthenticationFatal(context,"Inherited native WebSocket worker policy failed: "+error);};
  worker_hooks.attach_websocket=[this,alive=alive_](int context,const std::string&owner,suitecut::NativeWebSocketRoutes::Send send,suitecut::NativeWebSocketRoutes::Fatal fatal,SuiteCutContextWorkers::NetworkResult ready){if(!*alive){ready(false,"Native Engine is closed");return true;}return websocket_routes_->Attach(context,0,owner,std::move(send),std::move(fatal),std::move(ready));};
  worker_hooks.detach_websocket=[this,alive=alive_](const std::string&owner){if(*alive)websocket_routes_->DetachPrefix(owner);};
  worker_hooks.websocket_event=[this,alive=alive_](const std::string&owner,Dict event){return *alive&&websocket_routes_->Event(owner,event);};
  worker_hooks.route_fetch=[this,alive=alive_](int owner,int context,CefRefPtr<CefRequestContext> expected,Dict request,Dict options,double deadline,std::shared_ptr<void> lease,std::function<bool(bool,Dict,const std::string&)> done){
    if(!*alive||!http_){done(false,nullptr,"Native worker HTTP context engine is unavailable");return;}
    http_->RouteFetchContext(owner,{std::move(expected),context},request,options,std::move(done),deadline,std::move(lease));
  };
  worker_hooks.cancel_route_fetch=[this,alive=alive_](int owner){if(*alive&&http_)http_->Cancel(owner);};
  worker_hooks.network_owner=[this,alive=alive_](int context,const std::string&owner,SuiteCutContextWorkers::PublicNetworkCommand command,SuiteCutContextWorkers::BodyFetch body,std::function<void(int)>cancel){return *alive&&worker_network_->Attach(context,owner,true,std::move(command),std::move(body),std::move(cancel));};
  worker_hooks.network_closed=[this,alive=alive_](const std::string&owner){if(*alive)worker_network_->Detach(owner);};
  worker_hooks.network_event=[this,alive=alive_](int context,const std::string&owner,Dict worker,Dict event){if(*alive)ContextWorkerNetwork(context,owner,worker,event);};
  worker_hooks.network_body=[this,alive=alive_](int context,const std::string&owner,uint64_t body,size_t offset,size_t total,std::span<const uint8_t>bytes,bool canceled){if(!*alive)return false;auto header=Header("contextnetworkbody");header->SetInt("contextId",context);header->SetString("owner",owner);header->SetString("bodyId",std::to_string(body));header->SetInt("offset",int(offset));header->SetInt("totalBytes",int(total));if(canceled)header->SetBool("canceled",true);return writer_.TryEvent(header,std::vector<uint8_t>(bytes.begin(),bytes.end()));};
  worker_hooks.authentication=[this](int context){return suitecut::NativeContextAuthentication(settings_.Get(context),authentication_.Get(context));};
  worker_hooks.attach_authentication=[this](int context,const std::string&target,suitecut::NativeContextAuthenticationControls::Apply apply,SuiteCutContextWorkers::NetworkResult ready){return authentication_.Attach(context,target,std::move(apply),std::move(ready));};
  worker_hooks.detach_authentication=[this](const std::string&target){authentication_.Detach(target);};
  worker_hooks.attach_network=[this,alive=alive_](int context,const std::string& target,SuiteCutContextWorkers::NetworkApply apply,SuiteCutContextWorkers::NetworkResult ready){if(!*alive){ready(false,"Native engine is closed");return true;}return network_policy_.Attach(context,target,std::move(apply),std::move(ready));};
  worker_hooks.detach_network=[this,alive=alive_](const std::string& target){if(*alive)network_policy_.Detach(target);};
  worker_hooks.attach_headers=[this,alive=alive_](int context,const std::string&target,SuiteCutContextWorkers::HeaderApply apply,SuiteCutContextWorkers::NetworkResult ready){if(!*alive){ready(false,"Native engine is closed");return true;}return network_headers_.Attach(context,target,std::move(apply),std::move(ready));};
  worker_hooks.attach_routes=[this,alive=alive_](int context,const std::string&owner,suitecut::NativeContextRoutes::Apply apply,SuiteCutContextWorkers::NetworkResult ready){if(!*alive){ready(false,"Native Engine is closed");return true;}return context_routes_->Attach(context,owner,std::move(apply),std::move(ready));};
  worker_hooks.detach_routes=[this,alive=alive_](const std::string&owner){if(*alive)context_routes_->Detach(owner);};
  worker_hooks.attach_har=[this,alive=alive_](int context,const std::string&owner,suitecut::NativeContextHARRoutes::Apply apply,SuiteCutContextWorkers::NetworkResult ready){if(!*alive){ready(false,"Native Engine is closed");return true;}return context_har_->Attach(context,owner,std::move(apply),std::move(ready));};
  worker_hooks.detach_har=[this,alive=alive_](const std::string&owner){if(*alive)context_har_->Detach(owner);};
  worker_hooks.detach_headers=[this,alive=alive_](const std::string&target){if(*alive)network_headers_.Detach(target);};
  worker_hooks.resource_owner=[this,alive=alive_](int context,const std::string&owner,SuiteCutContextWorkers::BodyFetch fetch){return *alive&&network_archive_->Attach(context,owner,std::move(fetch),suitecut::NativeHARCapture::OwnerKind::WorkerBroker);};
  worker_hooks.resource_post=[this,alive=alive_](int context,const std::string&owner,const std::string&id){if(!*alive)return SuiteCutContextWorkers::PostReservation{};auto reserved=network_archive_->ReservePost(context,owner,id);if(network_archive_->NeedsTick())WakeTrace();return SuiteCutContextWorkers::PostReservation{std::move(reserved.data),std::move(reserved.receipt)};};
  worker_hooks.resource_closed=[this,alive=alive_](const std::string&owner){if(*alive)network_archive_->Detach(owner);};
  worker_hooks.resource_request=[this,alive=alive_](int context,const std::string&owner,const std::string&session,Dict raw){if(*alive&&!network_archive_->OriginalRequest(context,owner,session,raw))writer_.Error("Native worker archive original request rejected");if(*alive&&network_archive_->NeedsTick())WakeTrace();};
  worker_hooks.resource_event=[this,alive=alive_](int context,const std::string&owner,Dict event){const auto kind=event->GetString("event");if(kind!="request"&&kind!="requestupdated"&&kind!="response"&&kind!="requestfinished"&&kind!="requestfailed")return;if(*alive&&!network_archive_->Observe(context,owner,event,0))writer_.Error("Native worker archive metadata rejected");if(*alive&&network_archive_->NeedsTick())WakeTrace();};
  worker_hooks.now=[] {return Now();};
  worker_hooks.context=[this](int context){return contexts_->RequestContextForContext(context);};
  worker_hooks.event=[this](int context,Dict event){auto record=CefDictionaryValue::Create();record->SetInt("contextId",context);record->SetDictionary("workerEvent",event);const auto json=Json(record);writer_.Send(Header("contextworker"),std::vector<uint8_t>(json.begin(),json.end()));};
  worker_hooks.console=[this](int context,Dict info){if(!context_console_->ObserveWorker(context,info))writer_.Error("Native context worker console journal rejected payload");};
  worker_hooks.predicate=[this](int context,Dict snapshot){auto record=CefDictionaryValue::Create();record->SetInt("contextId",context);record=snapshot->Copy(false);record->SetInt("contextId",context);auto typed=CefValue::Create();typed->SetDictionary(record->Copy(false));std::string json;if(!suitecut::DevToolsLosslessJSON(typed,json,1048576)){writer_.Error("Native context console predicate output limit exceeded");return;}writer_.Send(Header("contextworkerpredicate"),std::vector<uint8_t>(json.begin(),json.end()));};
  worker_hooks.error=[](int context,const std::string& error){std::fprintf(stderr,"Native context worker broker %d: %s\n",context,error.c_str());};
  worker_hooks.idle=[this](int context){contexts_->ContinueCloseContext(context);Idle();};
  worker_hooks.inspector_work=inspector_work_;
  workers_=std::make_shared<SuiteCutContextWorkers>(std::move(worker_hooks));
  utilities_ = std::make_shared<SuiteCutUtilityBrowsers>(SuiteCutUtilityBrowsers::HTTPObserver{}, [this] { Idle(); },inspector_work_);
  contexts_->SetUtilities(utilities_);
  SuiteCutHTTPControls::Hooks http_hooks;
  http_hooks.page_context = [this](int page) { return SuiteCutHTTPControls::BrowserContext{contexts_->RequestContext(page), contexts_->ContextId(page)}; };
  http_hooks.browser_context = [this](int context) { return SuiteCutHTTPControls::BrowserContext{contexts_->RequestContextForContext(context), context}; };
  http_hooks.create_context=[this](std::function<void(SuiteCutHTTPControls::BrowserContext,const std::string&)>done){contexts_->CreateInternal([this,done=std::move(done)](bool ok,Dict result,const std::string&error){const int id=ok&&result?result->GetInt("contextId"):0;done({id?contexts_->RequestContextForContext(id):nullptr,id},error);});};
  http_hooks.close_context=[this](int context,SuiteCutHTTPControls::Reply reply){auto params=CefDictionaryValue::Create();params->SetInt("contextId",context);contexts_->Handle("close",params,std::move(reply));};
  http_hooks.context_command=[this](int owner,int context,const std::string&action,Dict params,SuiteCutHTTPControls::Reply reply){params->SetInt("contextId",context);params->SetInt("internalOwner",owner);if(action=="httpCookieCommit")contexts_->HTTPCommitCookies(owner,context,params,std::move(reply));else contexts_->Handle(action,params,std::move(reply));};
  http_hooks.match_cookies=[this](int owner,int context,Dict filter,CefRefPtr<CefListValue>cookies,double deadline,SuiteCutHTTPControls::Reply reply){contexts_->MatchHTTPCookies(owner,context,filter,cookies,deadline,std::move(reply));};
  http_hooks.cancel_context_command=[this](int owner){contexts_->CancelWait(owner);};
  http_hooks.physical_owner=[this](int owner){return contexts_->PhysicalOwner(owner);};
  http_hooks.settled_context=[this](int context){contexts_->ContinueCloseContext(context);Idle();};
  http_hooks.default_user_agent=[this]{return http_user_agent_;};
  http_hooks.context_headers=[this](int context){return network_headers_.Snapshot(context);};
  http_hooks.context_policy=[this](int context){
    const auto config=settings_.Get(context);if(!config)return Dict{};auto params=CefDictionaryValue::Create();
    if(config->user_agent)params->SetString("userAgent",*config->user_agent);
    if(config->ignore_https_errors)params->SetBool("ignoreHTTPSErrors",*config->ignore_https_errors);
    auto current=*config;const auto snapshot=authentication_.Get(context);const auto credentials=snapshot?snapshot->Get():nullptr;current.http_credentials_all=credentials?*credentials:suitecut::NativeContextCredentials{};current.http_credentials.reset();current.WriteHTTPCredentials(params);
    if(config->proxy){const auto&proxy=*config->proxy;auto value=CefDictionaryValue::Create();value->SetString("server",proxy.server);auto bypass=proxy.bypass;const std::string loopback="<-loopback>";if(bypass.starts_with(loopback))bypass.erase(0,loopback.size());while(!bypass.empty()&&bypass.front()==';')bypass.erase(0,1);for(auto&item:bypass)if(item==';')item=',';value->SetString("bypass",bypass);if(proxy.authentication){value->SetString("username",proxy.username);value->SetString("password",proxy.password);}params->SetDictionary("proxy",value);}
    return params;
  };
  http_hooks.lookup_event=[this](const suitecut::HTTPLookupControls::Event&event){
    auto info=CefDictionaryValue::Create();info->SetString("event",event.canceled?"canceled":"request");info->SetInt("owner",event.owner);info->SetInt("apiContextId",event.context);info->SetDouble("generation",static_cast<double>(event.generation));info->SetString("lookupKey",event.key);info->SetString("hostname",event.hostname);info->SetInt("port",event.port);const auto json=Json(info);writer_.Send(Header("httplookup"),std::vector<uint8_t>(json.begin(),json.end()));
  };

  http_hooks.now = [] { return Now(); };
  http_hooks.wake = [this] {
    if (!deadline_timer_active_) { deadline_timer_active_ = true; CefPostDelayedTask(TID_UI, new Task([this, alive = alive_] { if (*alive) Deadlines(); },suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()), 10); }
  };
  http_ = std::make_shared<SuiteCutHTTPControls>(std::move(http_hooks));
  suitecut::DownloadControls::Hooks download_hooks;
  download_hooks.policy=[this](int page){
    int owner=page;const int opener=contexts_->PopupOpener(page);const auto source=sources_.find(page);
    if(opener&&source!=sources_.end()&&source->second->DownloadProvisionalPopup())owner=opener;
    return suitecut::DownloadControls::Policy{contexts_->ContextId(page),contexts_->AcceptDownloads(page),owner};
  };
  download_hooks.event=[this](int page,Dict info){const auto source=sources_.find(page);if(source!=sources_.end())source->second->DownloadPublish(info);};
  download_hooks.navigation_response=[this](int page){const auto source=sources_.find(page);if(source!=sources_.end())source->second->DownloadNavigationChanged();};
  download_hooks.now=[] {return Now();};
  download_hooks.wake=[this]{if(!deadline_timer_active_){deadline_timer_active_=true;CefPostDelayedTask(TID_UI,new Task([this,alive=alive_]{if(*alive)Deadlines();},suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()),10);}};
  download_hooks.settled_context=[this](int context){contexts_->ContinueCloseContext(context);};
  download_hooks.close_provisional_page=[this](int page){
    const auto source=sources_.find(page);
    if(contexts_->PopupOpener(page)&&source!=sources_.end()&&source->second->DownloadProvisionalPopup()){
      auto params=CefDictionaryValue::Create();params->SetInt("pageId",page);contexts_->Handle("closePage",params,[](bool,Dict,const std::string&){});
    }
  };
  const auto download_command=CefCommandLine::GetGlobalCommandLine();
  download_hooks.directory=download_command->GetSwitchValue(download_command->HasSwitch("suitecut-downloads-path")?"suitecut-downloads-path":"suitecut-profile").ToString();
  downloads_=std::make_shared<suitecut::DownloadControls>(std::move(download_hooks));
  suitecut::TraceControls::Hooks trace_hooks;trace_hooks.action_png=true;trace_hooks.aria=true;trace_hooks.now=[]{return Now();};trace_hooks.wake=[this]{WakeTrace();};trace_hooks.settled_context=[this](int context){contexts_->ContinueCloseContext(context);Idle();};
  trace_hooks.background=[this](std::function<void()> work){return trace_background_.Submit(std::move(work));};trace_hooks.post=[](std::function<void()> work){return CefPostTask(TID_UI,new Task(std::move(work)));};
  trace_hooks.raw_directory=CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-traces-dir").ToString();
  trace_hooks.context_options=[this](int context)->Dict{if(!contexts_->RequestContextForContext(context))return nullptr;auto info=CefDictionaryValue::Create();info->SetInt("version",1);info->SetString("origin","suitecut-native");info->SetString("browserName","chromium");info->SetString("cefVersion",CEF_VERSION);info->SetInt("contextId",context);info->SetDouble("monotonicTime",Now());info->SetDouble("wallTime",std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count());auto options=CefDictionaryValue::Create();info->SetDictionary("options",options);return info;};
  trace_hooks.pending_capture=[this](int context){return TracePending(context);};
  trace_hooks.begin_session=[this](suitecut::TraceJournal::Lease lease,bool initial,std::string& error){const auto options=traces_->Configuration(lease.context);if(!options||!options->dom)return true;const bool begun=network_archive_->BeginTrace(lease.context,lease.session)||(!initial&&network_archive_->ResumeTrace(lease.context,lease.session));if(!begun)error="Native trace network consumer is unavailable";return begun;};
  trace_hooks.seal_session=[this](suitecut::TraceJournal::Lease lease){network_archive_->SealTrace(lease.context,lease.session);};
  trace_hooks.end_session=[this](suitecut::TraceJournal::Lease lease){network_archive_->EndTrace(lease.context,lease.session);if(trace_sources_)trace_sources_->Closed(lease.context);};
  trace_hooks.source=[this](suitecut::TraceJournal::Lease lease,const std::string& path){if(!trace_sources_->Capture(lease,path))traces_->CaptureError(lease,"Native trace source capture capacity exceeded");};
  trace_hooks.request_capture=[this](int context){const auto lease=traces_->CaptureLease(context);for(const auto& [page,source]:sources_)if(contexts_->ContextId(page)==context&&source->browser_&&!source->closing_page_){source->NativeTraceDOM(lease,0,"initial",Now()+5000);source->browser_->GetHost()->Invalidate(PET_VIEW);source->WakeDeadlines();}};
  traces_=std::make_shared<suitecut::TraceControls>(std::move(trace_hooks));
  network_archive_->SetReservation([this](int context){suitecut::NativeNetworkArchives::Publication publication;const auto lease=traces_->SessionLease(context);const auto options=traces_->Configuration(context);if(!lease.context||!options||!options->dom)return publication;
    publication.snapshot=[traces=traces_,lease](Dict event){auto produced=event->Copy(false);auto snapshot=produced->GetDictionary("snapshot");auto response=snapshot?snapshot->GetDictionary("response"):nullptr;auto content=response?response->GetDictionary("content"):nullptr;if(content){if(content->GetType("_sha1")==VTYPE_STRING)content->SetString("_file","resources/"+content->GetString("_sha1").ToString());content->Remove("text");content->Remove("encoding");}traces->Network(lease,produced);};
    publication.resource=[traces=traces_,lease](const std::string& path,const std::vector<uint8_t>& bytes){traces->Resource(lease,path,bytes);};
    publication.capture_body=[](Dict request,Dict response){auto type=request->GetString("resourceType").ToString();std::transform(type.begin(),type.end(),type.begin(),[](unsigned char c){return char(std::tolower(c));});if(type=="script")return false;auto headers=response->GetDictionary("headers");if(headers){CefDictionaryValue::KeyList keys;headers->GetKeys(keys);for(const auto& key:keys){auto name=key.ToString();std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});if(name=="content-type"){auto value=headers->GetString(key).ToString();std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});if(value.find("javascript")!=std::string::npos||value.find("ecmascript")!=std::string::npos)return false;}}}return true;};
    return publication;
  });
  const auto background=[this](std::function<void()> work){return trace_background_.Submit(std::move(work));};const auto owner=[](std::function<void()> work){return CefPostTask(TID_UI,new Task(std::move(work)));};
  suitecut::ScreenshotComparisons::Hooks screenshot_hooks;screenshot_hooks.now=[]{return Now();};screenshot_hooks.background=background;screenshot_hooks.post=owner;screenshot_hooks.wake=[this,alive=alive_]{if(*alive){WakeTrace();Idle();}};screenshot_hooks.available=[this,alive=alive_]{return *alive&&(!screenshot_inputs_||screenshot_inputs_->AdmissionCount()+screenshot_comparisons_->LogicalCount()<16);};screenshot_hooks.physical_available=[this,alive=alive_]{return *alive&&(!screenshot_inputs_||screenshot_comparisons_->PhysicalCount()+screenshot_inputs_->PhysicalCount()<2);};screenshot_hooks.settled_context=[this,alive=alive_](int context){if(*alive)contexts_->ContinueCloseContext(context);};screenshot_comparisons_=std::make_shared<suitecut::ScreenshotComparisons>(std::move(screenshot_hooks));
  suitecut::ScreenshotInputs::Hooks input_hooks;input_hooks.now=[]{return Now();};input_hooks.background=background;input_hooks.post=owner;input_hooks.wake=[this,alive=alive_]{if(*alive){WakeTrace();Idle();}};input_hooks.settled=[this,alive=alive_](int context){if(*alive)contexts_->ContinueCloseContext(context);};input_hooks.available=[this,alive=alive_]{return *alive&&screenshot_inputs_->AdmissionCount()+screenshot_comparisons_->LogicalCount()<16;};input_hooks.physical_available=[this,alive=alive_]{return *alive&&screenshot_inputs_->PhysicalCount()+screenshot_comparisons_->PhysicalCount()<2;};screenshot_inputs_=std::make_shared<suitecut::ScreenshotInputs>(screenshot_comparisons_->Budget(),std::move(input_hooks));
  suitecut::TracePreview::Hooks preview_hooks;preview_hooks.background=background;preview_hooks.owner=owner;preview_hooks.wake=[this]{WakeTrace();Idle();};preview_hooks.error=[this](suitecut::TraceJournal::Lease lease,const std::string& error){traces_->CaptureError(lease,error);};preview_hooks.captured=[this](suitecut::TraceJournal::Lease lease,Dict event,const std::string& file,std::vector<uint8_t> bytes){traces_->Captured(lease,event,file,std::move(bytes));};trace_preview_=std::make_shared<suitecut::TracePreview>(std::move(preview_hooks));
  suitecut::TraceSources::Hooks source_hooks;source_hooks.background=background;source_hooks.owner=owner;source_hooks.wake=[this]{WakeTrace();Idle();};source_hooks.error=[this](suitecut::TraceJournal::Lease lease,const std::string& error){traces_->CaptureError(lease,error);};source_hooks.captured=[this](suitecut::TraceJournal::Lease lease,const std::string& file,std::vector<uint8_t> bytes){traces_->SourceResource(lease,file,std::move(bytes));};trace_sources_=std::make_shared<suitecut::TraceSources>(std::move(source_hooks));
  context_har_=std::make_unique<suitecut::NativeContextHARRoutes>([this](const std::string&owner,const std::string&error){HARPolicyFailure(owner,error);},[]{return Now();});
  context_unroute_all_=std::make_unique<suitecut::NativeContextUnrouteAll>(*context_routes_,*context_har_,[]{return Now();});
  suitecut::NativeHARRecording::Hooks har_hooks;har_hooks.background=background;har_hooks.owner=owner;har_hooks.now=[]{return Now();};har_hooks.available=[this]{return trace_background_.Available();};har_hooks.wake=[this]{WakeTrace();Idle();};har_hooks.settled=[this](int context){contexts_->ContinueCloseContext(context);Idle();};har_recording_=std::make_shared<suitecut::NativeHARRecording>(*network_archive_,std::move(har_hooks));
  suitecut::NativeContextVideo::Hooks video_hooks;
  video_hooks.background=background;video_hooks.owner=owner;video_hooks.now=[]{return Now();};
  video_hooks.wake=[this]{WakeTrace();Idle();};
  video_hooks.settled=[this](int context){contexts_->ContinueCloseContext(context);Idle();};
  video_hooks.start=[this](int page,int context,suitecut::MediaPipelineOptions options){return recordings_->StartAutomatic(page,context,std::move(options));};
  context_video_=std::make_shared<suitecut::NativeContextVideo>(std::move(video_hooks));
  suitecut::NativeHARReplayFiles::Hooks replay_hooks;replay_hooks.background=background;replay_hooks.owner=owner;replay_hooks.now=[]{return Now();};replay_hooks.wake=[this]{WakeTrace();Idle();};har_files_=std::make_shared<suitecut::NativeHARReplayFiles>(std::move(replay_hooks));
  suitecut::HTTPTraceControls::Hooks api_trace_hooks;api_trace_hooks.raw_directory=CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-traces-dir").ToString();api_trace_hooks.now=[] {return Now();};api_trace_hooks.context_exists=[this](int context){return http_->ContextExists(context);};api_trace_hooks.background=background;api_trace_hooks.owner=owner;api_trace_hooks.wake=[this]{WakeTrace();Idle();};http_->SetTracing(std::make_shared<suitecut::HTTPTraceControls>(std::move(api_trace_hooks)));
}
void Engine::AuthenticationFatal(int context,const std::string&error){
  if(!authentication_.Get(context))return;
  authentication_.Closing(context);
  auto&failure=authentication_failures_[context];
  if(!failure){
    auto reason=std::make_shared<std::string>();
    failure=std::make_shared<suitecut::NativeNetworkFatalTeardown>(
      [alive=alive_](std::function<void()>task){return CefPostTask(TID_UI,new Task([alive,task=std::move(task)]{if(*alive)task();}));},
      [reason](const std::string&error){*reason=error;},
      [this,alive=alive_,context,reason]{if(!*alive)return;auto close=CefDictionaryValue::Create();close->SetInt("contextId",context);close->SetString("reason",*reason);contexts_->Handle("close",close,[](bool,Dict,const std::string&){});});
  }
  failure->Fail(error);WakeTrace();
}
void Engine::HARPolicyFailure(const std::string&owner,const std::string&error){for(const auto&[page,source]:sources_)if(owner=="page:"+std::to_string(page)){source->NetworkFatal(error);return;}if(!workers_->NetworkPolicyFailure(owner,error)){writer_.Error(error.c_str());CloseAll();}}
std::vector<CefRefPtr<Source>> Engine::OwnerFrameSources(int page) const {
 std::vector<CefRefPtr<Source>> result;const int context=contexts_->ContextId(page);
 for(const auto&[id,source]:sources_)if(contexts_->ContextId(id)==context&&source->browser_&&!source->closing_page_)result.push_back(source);
 return result;
}
bool Engine::TracePending(int context) const{const auto session=traces_->SessionLease(context);if(session.context&&network_archive_->pending(context,session.session))return true;if(trace_preview_->HasPendingForContext(context)||trace_sources_->Pending(context))return true;for(const auto& [page,source]:sources_)if(contexts_->ContextId(page)==context&&(!source->native_trace_protocol_.empty()||(source->native_trace_phases_&&source->native_trace_phases_->Pending(context))))return true;return false;}
void Engine::TracePaint(int page,int width,int height,double now,const void* pixels,int pixel_width,int pixel_height){const int context=contexts_->ContextId(page);const auto lease=traces_->CaptureLease(context);const auto config=traces_->Configuration(context);if(lease.context&&config&&config->screenshots)trace_preview_->Paint(lease,page,width,height,now,pixels,pixel_width,pixel_height);}
void Engine::WakeTrace(){if(!deadline_timer_active_){deadline_timer_active_=true;CefPostDelayedTask(TID_UI,new Task([this,alive=alive_]{if(*alive)Deadlines();},locator_handler_leases_.empty()?suitecut::AcceptedTaskWork::Kind::Work:suitecut::AcceptedTaskWork::Kind::BarrierCoordinator,LocatorHandlerMonitorOrigin()),10);}}
Engine::~Engine() {
  *alive_ = false;
#if defined(__linux__)
  if(native_work_wake_)native_work_wake_->Stop();
#endif
  for(const auto&[context,failure]:authentication_failures_){(void)context;failure->Closed();}authentication_failures_.clear();
  context_unroute_all_->Clear();context_routes_->Clear();context_har_->Clear();har_files_->Clear();har_recording_->Clear();
  context_video_->Clear();video_preparations_.clear();
  screenshot_inputs_->Clear();
  traces_->Clear();trace_preview_->Clear();trace_sources_->Clear();
  workers_->Clear();
  context_console_->Clear();context_network_->Clear();worker_network_->Clear();network_archive_->Clear();network_policy_.Clear();network_headers_.Clear();authentication_.Clear();
  if (http_) http_->Clear();
  if(downloads_)downloads_->Clear();
  if (reader_source_) reader_source_->StopReader();
  sources_.clear(); reader_source_ = nullptr; contexts_.reset();
}
#if defined(OS_LINUX)
bool Engine::SelectStreamingSource(const std::string& stream,int page,std::string& error) {
  const auto found=sources_.find(page);
  if(found==sources_.end()||found->second->closing_page_||!found->second->browser_||!found->second->ready_){error="Native stream source is not an active page in this Engine";return false;}
  auto source=found->second;
  try {
    streaming_->Select(stream,page,contexts_->ContextId(page),source->capture_width_,source->capture_height_,source->i420_);
    source->EmitFrame();source->browser_->GetHost()->Invalidate(PET_VIEW);return true;
  } catch(const std::exception& failure){error=std::string(failure.what()).substr(0,4096);return false;}
}
#endif
bool Engine::Initialize(Dict launch_video,double startup_deadline) {
  const auto context = CefRequestContext::GetGlobalContext();
  if(!contexts_->InitializeDefault(context))return false;
  if(!launch_video)return CreatePage(1,1,context);
  // A real startup operation exists before any SDK reader or public page. It
  // shares no client IDs or completion packets with the later command registry.
  const double now=Now();
  if(startup_deadline<=now){StartupFailed("Native launch recording startup deadline expired");return true;}
  const auto operation=startup_operations_.Begin(1,now,startup_deadline-now);
  if(!operation){StartupFailed("Native launch recording owner admission failed");return true;}
  const auto generation=operation->generation;
  context_video_->Prepare(1,generation,launch_video,startup_deadline,
    [this,alive=alive_,context,generation](suitecut::NativeContextVideo::Prepared prepared,const std::string& error){
      if(!*alive)return;
      if(closing_||!startup_operations_.IsCurrent(1,generation)||startup_operations_.IsExpired(1,generation,Now())){
        context_video_->Discard(std::move(prepared));
        if(!closing_)StartupFailed("Native launch recording preparation exceeded its original deadline");
        return;
      }
      if(!error.empty()){StartupFailed(error);return;}
      std::string failure;
      if(!context_video_->Configure(1,prepared,failure)){
        context_video_->Discard(std::move(prepared));StartupFailed(failure);return;
      }
      if(!CreatePage(1,1,context))StartupFailed("Native launch recording page creation failed");
    });
  // Rejections are delivered by Prepare as well. Its actual work retirement,
  // and every error close, remain owned by the normal engine shutdown barrier.
  WakeTrace();return true;
}
void Engine::StartupFailed(const std::string& error){
  const auto operation=startup_operations_.Get(1);
  if(operation)context_video_->Cancel(1,operation->generation);
  startup_operations_.Clear();writer_.Error(error.c_str());
  // Before CreateBrowser admission, the reserved default page has no CEF
  // callback that could retire it. Do not leave shutdown waiting on that slot.
  if(!sources_.contains(1))contexts_->PageCreationFailed(1,error);
  CloseAll();
}
bool Engine::StartupReady(int page){
  if(page!=1||!startup_operations_.Contains(1))return !closing_;
  const auto operation=startup_operations_.Get(1);
  if(startup_operations_.IsExpired(1,operation->generation,Now())){
    StartupFailed("Native launch recording page startup exceeded its original deadline");return false;
  }
  startup_operations_.Finish(1);return !closing_;
}
bool Engine::CreatePage(int page, int, CefRefPtr<CefRequestContext> context) {
  if (closing_) return false;
  const auto config=settings_.Get(contexts_->ContextId(page));const auto view=config&&config->viewport?*config->viewport:suitecut::ContextSettings::Size{width_,height_};const double scale=config&&config->device_scale?*config->device_scale:scale_;
  CefRefPtr<Source> source = new Source(writer_, *this, page, view.width, view.height, scale, i420_);
  sources_[page] = source;
  if (page == 1) reader_source_ = source;
  CefWindowInfo window; window.SetAsWindowless(0);
  CefBrowserSettings settings; settings.windowless_frame_rate = fps_;
  settings.background_color = CefColorSetARGB(255, 0, 0, 0);
  if (CefBrowserHost::CreateBrowser(window, source, "about:blank", settings, BrowserInfo(page), context)) return true;
  sources_.erase(page); return false;
}
CefRefPtr<Source> Engine::PopupClient(int opener, int popup_id, const std::string& initial_url) {
  const int page = contexts_->ReservePopup(opener, popup_id, initial_url);
  if (!page) return nullptr;
  const auto parent = sources_.find(opener);
  const int width = parent == sources_.end() ? width_ : parent->second->width_;
  const int height = parent == sources_.end() ? height_ : parent->second->height_;
  const auto config=settings_.Get(contexts_->ContextId(page));const double scale=config&&config->device_scale?*config->device_scale:scale_;
  CefRefPtr<Source> source = new Source(writer_, *this, page, width, height, scale, i420_);
  sources_[page] = source; return source;
}
bool Engine::PageCreated(int page,CefRefPtr<CefBrowser> browser){
  const int context=contexts_->ContextId(page);const auto found=sources_.find(page);
  if(!context||found==sources_.end()){browser->GetHost()->CloseBrowser(true);return false;}
  std::optional<suitecut::NativeContextVideo::Descriptor> video;
  if(context_video_->Configured(context)){
    suitecut::MediaPipelineOptions actual;
    actual.launcher_path=CefCommandLine::GetGlobalCommandLine()->GetProgram().ToString();
    actual.source_width=found->second->capture_width_;actual.source_height=found->second->capture_height_;
    actual.output_width=found->second->width_;actual.output_height=found->second->height_;
    actual.i420=found->second->i420_;actual.frames_per_second=fps_;actual.start_timestamp_ms=Now();
    const auto startup=page==1?startup_operations_.Get(1):std::nullopt;
    const double deadline=startup?startup->deadline_ms:contexts_->PageCreationDeadline(page);
    actual.first_frame_deadline=Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(deadline?std::max(0.0,deadline-Now()):30000));
    std::string error;
    if(!context_video_->PageCreated(page,context,std::move(actual),video,error)){
      contexts_->PageCreationFailed(page,error);
      if(page==1)StartupFailed(error);
      browser->GetHost()->CloseBrowser(true);return false;
    }
  }
  // Admission precedes both the popup event and a real newPage reply. Encoder
  // startup is asynchronous and follows actual OSR paint, never a fake frame.
  contexts_->PageCreated(page,browser);workers_->Ensure(context);
  if(video)browser->GetHost()->Invalidate(PET_VIEW);
  return true;
}
void Engine::PageClosed(int page) { websocket_routes_->PageClosed(contexts_->ContextId(page),page);worker_network_->Detach("page:"+std::to_string(page));network_archive_->Detach("page:"+std::to_string(page)); trace_preview_->PageClosed(page);
#if defined(OS_LINUX)
  streaming_->PageGone(page);
#endif
  context_routes_->Detach("page:"+std::to_string(page));context_har_->Detach("page:"+std::to_string(page));network_headers_.Detach("page:"+std::to_string(page));authentication_.Detach("page:"+std::to_string(page));network_policy_.Detach("page:"+std::to_string(page)); downloads_->PageGone(page); writer_.DropPage(page); const auto closed_source=sources_.find(page);contexts_->PageClosed(page,closed_source==sources_.end()?std::string{}:closed_source->second->closed_reason_); sources_.erase(page); }
void Engine::AbortPopup(int opener, int popup_id) {
  contexts_->AbortPopup(opener, popup_id);
  for (auto iterator = sources_.begin(); iterator != sources_.end();) {
    if (!contexts_->ContextId(iterator->first) && iterator->first != 1) iterator = sources_.erase(iterator); else ++iterator;
  }
}
void Engine::CompleteContext(int id, bool ok, Dict result, const std::string& error, bool lossless_console) {
  const auto handler_owner=operations_.Get(id);
  const suitecut::NativeWorkOriginFrame handler_origin(LocatorHandlerCommandOrigin(id,0,handler_owner?handler_owner->generation:0));
  if (!operations_.Finish(id)) return;
  if(context_video_)context_video_->Cancel(id,handler_owner->generation);
  if(const auto prepared=video_preparations_.find(id);prepared!=video_preparations_.end()){auto value=std::move(prepared->second.value);video_preparations_.erase(prepared);context_video_->Discard(std::move(value));}
  ForgetLocatorHandlerCommand(id);
  if(const auto found=trace_context_api_.find(id);found!=trace_context_api_.end()){const auto [lease,call]=found->second;trace_context_api_.erase(found);auto event=CefDictionaryValue::Create();event->SetString("type","after");event->SetString("callId","call@"+std::to_string(call));event->SetDouble("endTime",Now());if(!ok){auto failure=CefDictionaryValue::Create();failure->SetString("message",error);event->SetDictionary("error",failure);}traces_->After(lease.context,call,event);}
  traces_->Cancel(id);
  screenshot_inputs_->CancelWait(id);
  contexts_->CancelWait(id);
  workers_->Cancel(id);
  har_files_->Cancel(id);context_har_->Cancel(id);
  websocket_routes_->Cancel(id);context_unroute_all_->Cancel(id);context_routes_->Cancel(id);context_console_->Cancel(id);context_network_->Cancel(id);worker_network_->CancelOwner(id);network_policy_.Cancel(id);geolocation_.Cancel(id);authentication_.Cancel(id);network_headers_.Cancel(id);
  http_->Cancel(id);
  downloads_->Cancel(id);
  std::string json = result ? Json(result) : "";
  if(result&&lossless_console){auto typed=CefValue::Create();typed->SetDictionary(result->Copy(false));json.clear();if(!suitecut::DevToolsLosslessJSON(typed,json,kMaxResponseBytes)){writer_.Reply(id,false,"Native context console output limit exceeded");return;}}
  writer_.Reply(id, ok, error, std::vector<uint8_t>(json.begin(), json.end()));
}
void Engine::ContextCommand(int id,uint64_t generation,const std::string& action,Dict params){
  contexts_->Handle(action,params,[this,alive=alive_,id,generation,action](bool ok,Dict result,const std::string& error){
    if(!*alive)return;
    if(!operations_.IsCurrent(id,generation)){
      // Native page creation can settle after the original caller was canceled.
      if(ok&&(action=="newPage"||action=="newOwnedPage")&&result){auto close=CefDictionaryValue::Create();close->SetInt("pageId",result->GetInt("pageId"));contexts_->Handle("closePage",close,[](bool,Dict,const std::string&){});}
      return;
    }
    CompleteContext(id,ok,result,error);
  });
  // A validation rejection can happen before context_created consumes its token.
  const auto unused=video_preparations_.find(id);
  if(unused!=video_preparations_.end()&&unused->second.generation==generation){auto prepared=std::move(unused->second.value);video_preparations_.erase(unused);context_video_->Discard(std::move(prepared));}
}
#include "screenshot_input_engine.inc"
#include "locator_handler_engine.inc"
void Engine::Route(const std::string& line) {
  auto parsed = CefParseJSON(line, JSON_PARSER_RFC); auto message = parsed ? parsed->GetDictionary() : nullptr;
  if (!message || message->GetInt("version") != 1 || message->GetInt("id") < 1) {
    writer_.ReleaseReply(); writer_.Error("Invalid native command"); return;
  }
  const int id = message->GetInt("id"); const auto command = message->GetString("command").ToString();
  if(LocatorHandlerControl(id,command,message))return;
  double locator_handler_deadline=0;
  if(message->HasKey("locatorHandlerOrigin")&&!LocatorHandlerAdmission(message,locator_handler_deadline)){writer_.Reply(id,false,"Native locator handler child admission is stale or expired");return;}
  if(message->HasKey("locatorHandlerOrigin")&&!suitecut::NativeLocatorHandlerCallbackCommandAllowed(command,message->GetString("action").ToString())){writer_.Reply(id,false,"This native browser command is not supported inside a locator handler callback");return;}
  if(message->HasKey("locatorHandlerOrigin")&&command=="input"&&message->GetString("backend")=="cef"){writer_.Reply(id,false,"Direct CEF input is not supported inside a locator handler callback");return;}
  auto handler_native_origin=suitecut::CurrentNativeWorkOrigin();
  if(message->HasKey("locatorHandlerOrigin")){uint64_t token=0;Source::ActionHookInteger(message->GetDictionary("locatorHandlerOrigin"),"token",token);handler_native_origin.locator_handler_invocation=token;}
  if(handler_native_origin.locator_handler_invocation&&!suitecut::BindCurrentNativeLocatorHandlerTask(handler_native_origin.locator_handler_invocation)){writer_.Reply(id,false,"Native locator handler task ownership is unavailable");return;}
  const suitecut::NativeWorkOriginFrame handler_origin(handler_native_origin);
  if (closing_) { writer_.Reply(id, false, "Native browser is closing"); return; }
  bool duplicate = operations_.Contains(id);
#if defined(__linux__)
  duplicate=duplicate||(native_work_&&native_work_->command==id&&!native_work_->granted&&!native_work_->refused);
#endif
  for (const auto& [page, source] : sources_) duplicate = duplicate || source->operations_.Contains(id);
  if (duplicate) { writer_.ReleaseReply(); writer_.Error("Duplicate active native command ID"); return; }
  if (command == "viewerIdle") {
    // A viewer handover observes the existing physical-work predicate without
    // adding an operation owner of its own. The original reader Work task must
    // return before this coordinator can truthfully inspect task retirement.
    if (!CefPostTask(TID_UI, new Task([this, alive = alive_, id] {
      if (!*alive) return;
      auto result = CefDictionaryValue::Create();
      result->SetInt("viewerIdleVersion", 1);
      result->SetBool("idle", AcceptedWorkReady(true));
      const auto json = Json(result);
      writer_.Reply(id, true, "", std::vector<uint8_t>(json.begin(), json.end()));
    }, suitecut::AcceptedTaskWork::Kind::BarrierCoordinator)))
      writer_.Reply(id, false, "Could not schedule native viewer idle probe");
    return;
  }
#if defined(__linux__)
  if(command=="nativeWorkBarrier"){OriginalAcceptedWorkBarrier(id,message);return;}
#endif
  if (command == "cancel" && operations_.Contains(message->GetInt("target"))) {
    CompleteContext(message->GetInt("target"), false, nullptr, "Native context operation canceled");
    writer_.Reply(id, true); return;
  }
  if (command == "selectors") {
    const auto action = message->GetString("action").ToString();
    if (action == "diagnostics") {
      auto result = CefDictionaryValue::Create(); result->SetInt("engines",static_cast<int>(selectors_.entries().size()));
      result->SetInt("sourceBytes",static_cast<int>(selectors_.bytes()));
      const auto json = Json(result); writer_.Reply(id,true,"",std::vector<uint8_t>(json.begin(),json.end())); return;
    }
    if (action != "register" || message->GetType("name") != VTYPE_STRING || message->GetType("source") != VTYPE_STRING ||
        (message->HasKey("contentScript") && message->GetType("contentScript") != VTYPE_BOOL)) {
      writer_.Reply(id,false,"Invalid native selector registration"); return;
    }
    std::string error;
    const bool ok = selectors_.Register(message->GetString("name"),message->GetString("source"),message->GetBool("contentScript"),error);
    writer_.Reply(id,ok,error); return;
  }
  if (command == "context" || command == "http") {
    double timeout = 30000;
    if (message->HasKey("timeoutMs")) {
      if (message->GetType("timeoutMs") != VTYPE_INT || message->GetInt("timeoutMs") < 0 || message->GetInt("timeoutMs") > 120000) { writer_.Reply(id, false, "Invalid native timeout"); return; }
      timeout = message->GetInt("timeoutMs");
    }
    const auto context_action = message->GetString("action").ToString();
    const bool cleanup = (command == "http" && (context_action == "dispose" || context_action == "resolveLookup")) || (command == "context" && (context_action == "close" || context_action == "closePage" || context_action == "workerNetworkBody" || context_action == "screenshotInput"));
    auto operation = cleanup ? operations_.BeginControl(id, Now(), timeout) : operations_.Begin(id, Now(), timeout);
    if (!operation) { writer_.Reply(id, false, "Too many pending native context operations or duplicate ID"); return; }
    if(locator_handler_deadline&&(!operation->deadline_ms||operation->deadline_ms>locator_handler_deadline)&&!operations_.BindOriginalDeadline(id,operation->generation,locator_handler_deadline)){CompleteContext(id,false,nullptr,"Native locator handler child original deadline is invalid");return;}
    if(!TrackLocatorHandlerCommand(id,0,operation->generation)){CompleteContext(id,false,nullptr,"Native locator handler command capacity exceeded");return;}
    operation=operations_.Get(id);
    if (!deadline_timer_active_) { deadline_timer_active_ = true; CefPostDelayedTask(TID_UI, new Task([this, alive = alive_] { if (*alive) Deadlines(); },suitecut::AcceptedTaskWork::Kind::Work,LocatorHandlerMonitorOrigin()), 10); }
    if (command == "http") {
      http_->Handle(id, message, [this, alive = alive_, id, generation = operation->generation](bool ok, Dict result, const std::string& error) {
        if (*alive && operations_.IsCurrent(id, generation)) CompleteContext(id, ok, result, error);
      },operations_.Get(id)->deadline_ms);
      return;
    }
    const auto action = message->GetString("action").ToString();
    auto params = message->GetDictionary("params");
    params = params ? params->Copy(false) : CefDictionaryValue::Create(); params->SetInt("internalOwner", id);
    if(action=="websocket"){
      const int context=params->GetInt("contextId");
      if(!contexts_->RequestContextForContext(context)){CompleteContext(id,false,nullptr,"Actual native WebSocket context is closed");return;}
      const auto original_deadline=operation->deadline_ms;
      auto handle=[this,alive=alive_,id,generation=operation->generation,context,params,original_deadline](bool ok,const std::string&error){
        if(!*alive||!operations_.IsCurrent(id,generation))return;
        if(!ok){CompleteContext(id,false,nullptr,error);return;}
        websocket_routes_->Command(id,context,0,params,original_deadline,[this,alive,id,generation](bool accepted,Dict result,const std::string&reason){if(*alive&&operations_.IsCurrent(id,generation)){if(accepted&&operations_.IsExpired(id,generation,Now()))CompleteContext(id,false,nullptr,"Native WebSocket policy original command deadline exceeded");else CompleteContext(id,accepted,result,reason);}});
      };
      workers_->WebSocketPolicyReady(context,std::move(handle));return;
    }
    if(action=="video"){
      context_video_->Handle(id,operation->generation,params->GetInt("contextId"),params,operation->deadline_ms,
        [this,alive=alive_,id,generation=operation->generation](bool ok,const std::string& path,const std::string& error){if(!*alive||!operations_.IsCurrent(id,generation))return;auto result=CefDictionaryValue::Create();if(!path.empty())result->SetString("path",path);CompleteContext(id,ok,result,error);});return;
    }
    if(action=="screenshotInput"){ScreenshotInputControl(id,params,operation->generation);return;}
    if(action=="download"){
      downloads_->Handle(id,params,operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,Dict result,const std::string& error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,result,error);});return;
    }
    if(action=="har"){
      const int context=params->GetInt("contextId");if(!contexts_->RequestContextForContext(context)){CompleteContext(id,false,nullptr,"Native HAR replay context is closed");return;}
      const auto method=params->GetString("action").ToString();
      if(method=="record"){std::string error;const bool ok=har_recording_->Configure(context,params->GetDictionary("options"),error);CompleteContext(id,ok,nullptr,error);return;}
      if(method!="replay"){CompleteContext(id,false,nullptr,"Invalid native context HAR action");return;}
      const auto registration=params->GetString("registrationId").ToString(),pattern=params->HasKey("url")?params->GetString("url").ToString():"**",not_found=params->HasKey("notFound")?params->GetString("notFound").ToString():"abort";
      if(registration.empty()||registration.size()>128||pattern.empty()||pattern.size()>8192||(not_found!="abort"&&not_found!="fallback")){CompleteContext(id,false,nullptr,"Invalid native context HAR route");return;}
      const auto admission_order=suitecut::NativeRouteRegistrationOrder::Reserve();if(!admission_order){CompleteContext(id,false,nullptr,"Native route registration order exhausted");return;}
      har_files_->Read(id,params->GetString("path"),operation->deadline_ms,[this,alive=alive_,id,context,generation=operation->generation,registration,pattern,not_found,admission_order=*admission_order](suitecut::NativeHARReplayFiles::Replay replay,const std::string&error){
        if(!*alive||!operations_.IsCurrent(id,generation))return;
        const auto operation=operations_.Get(id);if(!operation||operations_.IsExpired(id,generation,Now())){CompleteContext(id,false,nullptr,"Native HAR original context command deadline exceeded");return;}
        if(!contexts_->RequestContextForContext(context)){CompleteContext(id,false,nullptr,"Native HAR replay context closed during file read");return;}
        if(!replay){CompleteContext(id,false,nullptr,error);return;}
        if(!context_har_->Install(id,context,{registration,pattern,std::move(replay),not_found=="fallback",admission_order},operation->deadline_ms,[this,alive,id,generation](bool ok,const std::string&reason){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,nullptr,reason);}))CompleteContext(id,false,nullptr,"Invalid or excessive native context HAR policy");
      },not_found=="fallback");return;
    }
    if(action=="setExtraHTTPHeaders") {
      const auto context=params->GetInt("contextId");
      if(!contexts_->RequestContextForContext(context)||!network_headers_.Set(id,context,params->GetDictionary("headers"),operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,const std::string&error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,nullptr,error);}))CompleteContext(id,false,nullptr,"Invalid native context HTTP headers or capacity exceeded");
      return;
    }
    if(action=="setHTTPCredentials"){
      const int context=params->GetInt("contextId");const auto cef=contexts_->RequestContextForContext(context);auto config=CefDictionaryValue::Create();
      if(params->GetType("credentials")==VTYPE_NULL)config->SetList("httpCredentials",CefListValue::Create());
      else if(params->GetType("credentials")==VTYPE_DICTIONARY)config->SetDictionary("httpCredentials",params->GetDictionary("credentials")->Copy(false));
      else if(params->GetType("credentials")==VTYPE_LIST)config->SetList("httpCredentials",params->GetList("credentials")->Copy());
      else{CompleteContext(id,false,nullptr,"Native context HTTP credentials must be a credential, array or null");return;}
      suitecut::ContextSettings parsed;std::string error;if(!cef||!suitecut::ContextSettings::Parse(config,parsed,error)){CompleteContext(id,false,nullptr,error.empty()?"Native browser context is closed":error);return;}
      if(!authentication_.Set(id,context,std::move(parsed.http_credentials_all),operation->deadline_ms,
        [cef](suitecut::NativeContextAuthenticationControls::Result done){cef->ClearHttpAuthCredentials(new suitecut::NativeContextAuthenticationClear(cef,std::move(done)));},
        [this,alive=alive_,id,context,generation=operation->generation](bool ok,const std::string&error){
          if(!*alive)return;
          const bool expired=error==suitecut::NativeContextAuthenticationControls::DeadlineError();
          if(!ok&&!expired)AuthenticationFatal(context,error);
          if(operations_.IsCurrent(id,generation))CompleteContext(id,ok,nullptr,expired?"Native context operation timed out":error);
        }))CompleteContext(id,false,nullptr,"Native context HTTP credential update is pending or exceeds capacity");
      WakeTrace();return;
    }
    if(action=="setGeolocation"){
      const int context=params->GetInt("contextId");auto input=CefDictionaryValue::Create();
      if(params->GetType("geolocation")==VTYPE_NULL)input->SetNull("geolocation");
      else if(params->GetType("geolocation")==VTYPE_DICTIONARY)input->SetDictionary("geolocation",params->GetDictionary("geolocation")->Copy(false));
      else{CompleteContext(id,false,nullptr,"Native geolocation must be coordinates or null");return;}
      suitecut::ContextSettings parsed;std::string error;
      if(!contexts_->RequestContextForContext(context)||!suitecut::ContextSettings::Parse(input,parsed,error)){CompleteContext(id,false,nullptr,error.empty()?"Native browser context is closed":error);return;}
      std::optional<suitecut::NativeGeolocationValue::Coordinates>coordinates;if(parsed.geolocation)coordinates=suitecut::NativeGeolocationValue::Coordinates{parsed.geolocation->latitude,parsed.geolocation->longitude,parsed.geolocation->accuracy};
      if(!geolocation_.Set(id,context,std::move(coordinates),operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,const std::string&error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,nullptr,error);}))CompleteContext(id,false,nullptr,"Native geolocation update is pending or exceeds capacity");
      return;
    }
    if(action=="setOffline") {
      const auto context=params->GetInt("contextId");
      if(!contexts_->RequestContextForContext(context)||params->GetType("offline")!=VTYPE_BOOL||!network_policy_.Set(id,context,params->GetBool("offline"),operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,const std::string& error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,nullptr,error);}))CompleteContext(id,false,nullptr,"Invalid native context offline policy or capacity exceeded");
      return;
    }
    if(action=="workerNetworkBody"){
      const bool accepted=workers_->NetworkBody(params->GetInt("contextId"),params->GetString("owner"),params->GetString("bodyId"),params->GetString("stage"));CompleteContext(id,accepted,nullptr,accepted?"":"Actual worker network body owner is unavailable");return;
    }
    if(action=="routes"){HandleContextRoutes(id,params,operation->deadline_ms,operation->generation);return;}
    if(action=="route"){
      const int context=params->GetInt("contextId");
      if(!contexts_->RequestContextForContext(context)||!worker_network_->HandleRoute(id,context,params->GetString("owner"),params,operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,Dict result,const std::string&error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,result,error);}))CompleteContext(id,false,nullptr,"Actual route owner is unavailable or command invalid");
      return;
    }
    if(action=="workerNetwork"){
      const int context=params->GetInt("contextId");
      if(!contexts_->RequestContextForContext(context)||!worker_network_->Handle(id,context,params->GetString("requestId"),params,operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,Dict result,const std::string&error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,result,error);})){CompleteContext(id,false,nullptr,"Actual worker network request is unavailable or command invalid");}
      return;
    }
    if(action=="waitNetwork") {
      const auto context=params->GetInt("contextId");const auto cursor=params->GetType("afterSequence");
      if(!contexts_->RequestContextForContext(context)||(cursor!=VTYPE_INT&&cursor!=VTYPE_DOUBLE)||!context_network_->Wait(id,context,params->GetString("event"),suitecut::DictionaryNumber(params,"afterSequence"),operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,Dict result,const std::string& error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,result,error);}))CompleteContext(id,false,nullptr,"Invalid native context network wait or capacity exceeded");
      return;
    }
    if(action=="trace"){
      traces_->Handle(id,params->GetInt("contextId"),params,operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,Dict result,const std::string& error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,result,error);});return;
    }
    if(action=="worker") {
      workers_->Handle(id,params->GetInt("contextId"),params,operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation,console=params->GetString("action")=="console"||params->GetString("action")=="waitEventPredicate"||params->GetString("action")=="continueEventPredicate"](bool ok,Dict result,const std::string& error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,result,error,console);});return;
    }
    if(action=="waitConsole") {
      const auto context=params->GetInt("contextId");
      const auto cursor=params->GetType("afterSequence");
      if(!contexts_->RequestContextForContext(context)||(cursor!=VTYPE_INT&&cursor!=VTYPE_DOUBLE)||!context_console_->Wait(id,context,suitecut::DictionaryNumber(params,"afterSequence"),operation->deadline_ms,[this,alive=alive_,id,generation=operation->generation](bool ok,Dict result,const std::string& error){if(*alive&&operations_.IsCurrent(id,generation))CompleteContext(id,ok,result,error,true);}))CompleteContext(id,false,nullptr,"Invalid native context console wait or capacity exceeded");
      return;
    }
    if(action=="close"&&handler_native_origin.locator_handler_invocation&&params->GetType("contextId")==VTYPE_INT){for(const auto&[owner,source]:locator_handler_leases_){(void)owner;const auto work=source->locator_handler_active_;if(work&&work->token==handler_native_origin.locator_handler_invocation)work->close_contexts.insert(params->GetInt("contextId"));}}
    if(action=="closePage"&&handler_native_origin.locator_handler_invocation&&params->GetType("pageId")==VTYPE_INT){for(const auto&[owner,source]:locator_handler_leases_){(void)owner;const auto work=source->locator_handler_active_;if(work&&work->token==handler_native_origin.locator_handler_invocation)work->close_pages.insert(params->GetInt("pageId"));}}
    if(action=="closePage"&&params->GetType("pageId")==VTYPE_INT){const int page=params->GetInt("pageId"),context=contexts_->ContextId(page);const auto lease=traces_->CaptureLease(context);if(lease.context){const auto call=traces_->NextCall();if(call){auto event=suitecut::TracePageCloseMetadata(params,page,Now());event->SetString("callId","call@"+std::to_string(call));traces_->Before(context,call,event);trace_context_api_.emplace(id,std::make_pair(lease,call));}else traces_->CaptureError(lease,"Native trace context action identity exhausted");}}
    if((action=="create"||action=="newOwnedPage")&&params->HasKey("recordVideo")){
      const auto generation=operation->generation;
      context_video_->Prepare(id,generation,params->GetDictionary("recordVideo"),operation->deadline_ms,
        [this,alive=alive_,id,generation,action,params](suitecut::NativeContextVideo::Prepared prepared,const std::string& error){
          if(!*alive)return;
          if(!operations_.IsCurrent(id,generation)||operations_.IsExpired(id,generation,Now())||closing_){context_video_->Discard(std::move(prepared));if(operations_.IsCurrent(id,generation))CompleteContext(id,false,nullptr,"Native context video preparation exceeded its original deadline");return;}
          if(!prepared){CompleteContext(id,false,nullptr,error);return;}
          video_preparations_.emplace(id,VideoPreparation{generation,std::move(prepared)});
          ContextCommand(id,generation,action,params);
        });
    }else ContextCommand(id,operation->generation,action,params);
    return;
  }
#include "route_callback_engine.inc"
  int page = 1;
  if (message->HasKey("pageId")) {
    if (message->GetType("pageId") != VTYPE_INT || message->GetInt("pageId") < 1) { writer_.Reply(id, false, "Invalid native page ID"); return; }
    page = message->GetInt("pageId");
  }
  const auto source = sources_.find(page);
  if (source == sources_.end()) { writer_.Reply(id, command == "cancel", command == "cancel" ? "" : "Native page is closed or unknown"); return; }
  source->second->Command(line);
}
void Engine::Deadlines() {
  if(!startup_operations_.Expired(Now()).empty())StartupFailed("Native launch recording startup exceeded its original deadline");
  TickLocatorHandlers();
  for(const auto&[context,failure]:authentication_failures_){(void)context;failure->Tick();}
  websocket_routes_->Tick();
  context_video_->Tick();
  screenshot_inputs_->Tick();screenshot_comparisons_->Tick();trace_preview_->Tick();trace_sources_->Tick();traces_->Tick(Now());har_files_->Tick();har_recording_->Tick();context_routes_->Tick(Now());context_har_->Tick(Now());context_unroute_all_->Tick();
  for (const int id : operations_.Expired(Now())) {const auto http_error=http_->TimeoutError(id);CompleteContext(id,false,nullptr,http_error.empty()?"Native context operation timed out":http_error);}
  context_console_->Tick(Now());context_network_->Tick(Now());network_archive_->Tick(Now());network_policy_.Tick(Now());geolocation_.Tick(Now());authentication_.Tick(Now());network_headers_.Tick(Now());
  http_->Tick(Now());
  downloads_->Tick();
  Idle();
  if (!startup_operations_.Size() && !context_video_->NeedsTick() && locator_handler_leases_.empty() && ((closing_ && !authentication_.pending() && !authentication_.physical() && authentication_failures_.empty() && !http_->NeedsTick() && !traces_->NeedsTick() && !trace_preview_->PhysicalCount() && !trace_sources_->PhysicalCount() && !trace_background_.PhysicalCount() && !screenshot_comparisons_->NeedsTick() && !screenshot_inputs_->NeedsTick() && !websocket_routes_->Physical() && !websocket_routes_->NeedsTick() && !context_routes_->physical() && !context_routes_->pending() && !context_unroute_all_->pending() && !context_har_->physical() && !context_har_->pending() && !har_files_->Physical() && !har_recording_->NeedsTick() && !network_archive_->NeedsTick()) || (!operations_.Size() && !authentication_.pending() && !authentication_.physical() && authentication_failures_.empty() && !traces_->NeedsTick() && !trace_preview_->PhysicalCount() && !trace_sources_->PhysicalCount() && !trace_background_.PhysicalCount() && !screenshot_comparisons_->NeedsTick() && !screenshot_inputs_->NeedsTick() && !websocket_routes_->Physical() && !websocket_routes_->NeedsTick() && !context_routes_->physical() && !context_routes_->pending() && !context_unroute_all_->pending() && !context_har_->physical() && !context_har_->pending() && !har_files_->Physical() && !har_recording_->NeedsTick() && !network_archive_->NeedsTick() && !http_->NeedsTick() && !downloads_->NeedsTick()))) { deadline_timer_active_ = false; return; }
  CefPostDelayedTask(TID_UI, new Task([this, alive = alive_] { if (*alive) Deadlines(); },locator_handler_leases_.empty()?suitecut::AcceptedTaskWork::Kind::Work:suitecut::AcceptedTaskWork::Kind::BarrierCoordinator,LocatorHandlerMonitorOrigin()), 10);
}
void Engine::CloseAll() {
  if (closing_) return;
  closing_ = true;
  if(const auto startup=startup_operations_.Get(1))context_video_->Cancel(1,startup->generation);
  startup_operations_.Clear();
  for (const int id : operations_.Ids()) CompleteContext(id, false, nullptr, "Native browser is closing");
  context_video_->Clear();video_preparations_.clear();
  screenshot_inputs_->Clear();
  traces_->Clear();trace_preview_->Clear();trace_sources_->Clear();WakeTrace();
  recordings_->FinalizeAll(Now(),Clock::now()+std::chrono::seconds(60));
#if defined(OS_LINUX)
  streaming_->FinalizeAll(Clock::now()+std::chrono::seconds(60));
#endif
  http_->Clear();
  har_files_->Clear();
  contexts_->CloseAll();
  utilities_->Clear();
}
void Engine::Idle() {
#if defined(OS_LINUX)
  if (streaming_->PhysicalCount()) return;
#endif
  if (!closing_ || context_video_->Physical() || !locator_handler_leases_.empty() || !authentication_failures_.empty() || authentication_.pending() || authentication_.physical() || websocket_routes_->Physical() || websocket_routes_->NeedsTick() || context_routes_->physical() || context_routes_->pending() || context_unroute_all_->pending() || context_har_->physical() || context_har_->pending() || har_files_->Physical() || har_recording_->NeedsTick() || contexts_->PageCount() || utilities_->BrowserCount() || workers_->BrowserCount() || traces_->PhysicalCount() || trace_preview_->PhysicalCount() || trace_sources_->PhysicalCount() || trace_background_.PhysicalCount() || screenshot_comparisons_->NeedsTick() || screenshot_inputs_->NeedsTick() || trace_physical_ || http_->TracePhysical() || http_->PhysicalProfiles() || downloads_->NeedsTick() || recordings_->PhysicalCount()) return;
  if (reader_source_) reader_source_->StopReader();
  CefQuitMessageLoop();
}

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit App(Writer& writer) : writer_(writer),startup_started_(Now()) {}
#if defined(__linux__)
  void BindAcceptedEndpoint(suitecut::runner::RetainedSourceEndpoint* endpoint){native_work_endpoint_=endpoint;}
#endif
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override { RegisterSuiteCutInitScheme(registrar); }
  void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command) override {
    // Context storage state has one localStorage/IndexedDB/OPFS namespace per
    // origin, including third-party frames. Keep this browser policy explicit.
    std::vector<std::string> disabled_features{"ThirdPartyStoragePartitioning"};
#if defined(__linux__)
    if (command->GetSwitchValue("suitecut-headless") == "1") {
      // Chromium paint holding keeps a new primary page's input router inactive
      // until it produces visually different content. A transparent/empty OSR
      // document must accept real input without an on-demand screenshot redraw.
      disabled_features.emplace_back("PaintHolding");
    }
#endif
    auto disabled = command->GetSwitchValue("disable-features").ToString();
    for (const auto& feature : disabled_features) {
      bool present = false;
      for (size_t start = 0; start <= disabled.size();) {
        const auto end = disabled.find(',', start);
        auto token = disabled.substr(start, end == std::string::npos ? end : end - start);
        const auto first = token.find_first_not_of(" \t");
        const auto last = token.find_last_not_of(" \t");
        if (first != std::string::npos) token = token.substr(first, last - first + 1);
        if (token == feature || token.starts_with(feature + "<") || token.starts_with(feature + ":")) present = true;
        if (end == std::string::npos) break;
        start = end + 1;
      }
      if (!present) disabled += (disabled.empty() ? "" : ",") + feature;
    }
    command->AppendSwitchWithValue("disable-features", disabled);
    command->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    command->AppendSwitch("disable-background-timer-throttling");
    command->AppendSwitch("disable-renderer-backgrounding");
#if defined(__linux__)
    if (command->GetSwitchValue("suitecut-headless") == "1") {
      command->AppendSwitchWithValue("ozone-platform", "headless");
      // Keep display-free layout and screenshot pixels free of native scrollbar
      // widgets from startup, without resizing the page during each capture.
      command->AppendSwitch("hide-scrollbars");
      command->AppendSwitch("disable-gpu");
      // Software compositing remains display-free; explicitly select the CEF
      // bundled software GL driver because Chromium removed implicit fallback.
      command->AppendSwitchWithValue("use-gl", "angle");
      command->AppendSwitchWithValue("use-angle", "swiftshader");
      command->AppendSwitch("enable-unsafe-swiftshader");
    }
#endif
  }
  void OnContextInitialized() override {
    auto command = CefCommandLine::GetGlobalCommandLine();
    const int width = std::atoi(command->GetSwitchValue("suitecut-width").ToString().c_str());
    const int height = std::atoi(command->GetSwitchValue("suitecut-height").ToString().c_str());
    const auto parsed_fps = ParseSuiteCutFrameRate(command->GetSwitchValue("suitecut-fps").ToString());
    if (!parsed_fps) {
      CefQuitMessageLoop(); return;
    }
    const int fps = *parsed_fps;
    const double scale = std::atof(command->GetSwitchValue("suitecut-scale").ToString().c_str());
    const auto pixel_format = command->GetSwitchValue("suitecut-pixel-format").ToString();
    if (width < 2 || height < 2 || (scale != 1 && scale != 2) ||
        width * scale > 3840 || height * scale > 2160) {
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
    engine_ = std::make_unique<Engine>(writer_, width, height, scale, pixel_format == "i420", fps);
#if defined(__linux__)
    engine_->BindAcceptedWork(native_work_endpoint_);
#endif
    const suitecut::NativeWorkOriginFrame actual_worker_startup(suitecut::NativeWorkOrigin::ResidentWorker(engine_->AcceptedSourceTicket()));
    Dict launch_video;int startup_timeout_ms=0;std::string launch_error;
    if(command->HasSwitch("suitecut-record-video")&&!suitecut::ParseLaunchVideoOptions(
        command->GetSwitchValue("suitecut-record-video").ToString(),
        command->GetSwitchValue("suitecut-startup-timeout-ms").ToString(),launch_video,startup_timeout_ms,launch_error)){
      writer_.Error(launch_error.c_str());CefQuitMessageLoop();return;
    }
    if (!engine_->Initialize(launch_video,launch_video?startup_started_+startup_timeout_ms:0)) {CefQuitMessageLoop();return;}
  }
  void ReleaseSource() { engine_.reset(); }
 private:
  Writer& writer_;
  std::unique_ptr<Engine> engine_;
  const double startup_started_;
#if defined(__linux__)
  suitecut::runner::RetainedSourceEndpoint* native_work_endpoint_=nullptr;
#endif
  IMPLEMENT_REFCOUNTING(App);
};
}  // namespace

// The browser process keeps the normal compiler stack protection. It is a
// separate non-inlined frame so no embedder buffers exist while a zygote child
// resets its stack canary inside CefExecuteProcess.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int RunSuiteCutBrowserMain(const CefMainArgs& args, int argc, char** argv
#if defined(__linux__)
    , suitecut::runner::RetainedSourceEndpoint::Config runner_process_config
#endif
) {
#if defined(__linux__)
  std::optional<suitecut::runner::RetainedSourceEndpoint> runner_process_endpoint;
  try { runner_process_endpoint.emplace(runner_process_config); }
  catch (const std::exception& error) { std::fprintf(stderr,"Native runner process endpoint: %s\n",error.what()); return 1; }
#endif
  // CEF and subprocess libraries may print diagnostics to stdout. Reserve a
  // close-on-exec descriptor for media, then redirect ordinary stdout to stderr.
  const int output = dup(STDOUT_FILENO);
  if (output < 0 || fcntl(output, F_SETFD, FD_CLOEXEC) < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) return 1;
  CefSettings settings;
  auto command = CefCommandLine::CreateCommandLine();
  command->InitFromArgv(argc, argv);
  const auto profile = command->GetSwitchValue("suitecut-profile").ToString();
  // Startup failures precede the asynchronous writer. A bounded initial error
  // packet fits in the empty stdout pipe and cannot race any media/control I/O.
  auto startup_error=[output](const char* message){
    const std::string header="{\"version\":1,\"type\":\"error\",\"bytes\":0,\"error\":\""+std::string(message)+"\"}";
    const uint32_t size=static_cast<uint32_t>(header.size());
    const uint8_t prefix[]={static_cast<uint8_t>(size),static_cast<uint8_t>(size>>8),static_cast<uint8_t>(size>>16),static_cast<uint8_t>(size>>24)};
    auto write_all=[output](const void* value,size_t length){const auto* bytes=static_cast<const uint8_t*>(value);while(length){const auto count=write(output,bytes,length);if(count<0&&errno==EINTR)continue;if(count<=0)return;bytes+=count;length-=static_cast<size_t>(count);}};
    write_all(prefix,sizeof(prefix));write_all(header.data(),header.size());close(output);return 1;
  };
  if (profile.empty() || profile.front() != '/'||profile.size()>4096) return startup_error("Native profile path must be an absolute directory");
  const bool persistent=command->GetSwitchValue("suitecut-persistent-profile")=="1";
  if(command->HasSwitch("suitecut-persistent-profile")&&command->GetSwitchValue("suitecut-persistent-profile")!="1"&&command->GetSwitchValue("suitecut-persistent-profile")!="0")return startup_error("Invalid native persistent profile configuration");
  std::error_code profile_error;
  std::filesystem::create_directories(profile,profile_error);
  if(profile_error||!std::filesystem::is_directory(profile,profile_error)||profile_error)return startup_error("Could not prepare native profile directory");
  const auto actual_profile=std::filesystem::canonical(profile,profile_error);
  if(profile_error)return startup_error("Could not resolve native profile directory");
  SuiteCutProfileLock profile_lock;std::string lock_error;
  if(persistent&&!profile_lock.Acquire(actual_profile.string(),lock_error))return startup_error(lock_error.c_str());
  Writer writer(output);
  CefRefPtr<App> app = new App(writer);
#if defined(__linux__)
  app->BindAcceptedEndpoint(runner_process_endpoint?&*runner_process_endpoint:nullptr);
#endif
  if(command->HasSwitch("suitecut-downloads-path")){
    const auto path=command->GetSwitchValue("suitecut-downloads-path").ToString();
    if(path.empty()||path.size()>4096||path.front()!='/')return 1;
    std::error_code error;std::filesystem::create_directories(path,error);
    if(error||!std::filesystem::is_directory(path,error)||error)return 1;
  }
  CefString(&settings.root_cache_path) = actual_profile.string();
  if(persistent)CefString(&settings.cache_path)=actual_profile.string();
  settings.windowless_rendering_enabled = true;
  settings.log_severity = LOGSEVERITY_DISABLE;
  settings.persist_session_cookies = persistent;
  // Ordinary launches keep an empty cache_path (incognito). Explicit persistent
  // launches use the selected real directory; CEF owns its storage and locks.
#if defined(__linux__)
  // Process startup only. Existing SDK browser/page readiness is independent.
  if (runner_process_endpoint->Enabled() && !runner_process_endpoint->WriteStartup()) {
    std::fprintf(stderr,"Native runner process startup receipt write failed\n"); return 1;
  }
#endif
  if (!CefInitialize(args, settings, app, nullptr)) return 1;
  CefRunMessageLoop();
  app->ReleaseSource();
  app = nullptr;
  CefShutdown();
  writer.Stop();
#if defined(__linux__)
  // Actual process shutdown boundary, not idle, CloseReceipt or physical exit.
  if (runner_process_endpoint->Enabled() && !runner_process_endpoint->WriteShutdownReached(0)) {
    std::fprintf(stderr,"Native runner process shutdown-boundary receipt write failed\n"); return 1;
  }
#endif
  return 0;
}

// Chromium intentionally replaces the stack canary after a Linux zygote fork.
// A protected wrapper spanning CefExecuteProcess would compare its old canary
// when the utility subprocess returns, producing a false stack-smashing abort.
// Only this buffer-free dispatch frame opts out; every browser/control frame
// remains protected and Chromium's sandbox and canary reset remain enabled.
#if defined(__linux__) && defined(__GNUC__)
__attribute__((noinline, no_stack_protector))
#endif
int RunSuiteCutBrowser(int argc, char** argv) {
  const int encoder=suitecut::MediaEncoderHelperMain(argc,argv);
  if(encoder>=0)return encoder;
  signal(SIGPIPE, SIG_IGN);
#if defined(__linux__)
  // Trivial configuration only; no endpoint owner/buffer spans zygote dispatch.
  suitecut::runner::RetainedSourceEndpoint::Config runner_process_config;
  try { runner_process_config=suitecut::runner::RetainedSourceEndpoint::Prepare(argc,argv); }
  catch (const std::exception& error) { std::fprintf(stderr,"Native runner process endpoint preparation: %s\n",error.what()); return 1; }
#endif
  CefMainArgs args(argc, argv);
#if !defined(__APPLE__)
  const int code = CefExecuteProcess(args, new SuiteCutRendererApp(), nullptr);
  if (code >= 0) return code;
#endif
  return RunSuiteCutBrowserMain(args, argc, argv
#if defined(__linux__)
      , runner_process_config
#endif
  );
}
