// SPDX-License-Identifier: MIT
#pragma once
#include "protocol/values.h"
#include "accepted_packet_receipt.h"
#include "accepted_publication_audit.h"
#include "command_credits.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
namespace suitecut::browser {
struct Packet {
  std::string header;
  std::vector<uint8_t> data;
  bool reply_credit = false;
  bool event = false;
  suitecut::AcceptedPacketReceipt native_work;
  uint64_t native_watermark = 0;
};

// Never block a CEF callback on Node or an encoder. Pending frames are replaced
// per page and served in arrival order within a 256 MiB aggregate budget.
class Writer {
 public:
  explicit Writer(int output);
  ~Writer();
  void Stop();
  std::shared_ptr<suitecut::AcceptedWriterWork> NativePublication() const noexcept;
  void WorkChanged() noexcept;
  suitecut::AcceptedPublicationAudit& PublicationAudit() noexcept;
  // Source must authenticate one actual Engine barrier before invoking this.
  // This only queues a cutoff packet on original stdout; it grants no idle.
  bool QueueWorkWatermark(std::shared_ptr<suitecut::AcceptedReaderWork> reader,
                          suitecut::NativeStreamCut reader_cut, uint64_t barrier);
  std::optional<std::pair<suitecut::NativeStreamCut, suitecut::NativeStreamCut>>
  WorkWatermarkIdentity(uint64_t barrier);
  bool WorkWatermarkWritten(uint64_t barrier, suitecut::NativeStreamCut reader,
                            suitecut::NativeStreamCut writer);
  std::exception_ptr WorkWatermarkFailure();
  // Call only after the Engine receives the matching authenticated SDK receipt
  // for this original stdout packet; this is not a generic close/idle ACK.
  bool RetireWorkWatermark(uint64_t barrier, suitecut::NativeStreamCut reader,
                           suitecut::NativeStreamCut writer);
  bool ReserveReply(const std::atomic<bool>& canceled);
  void ReleaseReply();
  void WakeReaders();
  void DropPage(int page);
  void Send(Dict header, std::vector<uint8_t> data = {}, bool reply_credit = false);
  // Network binary publication applies backpressure without discarding data.
  bool TryEvent(Dict header, std::vector<uint8_t> data);
  void Reply(int id, bool ok, const std::string& error = "", std::vector<uint8_t> data = {},
             Dict navigation = nullptr);
  void Error(const char* message);

 private:
  struct WorkWatermark {
    uint64_t barrier;
    std::shared_ptr<suitecut::AcceptedReaderWork> reader;
    suitecut::NativeStreamCut reader_cut, writer_cut;
    std::string header;
    bool queued, written;
  };
  Packet TrackedPacket(std::string header, std::vector<uint8_t> data, bool reply_credit, bool event,
                       suitecut::AcceptedWriterWork::Kind kind);
  bool WorkWatermarkReady() const;
  void EnqueueReadyWorkWatermarks();
  void Run();
  std::mutex mutex_;
  suitecut::AcceptedPublicationAudit publication_audit_;
  std::shared_ptr<suitecut::AcceptedWriterWork> publication_ =
      std::make_shared<suitecut::AcceptedWriterWork>();
  std::array<std::optional<WorkWatermark>, 8> watermarks_{};
  std::exception_ptr watermark_failure_;
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

}  // namespace suitecut::browser
