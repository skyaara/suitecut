// SPDX-License-Identifier: MIT
#include "protocol/writer.h"
#include "packet_write.h"
#include "include/cef_parser.h"
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
namespace suitecut::browser {
Writer::Writer(int output) : output_(output) {
  fcntl(output_, F_SETFL, fcntl(output_, F_GETFL) | O_NONBLOCK);
  thread_ = std::thread([this] { Run(); });
}

Writer::~Writer() {
  Stop();
  close(output_);
}

auto Writer::Stop() -> void {
  publication_->Unsupported();
  stopping_ = true;
  reply_credits_.Stop();
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

auto Writer::NativePublication() const noexcept -> std::shared_ptr<suitecut::AcceptedWriterWork> {
  return publication_;
}

auto Writer::WorkChanged() noexcept -> void { cv_.notify_one(); }

auto Writer::PublicationAudit() noexcept -> suitecut::AcceptedPublicationAudit& {
  return publication_audit_;
}

auto Writer::QueueWorkWatermark(std::shared_ptr<suitecut::AcceptedReaderWork> reader,
                                suitecut::NativeStreamCut reader_cut, uint64_t barrier) -> bool {
  std::lock_guard lock(mutex_);
  if (stopping_ || !reader || !reader->KnownCut(reader_cut) || !barrier) {
    publication_->Unsupported();
    return false;
  }
  for (const auto& value : watermarks_)
    if (value && value->barrier == barrier) {
      publication_->Unsupported();
      return false;
    }
  const auto cut = publication_->Cut();
  if (!cut.nonce) return false;
  try {
    auto header = Header("workwatermark");
    header->SetInt("bytes", 0);
    header->SetString("barrier", std::to_string(barrier));
    header->SetString("readerInstance", std::to_string(reader_cut.instance));
    header->SetString("readerThrough", std::to_string(reader_cut.sequence));
    header->SetString("readerCut", std::to_string(reader_cut.nonce));
    header->SetString("writerInstance", std::to_string(cut.instance));
    header->SetString("writerThrough", std::to_string(cut.sequence));
    header->SetString("writerCut", std::to_string(cut.nonce));
    WorkWatermark created{barrier, std::move(reader), reader_cut, cut, Json(header), false, false};
    // One slot per live barrier; a retired slot is reused, so the table only
    // grows with barriers that are actually outstanding.
    auto slot = std::find_if(watermarks_.begin(), watermarks_.end(),
                             [](const auto& value) { return !value; });
    if (slot == watermarks_.end())
      watermarks_.emplace_back(std::move(created));
    else
      *slot = std::move(created);
    cv_.notify_one();
    return true;
  } catch (...) {
    publication_->Unsupported();
    publication_->ReleaseCut(cut);
    throw;
  }
}

auto Writer::WorkWatermarkIdentity(uint64_t barrier)
    -> std::optional<std::pair<suitecut::NativeStreamCut, suitecut::NativeStreamCut>> {
  std::lock_guard lock(mutex_);
  for (const auto& value : watermarks_)
    if (value && value->barrier == barrier) return std::pair{value->reader_cut, value->writer_cut};
  return std::nullopt;
}

auto Writer::WorkWatermarkWritten(uint64_t barrier, suitecut::NativeStreamCut reader,
                                  suitecut::NativeStreamCut writer) -> bool {
  std::lock_guard lock(mutex_);
  for (const auto& value : watermarks_)
    if (value && value->barrier == barrier && value->reader_cut == reader &&
        value->writer_cut == writer)
      return !watermark_failure_ && value->written && value->reader->Through(reader) &&
             publication_->Through(writer);
  return false;
}

auto Writer::WorkWatermarkFailure() -> std::exception_ptr {
  std::lock_guard lock(mutex_);
  return watermark_failure_;
}

auto Writer::RetireWorkWatermark(uint64_t barrier, suitecut::NativeStreamCut reader,
                                 suitecut::NativeStreamCut writer) -> bool {
  std::lock_guard lock(mutex_);
  for (auto& value : watermarks_)
    if (value && value->barrier == barrier && value->reader_cut == reader &&
        value->writer_cut == writer) {
      if (watermark_failure_ || !value->written || !value->reader->Through(reader) ||
          !publication_->Through(writer))
        return false;
      const bool readers = value->reader->ReleaseCut(reader),
                 writers = publication_->ReleaseCut(writer);
      if (!readers || !writers) {
        publication_->Unsupported();
        return false;
      }
      value.reset();
      return true;
    }
  return false;
}

auto Writer::ReserveReply(const std::atomic<bool>& canceled) -> bool {
  return reply_credits_.Reserve(canceled);
}

auto Writer::ReleaseReply() -> void { reply_credits_.Release(); }

auto Writer::WakeReaders() -> void { reply_credits_.Wake(); }

auto Writer::DropPage(int page) -> void {
  std::lock_guard lock(mutex_);
  const auto found = frames_.find(page);
  if (found != frames_.end()) {
    found->second.native_work.Dropped(suitecut::AcceptedWriterWork::Discard::PageDropped);
    frames_.erase(found);
  }
  std::erase(frame_order_, page);
  dropped_.erase(page);
  std::erase_if(audio_, [page](Packet& packet) {
    auto parsed = CefParseJSON(packet.header, JSON_PARSER_RFC);
    const bool discard = parsed && parsed->GetDictionary()->GetInt("pageId") == page;
    if (discard) packet.native_work.Dropped(suitecut::AcceptedWriterWork::Discard::PageDropped);
    return discard;
  });
}

auto Writer::Send(Dict header, std::vector<uint8_t> data, bool reply_credit) -> void {
  header->SetInt("bytes", static_cast<int>(data.size()));
  std::lock_guard lock(mutex_);
  if (stopping_) return;
  const auto type = header->GetString("type").ToString();
  if (type == "frame") {
    const int page = header->GetInt("pageId");
    if (const auto found = frames_.find(page); found != frames_.end()) {
      ++dropped_[page];
      found->second.native_work.Dropped(suitecut::AcceptedWriterWork::Discard::FrameReplaced);
      frames_.erase(found);
    } else
      frame_order_.push_back(page);
    header->SetDouble("dropped", static_cast<double>(dropped_[page]));
    frames_[page] = TrackedPacket(Json(header), std::move(data), false, false,
                                  suitecut::AcceptedWriterWork::Kind::Frame);
  } else if (type == "audio") {
    audio_.push_back(TrackedPacket(Json(header), std::move(data), false, false,
                                   suitecut::AcceptedWriterWork::Kind::Audio));
  } else {
    const auto json = Json(header);
    if (!reply_credit) {
      // Events queue whole however long the host takes to read them.
      ++event_count_;
      event_bytes_ += json.size() + data.size();
    }
    publication_audit_.Published(type);
    auto packet = TrackedPacket(json, std::move(data), reply_credit, !reply_credit,
                                reply_credit ? suitecut::AcceptedWriterWork::Kind::Control
                                             : suitecut::AcceptedWriterWork::Kind::Event);
    control_.push_back(std::move(packet));
  }
  cv_.notify_one();
}

auto Writer::TryEvent(Dict header, std::vector<uint8_t> data) -> bool {
  header->SetInt("bytes", static_cast<int>(data.size()));
  const auto json = Json(header);
  std::lock_guard lock(mutex_);
  const auto bytes = json.size() + data.size();
  // Backpressure only: delay this producer while other unread events are
  // backed up past the thresholds. An empty backlog takes any size.
  if (stopping_ || (event_count_ && (event_count_ >= kEventBackpressure ||
                                     event_bytes_ >= kEventBackpressureBytes ||
                                     bytes > kEventBackpressureBytes - event_bytes_)))
    return false;
  publication_audit_.Published(header->GetString("type").ToString());
  ++event_count_;
  event_bytes_ += bytes;
  control_.push_back(
      TrackedPacket(json, std::move(data), false, true, suitecut::AcceptedWriterWork::Kind::Event));
  cv_.notify_one();
  return true;
}

auto Writer::Reply(int id, bool ok, const std::string& error, std::vector<uint8_t> data,
                   Dict navigation) -> void {
  auto header = Header("response");
  header->SetInt("id", id);
  header->SetBool("ok", ok);
  if (navigation) header->SetDictionary("navigation", navigation);
  if (!error.empty()) header->SetString("error", error);
  Send(header, std::move(data), true);
}

auto Writer::Error(const char* message) -> void {
  auto header = Header("error");
  header->SetString("error", message);
  Send(header);
}

auto Writer::TrackedPacket(std::string header, std::vector<uint8_t> data, bool reply_credit,
                           bool event, suitecut::AcceptedWriterWork::Kind kind) -> Packet {
  if (header.size() > SIZE_MAX - 4 || data.size() > SIZE_MAX - 4 - header.size()) {
    publication_->Unsupported();
    return {std::move(header), std::move(data), reply_credit, event, {}};
  }
  const auto bytes = 4 + header.size() + data.size();
  return {std::move(header), std::move(data), reply_credit, event,
          suitecut::AcceptedPacketReceipt(publication_, kind, bytes)};
}

auto Writer::WorkWatermarkReady() const -> bool {
  if (watermark_failure_) return false;
  for (const auto& value : watermarks_)
    if (value && !value->queued && value->reader->Through(value->reader_cut) &&
        publication_->Through(value->writer_cut))
      return true;
  return false;
}

auto Writer::EnqueueReadyWorkWatermarks() -> void {
  if (watermark_failure_) return;
  for (auto& value : watermarks_)
    if (value && !value->queued && value->reader->Through(value->reader_cut) &&
        publication_->Through(value->writer_cut)) {
      try {
        auto packet = TrackedPacket(value->header, {}, false, false,
                                    suitecut::AcceptedWriterWork::Kind::Watermark);
        packet.native_watermark = value->barrier;
        control_.push_front(std::move(packet));
        value->queued = true;
      } catch (...) {
        publication_->Unsupported();
        watermark_failure_ = std::current_exception();
        return;
      }
    }
}

auto Writer::Run() -> void {
  while (!stopping_) {
    Packet packet;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] {
        return stopping_ || !control_.empty() || !audio_.empty() || !frames_.empty() ||
               WorkWatermarkReady();
      });
      if (stopping_) break;
      EnqueueReadyWorkWatermarks();
      if (!control_.empty()) {
        packet = std::move(control_.front());
        control_.pop_front();
        if (packet.event) {
          --event_count_;
          event_bytes_ -= packet.header.size() + packet.data.size();
        }
      } else if (!audio_.empty()) {
        packet = std::move(audio_.front());
        audio_.pop_front();
      } else {
        const int page = frame_order_.front();
        frame_order_.pop_front();
        packet = std::move(frames_.at(page));
        frames_.erase(page);
      }
      packet.native_work.Dequeued();
    }
    if (!suitecut::WriteNativePacket(output_, packet.header, packet.data, stopping_,
                                     packet.native_work))
      break;
    const bool published = packet.native_work.FullWrite();
    if (packet.native_watermark) {
      std::lock_guard lock(mutex_);
      for (auto& value : watermarks_)
        if (value && value->barrier == packet.native_watermark) {
          if (published)
            value->written = true;
          else
            publication_->Unsupported();
        }
    }
    if (packet.reply_credit) reply_credits_.Release();
  }
}

}  // namespace suitecut::browser
