// SPDX-License-Identifier: MIT
#include "recording_controls.h"
#include <limits>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace suitecut {
RecordingControls::RecordingControls(Hooks hooks) : hooks_(std::move(hooks)) {
  if (!hooks_.dispatch) throw std::runtime_error("Native recording requires UI dispatch");
}
RecordingControls::~RecordingControls() { CloseAll(); }
std::string RecordingControls::Start(int owner, int page, int context, MediaPipelineOptions options, Reply ready) {
  return StartImpl(owner, page, context, std::move(options), std::move(ready), false);
}
std::string RecordingControls::StartAutomatic(int page, int context, MediaPipelineOptions options) {
  if (!hooks_.now) throw std::runtime_error("Automatic native recording requires an actual capture clock");
  return StartImpl(0, page, context, std::move(options), {}, true);
}
std::string RecordingControls::StartImpl(int owner, int page, int context, MediaPipelineOptions options, Reply ready, bool automatic) {
  if (!automatic && !options.ffmpeg_path.empty() && options.ffmpeg_path.front() != '/')
    options.ffmpeg_path = ResolveMediaExecutable(options.ffmpeg_path);
  ValidateMediaPipelineOptions(options);
  if (!automatic) options.output_path = (std::filesystem::canonical(std::filesystem::path(options.output_path).parent_path()) /
      std::filesystem::path(options.output_path).filename()).string();
  if ((automatic ? (owner != 0 || bool(ready)) : (owner <= 0 || !ready)) || page <= 0 || context <= 0 || weak_from_this().expired())
    throw std::runtime_error("Invalid native recording owner");
  std::lock_guard lock(mutex_);
  if (closing_) throw std::runtime_error("Native recording engine has been closed");
  for (const auto& [id, record] : records_) {
    if (record.pipeline && record.result.output_path == options.output_path)
      throw std::runtime_error("Native recording output already has an active writer");
    if (record.pipeline && record.page == page) throw std::runtime_error("Native page already has an active recording");
    if (owner && (record.ready.contains(owner) || record.stopped.contains(owner))) throw std::runtime_error("Duplicate native recording command");
  }
  if (sequence_ == std::numeric_limits<uint64_t>::max()) throw std::runtime_error("Native recording identity exhausted");
  while (records_.size() >= 256) {
    auto removable = records_.end();
    for (auto candidate = records_.begin(); candidate != records_.end(); ++candidate)
      if (!candidate->second.pipeline) { removable = candidate; break; }
    if (removable == records_.end()) throw std::runtime_error("Native recording registry capacity exceeded");
    records_.erase(removable);
  }
  const std::string id = "recording-" + std::to_string(++sequence_);
  Record record;
  record.page = page; record.context = context; record.automatic = automatic; record.viewport_frames = options.dynamic_viewport; record.result.output_path = options.output_path;
  record.result.frames_per_second = options.frames_per_second;
  if (ready) {record.ready.emplace(owner, BindNativeLocatorHandlerAsync(std::move(ready)));if(auto receipt=RetainNativeLocatorHandlerAsync())record.handler_ready.emplace(owner,std::move(receipt));}
  // Allocate the paint index node before starting its physical producer.
  decltype(active_pages_) index;index.emplace(page,id);auto node=index.extract(page);
  const auto weak = weak_from_this();
  const auto dispatch = hooks_.dispatch;
  record.pipeline = std::make_shared<MediaPipeline>(std::move(options),
    [weak, dispatch, id](const MediaPipelineResult& result) {
      dispatch([weak, id, result] { if (const auto controls = weak.lock()) controls->Finished(id, result); });
    },
    [weak, dispatch, id](double timestamp) {
      dispatch([weak, id, timestamp] { if (const auto controls = weak.lock()) controls->Started(id, timestamp); });
    });
  records_.emplace(id, std::move(record));
  active_pages_.insert(std::move(node));
  return id;
}
std::shared_ptr<MediaPipeline> RecordingControls::Active(const std::string& id) const {
  std::lock_guard lock(mutex_);
  const auto found = records_.find(id);
  if (found == records_.end()) throw std::runtime_error("Unknown native recording");
  return found->second.pipeline;
}
std::shared_ptr<MediaPipeline> RecordingControls::ForPage(int page, bool viewport) const {
  std::lock_guard lock(mutex_);
  const auto index = active_pages_.find(page);
  if (index == active_pages_.end()) return {};
  const auto found = records_.find(index->second);
  if (found != records_.end() && found->second.viewport_frames == viewport) return found->second.pipeline;
  return {};
}
void RecordingControls::Started(const std::string& id, double timestamp) {
  std::map<int, Reply> replies;
  MediaPipelineResult result;
  std::shared_ptr<MediaPipeline> close_pipeline;
  std::chrono::steady_clock::time_point close_deadline{};
  int page;
  {
    std::lock_guard lock(mutex_);
    const auto found = records_.find(id); if (found == records_.end() || !found->second.pipeline) return;
    auto& record = found->second;
    if (record.close_when_started) {
      close_pipeline = record.pipeline; close_deadline = record.close_deadline;
      record.close_when_started = false; record.phase = "stopping";
    } else if (record.phase != "stopping") record.phase = "recording";
    record.result = record.pipeline->Snapshot();
    record.result.first_timestamp_ms = timestamp;
    result = record.result; page = record.page;
    // Genuine Started transfers successful startup to the resident recorder.
    // Canceled owner receipts remain until Finished physically joins it.
    for(const auto&[owner,reply]:record.ready){(void)reply;record.handler_ready.erase(owner);}
    replies.swap(record.ready);
  }
  if (hooks_.event) hooks_.event(page, id, "recording", result);
  for (auto& [owner, reply] : replies) reply(id, result);
  if (close_pipeline) close_pipeline->Stop(hooks_.now(), close_deadline);
}
void RecordingControls::Finished(const std::string& id, const MediaPipelineResult& result) {
  std::shared_ptr<MediaPipeline> physical;
  {
    std::lock_guard lock(mutex_);
    const auto found = records_.find(id); if (found == records_.end()) return;
    physical = found->second.pipeline;
  }
  // The worker has queued this task after waitpid and private-file cleanup.
  // Join confirms the callback itself has returned before releasing the owner.
  if (physical) physical->Join();
  physical.reset();
  std::map<int, Reply> ready, stopped;
  std::vector<std::function<void()>> page_closed;
  int page, context;
  const std::string phase = result.failure.empty() ? "complete" : "failed";
  {
    std::lock_guard lock(mutex_);
    const auto found = records_.find(id); if (found == records_.end()) return;
    auto& record = found->second;
    record.pipeline.reset(); record.result = result; record.phase = phase;
    const auto index=active_pages_.find(record.page);
    if(index!=active_pages_.end()&&index->second==id)active_pages_.erase(index);
    page = record.page; context = record.context;
    ready.swap(record.ready); stopped.swap(record.stopped);
    record.handler_ready.clear();record.handler_stopped.clear();
    bool remaining = false;
    for (const auto& [key, candidate] : records_) if (candidate.page == page && candidate.pipeline) remaining = true;
    const auto closing = page_closes_.find(page);
    if (!remaining && closing != page_closes_.end()) { page_closed = std::move(closing->second); page_closes_.erase(closing); }
  }
  if (hooks_.event) hooks_.event(page, id, phase, result);
  for (auto& [owner, reply] : ready) reply(id, result);
  for (auto& [owner, reply] : stopped) reply(id, result);
  for (auto& continuation : page_closed) continuation();
  if (hooks_.physical_settled) hooks_.physical_settled(page, context);
}
void RecordingControls::Stop(int owner, const std::string& id, double timestamp,
    std::chrono::steady_clock::time_point deadline, Reply reply) {
  std::shared_ptr<MediaPipeline> pipeline;
  MediaPipelineResult result;
  {
    std::lock_guard lock(mutex_);
    const auto found = records_.find(id);
    if (found == records_.end() || owner <= 0 || !reply) throw std::runtime_error("Invalid native recording stop");
    size_t waiters = 0;
    for (const auto& [key, record] : records_) {
      if (record.ready.contains(owner) || record.stopped.contains(owner)) throw std::runtime_error("Duplicate native recording command");
      waiters += record.ready.size() + record.stopped.size();
    }
    if (waiters >= 128) throw std::runtime_error("Native recording wait capacity exceeded");
    pipeline = found->second.pipeline; result = found->second.result;
    if (pipeline) {found->second.stopped.emplace(owner, BindNativeLocatorHandlerAsync(std::move(reply)));if(auto receipt=RetainNativeLocatorHandlerAsync())found->second.handler_stopped.emplace(owner,std::move(receipt));found->second.phase = "stopping"; }
  }
  if (pipeline) pipeline->Stop(timestamp, deadline);
  else reply(id, result);
}
void RecordingControls::Pause(const std::string& id, double timestamp) { if (auto pipeline = Active(id)) pipeline->Pause(timestamp); }
void RecordingControls::Resume(const std::string& id, double timestamp) { if (auto pipeline = Active(id)) pipeline->Resume(timestamp); }
void RecordingControls::Cancel(const std::string& id, const std::string& reason) { if (auto pipeline = Active(id)) pipeline->Cancel(reason); }
void RecordingControls::CancelCommand(int owner) {
  std::vector<std::shared_ptr<MediaPipeline>> canceled;
  {
    std::lock_guard lock(mutex_);
    for (auto& [id, record] : records_) {
      const bool removed = record.ready.erase(owner) + record.stopped.erase(owner) > 0;
      if (removed && record.pipeline) canceled.push_back(record.pipeline);
    }
  }
  for (const auto& pipeline : canceled) pipeline->Cancel("Native recording operation canceled");
}
bool RecordingControls::NeedsFrame(int page, bool viewport) const {
  return static_cast<bool>(ForPage(page, viewport));
}
void RecordingControls::Frame(int page, const std::vector<uint8_t>& data, double timestamp) {
  if (auto pipeline = ForPage(page)) pipeline->Frame(data, timestamp);
}
void RecordingControls::ViewportFrame(int page, const std::vector<uint8_t>& bgra, int width, int height,
    double css_width, double css_height, double timestamp) {
  if (auto pipeline = ForPage(page, true)) pipeline->ViewportFrame(bgra, width, height, css_width, css_height, timestamp);
}
void RecordingControls::Audio(int page, const std::vector<uint8_t>& data, double timestamp) {
  if (auto pipeline = ForPage(page)) pipeline->Audio(data, timestamp);
}
void RecordingControls::ClosePage(int page) {
  std::vector<std::shared_ptr<MediaPipeline>> pipelines;
  { std::lock_guard lock(mutex_); for (auto& [id, record] : records_) if (record.page == page && record.pipeline) pipelines.push_back(record.pipeline); }
  for (auto& pipeline : pipelines) pipeline->Cancel("Native recording page has been closed");
}
void RecordingControls::BeforePageClose(int page, std::function<void()> continuation,
    std::chrono::steady_clock::time_point deadline) {
  std::shared_ptr<MediaPipeline> stop;
  bool pending = false;
  if (deadline == std::chrono::steady_clock::time_point{}) deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  {
    std::lock_guard lock(mutex_);
    for (auto& [id, record] : records_) if (record.page == page && record.pipeline) {
      if (!hooks_.now) throw std::runtime_error("Native recording close requires an actual capture clock");
      pending = true;
      if (record.automatic) {
        if (record.close_deadline == std::chrono::steady_clock::time_point{} || deadline < record.close_deadline) record.close_deadline = deadline;
        record.pipeline->LimitDeadline(record.close_deadline);
      }
      if (record.automatic && record.phase == "starting") record.close_when_started = true;
      else stop = record.pipeline;
    }
    if (pending) page_closes_[page].push_back(std::move(continuation));
  }
  // Automatic admission precedes first paint. Retain the actual page until its
  // first real frame starts the encoder; never manufacture a frame or ready ACK.
  // A native first-frame failure still reaches Finished and releases the page.
  if (stop) stop->Stop(hooks_.now(), deadline);
  if (!pending) continuation();
}
bool RecordingControls::Owns(const std::string& id, int page) const {
  std::lock_guard lock(mutex_); const auto found = records_.find(id);
  return found != records_.end() && found->second.page == page;
}
void RecordingControls::CloseContext(int context) {
  std::vector<std::shared_ptr<MediaPipeline>> pipelines;
  { std::lock_guard lock(mutex_); for (auto& [id, record] : records_) if (record.context == context && record.pipeline) pipelines.push_back(record.pipeline); }
  for (auto& pipeline : pipelines) pipeline->Cancel("Native recording context has been closed");
}
void RecordingControls::CloseAll() {
  std::vector<std::shared_ptr<MediaPipeline>> pipelines;
  { std::lock_guard lock(mutex_); closing_ = true; for (auto& [id, record] : records_) if (record.pipeline) pipelines.push_back(record.pipeline); }
  for (auto& pipeline : pipelines) pipeline->Cancel("Native recording engine has been closed");
}
void RecordingControls::FinalizeContext(int context, double timestamp, std::chrono::steady_clock::time_point deadline) {
  std::vector<std::shared_ptr<MediaPipeline>> pipelines;
  {
    std::lock_guard lock(mutex_);
    for (auto& [id, record] : records_) if (record.context == context && record.pipeline) {
      if (record.automatic) {
        if (record.close_deadline == std::chrono::steady_clock::time_point{} || (deadline != std::chrono::steady_clock::time_point{} && deadline < record.close_deadline)) record.close_deadline = deadline;
        record.pipeline->LimitDeadline(record.close_deadline);
      }
      if (record.automatic && record.phase == "starting") record.close_when_started = true;
      else pipelines.push_back(record.pipeline);
    }
  }
  for (auto& pipeline : pipelines) pipeline->Stop(timestamp, deadline);
}
void RecordingControls::FinalizeAll(double timestamp, std::chrono::steady_clock::time_point deadline) {
  std::vector<std::shared_ptr<MediaPipeline>> pipelines;
  {
    std::lock_guard lock(mutex_); closing_ = true;
    for (auto& [id, record] : records_) if (true && record.pipeline) {
      if (record.automatic) {
        if (record.close_deadline == std::chrono::steady_clock::time_point{} || (deadline != std::chrono::steady_clock::time_point{} && deadline < record.close_deadline)) record.close_deadline = deadline;
        record.pipeline->LimitDeadline(record.close_deadline);
      }
      if (record.automatic && record.phase == "starting") record.close_when_started = true;
      else pipelines.push_back(record.pipeline);
    }
  }
  for (auto& pipeline : pipelines) pipeline->Stop(timestamp, deadline);
}
bool RecordingControls::ContextReady(int context) const {
  std::lock_guard lock(mutex_);
  for (const auto& [id, record] : records_) if (record.context == context && record.pipeline) return false;
  return true;
}
size_t RecordingControls::PhysicalCount() const {
  std::lock_guard lock(mutex_);
  size_t count = 0; for (const auto& [id, record] : records_) if (record.pipeline) ++count;
  return count;
}
size_t RecordingControls::MetadataCount() const { std::lock_guard lock(mutex_); return records_.size(); }
MediaPipelineResult RecordingControls::Status(const std::string& id) const {
  std::lock_guard lock(mutex_);
  const auto found = records_.find(id);
  if (found == records_.end()) throw std::runtime_error("Unknown native recording");
  return found->second.pipeline ? found->second.pipeline->Snapshot() : found->second.result;
}
std::string RecordingControls::Phase(const std::string& id) const {
  std::lock_guard lock(mutex_); const auto found = records_.find(id);
  if (found == records_.end()) throw std::runtime_error("Unknown native recording");
  return found->second.phase;
}
}  // namespace suitecut
