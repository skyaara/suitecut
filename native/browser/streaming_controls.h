// SPDX-License-Identifier: MIT
#pragma once
#include "locator_handler_async_work.h"
#include "stream_pipeline.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
namespace suitecut {
// One Engine retains publisher processes even after an originating CEF page
// logically closes. Selected pages must be actual pages in this same Engine.
class StreamingControls : public std::enable_shared_from_this<StreamingControls> {
 public:
  using Reply = std::function<void(const std::string&, const StreamPipelineResult&)>;
  struct Hooks {
    std::function<void(std::function<void()>)> dispatch;
    std::function<void(int owner_page, const std::string&, const std::string&, const StreamPipelineResult&)> event;
    std::function<void(int owner_context, int selected_context)> physical_settled;
  };
  explicit StreamingControls(Hooks hooks);
  ~StreamingControls();
  std::string Start(int command, int page, int context, StreamPipelineOptions options, Reply ready, std::string client_token = {});
  void Stop(int command, const std::string&, std::chrono::steady_clock::time_point deadline, Reply);
  void Pause(const std::string&, bool);
  void Select(const std::string&, int page, int context, int width, int height, bool i420);
  void Cancel(const std::string&, const std::string& reason);
  void CancelCommand(int command);
  bool NeedsFrame(int page) const;
  void Frame(int page, const std::vector<uint8_t>&, double timestamp);
  void Audio(int page, const std::vector<uint8_t>&, double timestamp);
  void BeforePageClose(int page, std::chrono::steady_clock::time_point deadline, std::function<void()> continuation);
  void ClosePage(int page);
  void FailPage(int page);
  void RecoverPage(int page);
  void PageGone(int page);
  void ContextGone(int context);
  void FinalizeContext(int context, std::chrono::steady_clock::time_point deadline);
  void FinalizeAll(std::chrono::steady_clock::time_point deadline);
  void CloseAll();
  bool Owns(const std::string&, int page) const;
  bool ContextReady(int context) const;
  size_t PhysicalCount() const;
  size_t MetadataCount() const;
  std::string ClientToken(const std::string&) const;
  int SelectedPage(const std::string&) const;
  double FramesPerSecond(const std::string&) const;
  std::string Phase(const std::string&) const;
  StreamPipelineResult Status(const std::string&) const;
 private:
  struct Record {
    int owner_page = 0, owner_context = 0, selected_page = 0, selected_context = 0;
    int width = 0, height = 0; bool i420 = false;
    double frames_per_second = 0;
    std::string destination, client_token;
    std::string phase = "starting";
    std::shared_ptr<StreamPipeline> pipeline;
    StreamPipelineResult result, queued_startup;
    bool startup_queued = false;
    std::map<int,std::shared_ptr<NativeLocatorHandlerAsyncWork>> handler_ready,handler_stopped;
    std::map<int, Reply> ready, stopped;
  };
  mutable std::mutex mutex_;
  Hooks hooks_;
  std::map<std::string, Record> records_;
  std::map<int, std::vector<std::function<void()>>> page_closes_;
  std::set<int> closing_pages_, closing_contexts_, failed_pages_;
  uint64_t sequence_ = 0;
  bool closing_ = false;
  std::shared_ptr<StreamPipeline> Active(const std::string&) const;
  void QueueStarted(const std::string&, const StreamPipelineResult&);
  void Started(const std::string&);
  void Finished(const std::string&, const StreamPipelineResult&);
};
}  // namespace suitecut
