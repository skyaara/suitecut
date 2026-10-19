// SPDX-License-Identifier: MIT
#include "streaming_controls.h"
#include "media_pipeline.h"
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace suitecut {
StreamingControls::StreamingControls(Hooks hooks) : hooks_(std::move(hooks)) {
  if (!hooks_.dispatch) throw std::invalid_argument("Native publisher requires owning-thread dispatch");
}
StreamingControls::~StreamingControls() { CloseAll(); }
std::string StreamingControls::Start(int owner, int page, int context, StreamPipelineOptions options, Reply ready, std::string client_token) {
  if (client_token.size() > 128 || !std::all_of(client_token.begin(), client_token.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }))
    throw std::invalid_argument("Invalid native stream correlation token");
  options.ffmpeg_path = ResolveMediaExecutable(options.ffmpeg_path);
  ValidateStreamPipelineOptions(options);
  if (owner <= 0 || page <= 0 || context <= 0 || !ready || weak_from_this().expired()) throw std::invalid_argument("Native publisher owner is invalid");
  std::lock_guard lock(mutex_);
  if (closing_ || closing_pages_.contains(page) || failed_pages_.contains(page) || closing_contexts_.contains(context)) throw std::runtime_error("Native streaming owner is closing or closed");
  size_t waiters = 0;
  for (const auto& [id, record] : records_) {
    if (!client_token.empty() && record.client_token == client_token) throw std::runtime_error("Native stream correlation token is already retained");
    if (record.ready.contains(owner) || record.stopped.contains(owner)) throw std::runtime_error("Duplicate native stream command");
    if (record.pipeline && record.destination == options.destination) throw std::runtime_error("Native stream destination already has an active publisher");
    waiters += record.ready.size() + record.stopped.size();
  }
  if (waiters >= 128) throw std::runtime_error("Native stream wait capacity exceeded");
  while (records_.size() >= 256) {
    auto removable = records_.end();
    for (auto it = records_.begin(); it != records_.end(); ++it) if (!it->second.pipeline) { removable = it; break; }
    if (removable == records_.end()) throw std::runtime_error("Native stream registry capacity exceeded");
    records_.erase(removable);
  }
  if (sequence_ == std::numeric_limits<uint64_t>::max()) throw std::runtime_error("Native stream identity exhausted");
  const auto id = "stream-" + std::to_string(++sequence_);
  Record record; record.owner_page = record.selected_page = page; record.owner_context = record.selected_context = context;
  record.width = options.source_width; record.height = options.source_height; record.i420 = options.i420; record.frames_per_second = options.frames_per_second; record.destination = options.destination; record.client_token = std::move(client_token);
  record.ready.emplace(owner, BindNativeLocatorHandlerAsync(std::move(ready)));
  if(auto receipt=RetainNativeLocatorHandlerAsync())record.handler_ready.emplace(owner,std::move(receipt));
  const auto weak = weak_from_this(); const auto dispatch = hooks_.dispatch;
  record.pipeline = std::make_shared<StreamPipeline>(std::move(options),
    [weak, dispatch, id](const auto& result) { dispatch([weak, id, result] { if (auto controls = weak.lock()) controls->Finished(id, result); }); },
    [weak, id](const auto& result) { if (auto controls = weak.lock()) controls->QueueStarted(id, result); });
  records_.emplace(id, std::move(record)); return id;
}
std::shared_ptr<StreamPipeline> StreamingControls::Active(const std::string& id) const {
  std::lock_guard lock(mutex_); const auto found = records_.find(id);
  if (found == records_.end()) throw std::runtime_error("Unknown native stream");
  if (!found->second.pipeline) throw std::runtime_error("Native stream has already completed");
  return found->second.pipeline;
}
void StreamingControls::QueueStarted(const std::string& id,const StreamPipelineResult& startup){
  {std::lock_guard lock(mutex_);const auto found=records_.find(id);if(found==records_.end()||!found->second.pipeline)return;
    auto&record=found->second;record.queued_startup=startup;
    if(record.startup_queued)return;
    record.startup_queued=true;
  }
  // Repeated actual network attempts cannot accumulate owning-thread tasks
  // while CEF is occupied. Retain one bounded latest genuine readiness record.
  const auto weak=weak_from_this();hooks_.dispatch([weak,id]{if(auto controls=weak.lock())controls->Started(id);});
}
void StreamingControls::Started(const std::string& id) {
  std::map<int, Reply> replies; StreamPipelineResult result; int page; std::string phase;
  { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end() || !found->second.pipeline) return;
    auto& record = found->second;record.startup_queued=false;auto startup=std::move(record.queued_startup);
    if (record.phase != "stopping") record.phase = "streaming";
    result = record.pipeline->Snapshot();
    // A queued physical completion may follow this UI task. The original
    // readiness callback is tied to genuine encoded progress, not that later
    // result or a successful pipe write alone.
    if (!result.encoded_progress_observed) result = startup;
    page = record.owner_page; phase = record.phase;
    for(const auto&[owner,reply]:record.ready){(void)reply;record.handler_ready.erase(owner);}
    replies.swap(record.ready);
  }
  if (hooks_.event) hooks_.event(page, id, phase, result);
  for (auto& [command, reply] : replies) reply(id, result);
}
void StreamingControls::Finished(const std::string& id, const StreamPipelineResult& result) {
  std::shared_ptr<StreamPipeline> physical;
  { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end()) return; physical = found->second.pipeline; }
  if (physical) physical->Join();
  physical.reset();
  std::map<int, Reply> ready, stopped; std::vector<std::function<void()>> continuations; int page, owner_context, selected_context;
  const std::string phase = result.failure.empty() ? "complete" : "failed";
  { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end()) return;
    auto& record = found->second; record.pipeline.reset(); record.result = result; record.phase = phase; page = record.owner_page; owner_context = record.owner_context; selected_context = record.selected_context;
    ready.swap(record.ready); stopped.swap(record.stopped);
    record.handler_ready.clear();record.handler_stopped.clear();
    for (auto close = page_closes_.begin(); close != page_closes_.end();) {
      bool pending = false;
      for (const auto& [name, live] : records_) if (live.pipeline && (live.owner_page == close->first || live.selected_page == close->first)) pending = true;
      if (pending) { ++close; continue; }
      for (auto& continuation : close->second) continuations.push_back(std::move(continuation));
      close = page_closes_.erase(close);
    }
  }
  if (hooks_.event) hooks_.event(page, id, phase, result);
  for (auto& [command, reply] : ready) reply(id, result);
  for (auto& [command, reply] : stopped) reply(id, result);
  for (auto& continuation : continuations) continuation();
  if (hooks_.physical_settled) hooks_.physical_settled(owner_context, selected_context);
}
void StreamingControls::Stop(int owner, const std::string& id, std::chrono::steady_clock::time_point deadline, Reply reply) {
  std::shared_ptr<StreamPipeline> pipeline; StreamPipelineResult result;
  { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end() || owner <= 0 || !reply) throw std::invalid_argument("Invalid native stream stop");
    size_t waiters = 0; for (const auto& [name, live] : records_) {
      if (live.ready.contains(owner) || live.stopped.contains(owner)) throw std::runtime_error("Duplicate native stream command");
      waiters += live.ready.size() + live.stopped.size();
    }
    if (waiters >= 128) throw std::runtime_error("Native stream wait capacity exceeded");
    pipeline = found->second.pipeline; result = found->second.result;
    if (pipeline) {found->second.phase = "stopping";found->second.stopped.emplace(owner, BindNativeLocatorHandlerAsync(std::move(reply)));if(auto receipt=RetainNativeLocatorHandlerAsync())found->second.handler_stopped.emplace(owner,std::move(receipt)); }
  }
  if (pipeline) pipeline->Stop(deadline); else reply(id, result);
}
void StreamingControls::Pause(const std::string& id, bool paused) { Active(id)->Pause(paused); }
void StreamingControls::Select(const std::string& id, int page, int context, int width, int height, bool i420) {
  { std::lock_guard lock(mutex_); const auto found = records_.find(id);
    if (found == records_.end() || !found->second.pipeline || found->second.phase == "stopping") throw std::runtime_error("Native stream is not selectable");
    auto& record = found->second;
    if (closing_pages_.contains(page) || failed_pages_.contains(page) || closing_contexts_.contains(context)) throw std::runtime_error("Native stream selected page is closing");
    if (page <= 0 || context <= 0 || width != record.width || height != record.height || i420 != record.i420) throw std::invalid_argument("Native stream source geometry or ownership is incompatible");
    if (record.selected_page == page) return;
    // Dispatch, source selection and queue clearing share the manager lock so
    // an old frame/audio callback cannot enter after the actual page switch.
    record.pipeline->SwitchSource(); record.selected_page = page; record.selected_context = context;
  }
}
void StreamingControls::Cancel(const std::string& id, const std::string& reason) { Active(id)->Cancel(reason); }
void StreamingControls::CancelCommand(int owner) {
  std::vector<std::shared_ptr<StreamPipeline>> canceled;
  { std::lock_guard lock(mutex_); for (auto& [id, record] : records_) if (record.ready.erase(owner) + record.stopped.erase(owner) && record.pipeline) canceled.push_back(record.pipeline); }
  for (auto& pipeline : canceled) pipeline->Cancel("Native stream operation canceled");
}
bool StreamingControls::NeedsFrame(int page) const {
  std::lock_guard lock(mutex_);
  for (const auto& [id, record] : records_)
    if (record.pipeline && record.selected_page == page) return true;
  return false;
}
void StreamingControls::Frame(int page, const std::vector<uint8_t>& data, double timestamp) {
  std::lock_guard lock(mutex_);
  for (const auto& [id, record] : records_) if (record.pipeline && record.selected_page == page) record.pipeline->Frame(data, timestamp);
}
void StreamingControls::Audio(int page, const std::vector<uint8_t>& data, double timestamp) {
  std::lock_guard lock(mutex_);
  for (const auto& [id, record] : records_) if (record.pipeline && record.selected_page == page) record.pipeline->Audio(data, timestamp);
}
void StreamingControls::BeforePageClose(int page, std::chrono::steady_clock::time_point deadline, std::function<void()> continuation) {
  std::vector<std::shared_ptr<StreamPipeline>> physical;
  { std::lock_guard lock(mutex_); closing_pages_.insert(page); for (auto& [id, record] : records_) if (record.pipeline && (record.owner_page == page || record.selected_page == page)) { record.phase = "stopping"; physical.push_back(record.pipeline); }
    if (!physical.empty()) page_closes_[page].push_back(std::move(continuation)); }
  if (physical.empty()) continuation(); else for (auto& pipeline : physical) pipeline->Stop(deadline);
}
void StreamingControls::ClosePage(int page) {
  std::vector<std::shared_ptr<StreamPipeline>> physical;
  { std::lock_guard lock(mutex_); closing_pages_.insert(page); for (auto& [id, record] : records_) if (record.pipeline && (record.owner_page == page || record.selected_page == page)) { record.phase = "stopping"; physical.push_back(record.pipeline); } }
  for (auto& pipeline : physical) pipeline->Cancel("Native stream source page failed or closed");
}
void StreamingControls::FailPage(int page) {
  std::vector<std::shared_ptr<StreamPipeline>> physical;
  { std::lock_guard lock(mutex_); failed_pages_.insert(page);
    for (auto& [id, record] : records_) if (record.pipeline && (record.owner_page == page || record.selected_page == page)) {
      record.phase = "stopping"; physical.push_back(record.pipeline);
    }
  }
  for (auto& pipeline : physical) pipeline->Cancel("Native stream source page failed or closed");
}
void StreamingControls::RecoverPage(int page) { std::lock_guard lock(mutex_); failed_pages_.erase(page); }
void StreamingControls::PageGone(int page) { std::lock_guard lock(mutex_); closing_pages_.erase(page); failed_pages_.erase(page); }
void StreamingControls::ContextGone(int context) { std::lock_guard lock(mutex_); closing_contexts_.erase(context); }
void StreamingControls::FinalizeContext(int context, std::chrono::steady_clock::time_point deadline) {
  std::vector<std::shared_ptr<StreamPipeline>> physical;
  { std::lock_guard lock(mutex_); closing_contexts_.insert(context); for (auto& [id, record] : records_) if (record.pipeline && (record.owner_context == context || record.selected_context == context)) { record.phase = "stopping"; physical.push_back(record.pipeline); } }
  for (auto& pipeline : physical) pipeline->Stop(deadline);
}
void StreamingControls::FinalizeAll(std::chrono::steady_clock::time_point deadline) {
  std::vector<std::shared_ptr<StreamPipeline>> physical;
  { std::lock_guard lock(mutex_); closing_ = true; for (auto& [id, record] : records_) if (record.pipeline) { record.phase = "stopping"; physical.push_back(record.pipeline); } }
  for (auto& pipeline : physical) pipeline->Stop(deadline);
}
void StreamingControls::CloseAll() {
  std::vector<std::shared_ptr<StreamPipeline>> physical;
  { std::lock_guard lock(mutex_); closing_ = true; for (auto& [id, record] : records_) if (record.pipeline) { record.phase = "stopping"; physical.push_back(record.pipeline); } }
  for (auto& pipeline : physical) pipeline->Cancel("Native streaming engine failed or closed");
}
bool StreamingControls::Owns(const std::string& id, int page) const { std::lock_guard lock(mutex_); const auto found = records_.find(id); return found != records_.end() && found->second.owner_page == page; }
bool StreamingControls::ContextReady(int context) const { std::lock_guard lock(mutex_); for (const auto& [id, record] : records_) if (record.pipeline && (record.owner_context == context || record.selected_context == context)) return false; return true; }
size_t StreamingControls::PhysicalCount() const { std::lock_guard lock(mutex_); size_t count = 0; for (const auto& [id, record] : records_) if (record.pipeline) ++count; return count; }
size_t StreamingControls::MetadataCount() const { std::lock_guard lock(mutex_); return records_.size(); }
std::string StreamingControls::ClientToken(const std::string& id) const { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end()) throw std::runtime_error("Unknown native stream"); return found->second.client_token; }
int StreamingControls::SelectedPage(const std::string& id) const { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end()) throw std::runtime_error("Unknown native stream"); return found->second.selected_page; }
double StreamingControls::FramesPerSecond(const std::string& id) const { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end()) throw std::runtime_error("Unknown native stream"); return found->second.frames_per_second; }
std::string StreamingControls::Phase(const std::string& id) const { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end()) throw std::runtime_error("Unknown native stream"); return found->second.phase; }
StreamPipelineResult StreamingControls::Status(const std::string& id) const { std::lock_guard lock(mutex_); const auto found = records_.find(id); if (found == records_.end()) throw std::runtime_error("Unknown native stream"); return found->second.pipeline ? found->second.pipeline->Snapshot() : found->second.result; }
}  // namespace suitecut
