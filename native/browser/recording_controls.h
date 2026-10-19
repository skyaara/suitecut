// SPDX-License-Identifier: MIT
#pragma once
#include "locator_handler_async_work.h"
#include "media_pipeline.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace suitecut {
// Engine-level registry: page lifetime may end before an encoder physically
// settles. This owner survives pages and exposes that barrier to context close.
class RecordingControls : public std::enable_shared_from_this<RecordingControls> {
 public:
  using Reply = std::function<void(const std::string& id, const MediaPipelineResult& result)>;
  struct Hooks {
    // Must enqueue on the owning UI thread, never execute the closure inline.
    std::function<void(std::function<void()>)> dispatch;
    std::function<void(int page, const std::string& id, const std::string& phase,
                       const MediaPipelineResult&)> event;
    std::function<void(int page, int context)> physical_settled;
    std::function<double()> now;
  };
  explicit RecordingControls(Hooks hooks);
  ~RecordingControls();
  std::string Start(int owner_command, int page, int context, MediaPipelineOptions options, Reply ready);
  // Native context-video admission has no public command or first-frame ACK.
  // Paths/executable must already have been prepared by the engine IO owner.
  std::string StartAutomatic(int page, int context, MediaPipelineOptions options);
  void Stop(int owner_command, const std::string& id, double timestamp,
            std::chrono::steady_clock::time_point deadline, Reply reply);
  void Pause(const std::string& id, double timestamp);
  void Resume(const std::string& id, double timestamp);
  void Cancel(const std::string& id, const std::string& reason);
  void CancelCommand(int owner_command);
  bool NeedsFrame(int page, bool viewport = false) const;
  void Frame(int page, const std::vector<uint8_t>& data, double timestamp);
  void ViewportFrame(int page, const std::vector<uint8_t>& bgra, int width, int height,
                     double css_width, double css_height, double timestamp);
  void Audio(int page, const std::vector<uint8_t>& data, double timestamp);
  void ClosePage(int page);
  void BeforePageClose(int page, std::function<void()> continuation,
      std::chrono::steady_clock::time_point deadline = {});
  bool Owns(const std::string& id, int page) const;
  void CloseContext(int context);
  void CloseAll();
  void FinalizeContext(int context, double timestamp, std::chrono::steady_clock::time_point deadline);
  void FinalizeAll(double timestamp, std::chrono::steady_clock::time_point deadline);
  bool ContextReady(int context) const;
  size_t PhysicalCount() const;
  size_t MetadataCount() const;
  MediaPipelineResult Status(const std::string& id) const;
  std::string Phase(const std::string& id) const;

 private:
  struct Record {
    int page = 0;
    int context = 0;
    std::shared_ptr<MediaPipeline> pipeline;
    MediaPipelineResult result;
    std::string phase = "starting";
    bool automatic = false, viewport_frames = false, close_when_started = false;
    std::chrono::steady_clock::time_point close_deadline{};
    // A canceled start/stop keeps its actual encoder owner until Joined.
    std::map<int,std::shared_ptr<NativeLocatorHandlerAsyncWork>> handler_ready,handler_stopped;
    std::map<int, Reply> ready;
    std::map<int, Reply> stopped;
  };
  Hooks hooks_;
  mutable std::mutex mutex_;
  std::map<std::string, Record> records_;
  // Paint delivery must not scan retained terminal recording metadata.
  std::map<int, std::string> active_pages_;
  std::map<int, std::vector<std::function<void()>>> page_closes_;
  uint64_t sequence_ = 0;
  bool closing_ = false;
  std::string StartImpl(int owner, int page, int context, MediaPipelineOptions options, Reply ready, bool automatic);
  void Started(const std::string& id, double timestamp);
  void Finished(const std::string& id, const MediaPipelineResult& result);
  std::shared_ptr<MediaPipeline> Active(const std::string& id) const;
  std::shared_ptr<MediaPipeline> ForPage(int page, bool viewport = false) const;
};
}  // namespace suitecut
