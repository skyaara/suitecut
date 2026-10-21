// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
[[gnu::noinline]] auto Source::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
                                       const RectList& dirty, const void* buffer, int width,
                                       int height) -> void {
  if (type == PET_VIEW) last_view_delivery_ms_ = Now();
  if (!native_paint_timestamp_) {
    ScreenshotAssertionViewPaint(type, width, height);
    if (ArtifactViewPaint(type, width, height)) {
      (type == PET_VIEW ? view_damage_valid_ : popup_damage_valid_) = false;
      return;
    }
  }
  const double captured = native_paint_timestamp_.value_or(Now());
  if (type == PET_VIEW) {
    native_view_stale_ = false;
#if defined(__APPLE__)
    native_capture_snapshot_.reset();
#endif
  }
  if (width <= 0 || height <= 0 || width > 3840 || height > 2160) {
    (type == PET_VIEW ? view_damage_valid_ : popup_damage_valid_) = false;
    return;
  }
  if (type == PET_VIEW) {
    engine_.TracePaint(page_id_, width_, height_, captured, buffer, width, height);
    if (buffer != view_.data() &&
        !SuiteCutUpdateCapture(view_, buffer, width, height, view_damage_valid_ ? frame_width_ : 0,
                               frame_height_, dirty)) {
      view_damage_valid_ = false;
      return;
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
    if (buffer != popup_.data() &&
        !SuiteCutUpdateCapture(popup_, buffer, width, height,
                               popup_damage_valid_ ? popup_width_ : 0, popup_height_, dirty)) {
      popup_damage_valid_ = false;
      return;
    }
    popup_damage_valid_ = true;
    popup_width_ = width;
    popup_height_ = height;
  }
  EmitFrame(captured, type == PET_VIEW);
}

#if defined(__APPLE__)
auto Source::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                                const RectList& dirty, const CefAcceleratedPaintInfo& info)
    -> void {
  if (type == PET_VIEW) last_view_delivery_ms_ = Now();
  const auto& visible = info.extra.visible_rect;
  const int width = visible.width > 0 ? visible.width : info.extra.coded_size.width;
  const int height = visible.height > 0 ? visible.height : info.extra.coded_size.height;
  ScreenshotAssertionViewPaint(type, width, height);
  if (ArtifactViewPaint(type, width, height)) {
    (type == PET_VIEW ? view_damage_valid_ : popup_damage_valid_) = false;
    return;
  }
  auto& clock = native_paint_clocks_[type == PET_VIEW ? 0 : 1];
  const double native = info.extra.timestamp / 1000.0, now = Now();
  const double captured =
      clock.Map(native, now, engine_.recordings()->CaptureClockFrozen(page_id_),
                info.extra.has_capture_counter ? std::optional<uint64_t>(info.extra.capture_counter)
                                               : std::nullopt,
                engine_.CaptureRate());
  if (timing_trace_ && type == PET_VIEW)
    std::fprintf(
        stderr,
        "CLOCK_VIDEO counter=%llu native=%.6f received=%.6f mapped=%.6f offset=%.6f owned=%d\n",
        static_cast<unsigned long long>(info.extra.capture_counter), native, now, captured,
        clock.Offset(), engine_.recordings()->HasRecording(page_id_));
  ++native_paints_;
  const auto trace_config = engine_.traces()->Configuration(network_context_id_);
  const bool trace_pixels = trace_config && trace_config->screenshots &&
                            engine_.traces()->CaptureLease(network_context_id_).context;
  if (type == PET_VIEW && native_zero_copy_recording_ && !show_popup_ &&
      !frame_transport_enabled_ && !trace_pixels && width == capture_width_ &&
      height == capture_height_ && !engine_.streaming()->NeedsFrame(page_id_) &&
      engine_.recordings()->NeedsFrame(page_id_)) {
    if (!native_capture_pool_) native_capture_pool_ = std::make_unique<suitecut::MacCapturePool>();
    auto surface = native_capture_pool_->Copy(info.shared_texture_io_surface, visible.x, visible.y,
                                              width, height, true);
    const auto snapshot = surface;
    if (surface &&
        engine_.recordings()->NativeCanvasFrame(page_id_, std::move(surface), captured, {})) {
      native_capture_snapshot_ = snapshot;
      native_view_stale_ = true;
      ScreenshotComparisonPaint();
      return;
    }
  }
  auto& pixels = type == PET_VIEW ? view_ : popup_;
  if (!suitecut::CopyMacSurface(info.shared_texture_io_surface, visible.x, visible.y, width, height,
                                pixels)) {
    Error("Native IOSurface paint could not be copied safely");
    return;
  }
  native_paint_timestamp_ = captured;
  OnPaint(browser, type, dirty, pixels.data(), width, height);
  native_paint_timestamp_.reset();
}
#endif

auto Source::OnPopupShow(CefRefPtr<CefBrowser>, bool show) -> void {
  show_popup_ = show;
  if (show && native_view_stale_ && browser_) browser_->GetHost()->Invalidate(PET_VIEW);
  if (!show) {
    popup_.clear();
    EmitFrame();
  }
}

auto Source::OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) -> void { popup_rect_ = rect; }

auto Source::GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters& params) -> bool {
  params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
  params.sample_rate = 48000;
  params.frames_per_buffer = 960;
  return true;
}

auto Source::OnAudioStreamStarted(CefRefPtr<CefBrowser>, const CefAudioParameters& params,
                                  int channels) -> void {
  audio_clock_.Reset();
#if defined(__APPLE__)
  // Audio PTS is process-wide Mach TimeTicks; video PTS is relative to its
  // capture start. Calibrate each to Now(), without anchoring late PCM at
  // delivery. Choose the narrowest bounded clock sample to avoid preemption.
  double width = std::numeric_limits<double>::infinity(), offset = 0;
  for (int i = 0; i < 4; ++i) {
    const double before = Now(), native = suitecut::MacNativeTimeTicksMs(), after = Now();
    if (after - before < width) {
      width = after - before;
      offset = (before + after) / 2 - native;
    }
  }
  native_audio_clock_offset_.store(offset, std::memory_order_relaxed);
#endif
  audio_valid_ = params.sample_rate == 48000 && channels == 2;
  if (!audio_valid_) Error("Unsupported native audio format");
}

auto Source::OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** data, int frames, int64_t pts)
    -> void {
  if (!audio_valid_ || frames < 1 || frames > 4096) return;
  const double received = Now();
  const double epoch =
      std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch())
          .count();
  const double source_offset = native_audio_clock_offset_.load(std::memory_order_relaxed);
  const double timestamp = audio_clock_.Map(
      static_cast<double>(pts), received, epoch,
      std::isfinite(source_offset) ? std::optional<double>(source_offset) : std::nullopt);
  if (timing_trace_) {
    double energy = 0;
    for (int i = 0; i < frames; ++i) energy += data[0][i] * data[0][i];
    std::fprintf(stderr, "CLOCK_AUDIO native=%lld received=%.6f mapped=%.6f offset=%.6f rms=%.6f\n",
                 static_cast<long long>(pts), received, timestamp, source_offset,
                 std::sqrt(energy / frames));
  }
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
#if defined(OS_LINUX) || defined(__APPLE__)
  engine_.streaming()->Audio(page_id_, pcm, timestamp);
#endif
  auto header = Header("audio");
  header->SetDouble("timestampMs", timestamp);
  header->SetInt("frames", frames);
  SendPacket(header, std::move(pcm));
}

auto Source::OnAudioStreamStopped(CefRefPtr<CefBrowser>) -> void { audio_valid_ = false; }

auto Source::OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString&) -> void {
  Error("Native audio capture failed");
}

auto Source::ScheduleRecordingSnapshot(const std::string& id, int delay) -> void {
  if (recording_snapshot_pending_ == id) return;
  recording_snapshot_pending_ = id;
  CefPostDelayedTask(
      TID_UI, new Task([self = CefRefPtr<Source>(this), alive = engine_.Alive(), id] {
        if (!*alive) return;
        if (self->recording_snapshot_pending_ != id) return;
        self->recording_snapshot_pending_.clear();
        auto controls = self->engine_.recordings();
        if (!self->browser_ || !controls->Owns(id, self->page_id_) ||
            controls->Phase(id) != "starting" || controls->Status(id).input_frames)
          return;
        self->browser_->GetHost()->Invalidate(PET_VIEW);
        // A busy animated source must await its capture-time frame, never a
        // retimestamped warmup image. Only a quiet compositor needs a snapshot.
        if (Now() - self->last_view_delivery_ms_ < 100) {
          self->ScheduleRecordingSnapshot(id, 100);
          return;
        }
#if defined(__APPLE__)
        if (self->native_view_stale_ && self->native_capture_snapshot_)
          controls->NativeCanvasFrame(self->page_id_, self->native_capture_snapshot_, Now(), {});
        else
          self->EmitFrame();
#else
        self->EmitFrame();
#endif
      }),
      delay);
}

[[gnu::noinline]] auto Source::EmitFrame(double timestamp, bool clocked_capture) -> void {
  if (native_view_stale_) {
    if (frame_transport_enabled_ && browser_) browser_->GetHost()->Invalidate(PET_VIEW);
    return;
  }
  if (!ready_ || view_.empty()) return;
  const bool recording_viewport = engine_.recordings()->NeedsFrame(page_id_, true);
  bool recording_canvas = engine_.recordings()->NeedsFrame(page_id_);
#if defined(OS_LINUX) || defined(__APPLE__)
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
                  popup_.data() + (y * popup_width_ + start) * 4,
                  static_cast<size_t>(end - start) * 4);
    }
  }
  // Automatic video consumes the genuine composed paint before the legacy
  // stream/manual-recording fit. Its worker owns scaling and current CSS fit.
  if (recording_viewport && frame_css_width_ && frame_css_height_)
    engine_.recordings()->ViewportFrame(page_id_, *composed, frame_width_, frame_height_,
                                        frame_css_width_, frame_css_height_, timestamp);
  // Hand one owned BGRA snapshot to the existing recorder worker. Fitting
  // and color conversion must not consume CEF's next paint budget.
  if (clocked_capture) ++capture_clock_sequence_;
  if (recording_canvas &&
      engine_.recordings()->CanvasFrame(page_id_, *composed, frame_width_, frame_height_, timestamp,
                                        clocked_capture ? capture_clock_sequence_ : 0))
    recording_canvas = false;
  if (!frame_transport_enabled_ && !recording_canvas && !publishing) return;
  if (!needs_canvas) return;
  // Recording dimensions are fixed at launch. Resizing the browser viewport
  // fits its current capture into that canvas, preserving aspect ratio. In
  // particular, odd CSS viewport sizes never reach an even-size I420 writer.
  SuiteCutCaptureRect content_rect;
  if (!SuiteCutCaptureContentRect(frame_width_, frame_height_, capture_width_, capture_height_,
                                  content_rect) ||
      composed->size() != static_cast<size_t>(frame_width_) * frame_height_ * 4) {
    Error("Native capture resize failed");
    return;
  }
  if (frame_width_ != capture_width_ || frame_height_ != capture_height_) {
    std::vector<uint8_t> fitted;
    if (!SuiteCutFitCapture(*composed, frame_width_, frame_height_, capture_width_, capture_height_,
                            fitted)) {
      Error("Native capture resize failed");
      return;
    }
    output = std::move(fitted);
    composed = &output;
  }
  if (i420_) {
    const size_t y_size = static_cast<size_t>(capture_width_) * capture_height_;
    std::vector<uint8_t> planes(y_size * 3 / 2);
    // libyuv's ARGB name describes registers; in little-endian memory its
    // input is BGRA, matching CEF. Use explicit limited-range BT.709.
    if (libyuv::ARGBToI420Matrix(composed->data(), capture_width_ * 4, planes.data(),
                                 capture_width_, planes.data() + y_size, capture_width_ / 2,
                                 planes.data() + y_size * 5 / 4, capture_width_ / 2,
                                 &libyuv::kArgbH709Constants, capture_width_,
                                 capture_height_) != 0) {
      Error("Native pixel conversion failed");
      return;
    }
    output = std::move(planes);
  } else if (composed != &output)
    output = *composed;
  if (recording_canvas) engine_.recordings()->Frame(page_id_, output, timestamp);
#if defined(OS_LINUX) || defined(__APPLE__)
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
  } else
    header->SetNull("viewport");
  auto rect = CefDictionaryValue::Create();
  rect->SetInt("x", content_rect.x);
  rect->SetInt("y", content_rect.y);
  rect->SetInt("width", content_rect.width);
  rect->SetInt("height", content_rect.height);
  header->SetDictionary("contentRect", rect);
  header->SetDouble("timestampMs", timestamp);
  header->SetInt("width", capture_width_);
  header->SetInt("height", capture_height_);
  SendPacket(header, std::move(output));
}

}  // namespace suitecut::browser
