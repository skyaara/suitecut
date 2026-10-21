// SPDX-License-Identifier: MIT
// Included inside the authored Source class. Engine owns physical workers.
  static Dict RecordingResult(const std::string& recording, const suitecut::MediaPipelineResult& result,
                              const std::string& phase) {
    auto info = CefDictionaryValue::Create();
    info->SetString("id", recording); info->SetString("phase", phase);
    info->SetString("outputPath", result.output_path);
    info->SetString("failure", result.failure); info->SetString("diagnostics", result.diagnostics);
    info->SetString("encodingBackend", result.encoding_backend);
    info->SetString("encoder", result.encoder); info->SetString("container", result.container);
    info->SetDouble("preparationFrames", static_cast<double>(result.preparation_frames));
    info->SetDouble("preparationPackets", static_cast<double>(result.preparation_packets));
    info->SetDouble("preparationMs", result.preparation_ms);
    info->SetDouble("firstTimestampMs", result.first_timestamp_ms);
    info->SetDouble("durationMs", result.duration_ms);
    info->SetDouble("encodedDurationMs", result.encoded_duration_ms);
    info->SetDouble("framesPerSecond", result.frames_per_second);
    info->SetDouble("inputFrames", static_cast<double>(result.input_frames));
    info->SetDouble("outputFrames", static_cast<double>(result.output_frames));
    info->SetDouble("heldFrames", static_cast<double>(result.held_frames));
    info->SetDouble("coalescedFrames", static_cast<double>(result.coalesced_frames));
    info->SetDouble("gpuNv12Frames", result.gpu_nv12_frames);
    info->SetDouble("gpuInputFrames", result.gpu_input_frames);
    info->SetBool("captureTimestampsExact", result.capture_timestamps_exact);
    info->SetDouble("captureLookaheadMs", result.capture_lookahead_ms);
    info->SetDouble("maxTimestampCorrectionMs", result.max_timestamp_correction_ms);
    info->SetDouble("supersededFrames", static_cast<double>(result.superseded_frames));
    info->SetDouble("audioInputFrames", static_cast<double>(result.audio_input_frames));
    info->SetDouble("audioOutputFrames", static_cast<double>(result.audio_output_frames));
    info->SetDouble("audioOverlapFrames", static_cast<double>(result.audio_overlap_frames));
    info->SetDouble("audioGapFrames", static_cast<double>(result.audio_gap_frames));
    info->SetDouble("outputBytes", static_cast<double>(result.output_bytes));
    return info;
  }
  void RecordingCommand(int id, uint64_t generation, Dict message) {
    const auto operation = operations_.Get(id);
    if (!operation || operation->generation != generation) return;
    const auto action = message->GetString("action").ToString();
    CefDictionaryValue::KeyList keys; message->GetKeys(keys);
    const std::set<std::string> common{"version", "id", "pageId", "command", "timeoutMs", "action"};
    const std::set<std::string> start{"ffmpegPath", "outputPath", "framesPerSecond", "width", "height", "audio", "maxFileBytes"};
    for (const auto& key : keys) {
      const auto name = key.ToString();
      if (!common.contains(name) && !(action == "start" ? start.contains(name) : name == "recordingId")) {
        Complete(id, false, "Unexpected native recording option"); return;
      }
    }
    const auto deadline = operation->deadline_ms == 0 ? Clock::time_point{} :
        Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(std::max(0.0, operation->deadline_ms - Now())));
    try {
      auto controls = engine_.recordings();
      if (action == "start") {
        if (message->GetType("ffmpegPath") != VTYPE_STRING || message->GetType("outputPath") != VTYPE_STRING ||
            message->GetType("width") != VTYPE_INT || message->GetType("height") != VTYPE_INT ||
            (message->GetType("framesPerSecond") != VTYPE_INT && message->GetType("framesPerSecond") != VTYPE_DOUBLE) ||
            message->GetType("audio") != VTYPE_BOOL ||
            (message->HasKey("maxFileBytes") && message->GetType("maxFileBytes") != VTYPE_INT)) {
          Complete(id, false, "Invalid native recording start options"); return;
        }
        suitecut::MediaPipelineOptions options;
        options.launcher_path = CefCommandLine::GetGlobalCommandLine()->GetProgram().ToString();
        options.ffmpeg_path = message->GetString("ffmpegPath"); options.output_path = message->GetString("outputPath");
        options.source_width = capture_width_; options.source_height = capture_height_;
        options.output_width = message->GetInt("width"); options.output_height = message->GetInt("height");
        options.frames_per_second = Number(message, "framesPerSecond"); options.audio = message->GetBool("audio");
        options.capture_frames_per_second = std::atoi(CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-fps").ToString().c_str());
#if defined(__APPLE__)
        // Select the configured capture path even when navigation completes
        // before the first accelerated paint. Paint arrival must not race the
        // recorder into a different encoder backend.
        options.capture_timestamps_exact = CefCommandLine::GetGlobalCommandLine()->GetSwitchValue("suitecut-shared-texture")!="0";
        options.hardware_h264 = std::filesystem::path(options.output_path).extension() == ".mp4";
#endif
        options.i420 = i420_; options.start_timestamp_ms = Now(); options.first_frame_deadline = deadline;
#if defined(__APPLE__)
        options.i420 = false; options.worker_canvas_fit = true;
        options.worker_nv12 = capture_width_ % 2 == 0 && capture_height_ % 2 == 0;
#endif
#if defined(__APPLE__)
        native_zero_copy_recording_=options.hardware_h264&&options.worker_nv12&&options.capture_timestamps_exact&&options.source_width==options.output_width&&options.source_height==options.output_height;
#endif
        if (message->HasKey("maxFileBytes")) options.max_file_bytes = static_cast<uint64_t>(message->GetInt("maxFileBytes"));
        controls->Start(id, page_id_, engine_.contexts()->ContextId(page_id_), std::move(options),
          [self = CefRefPtr<Source>(this), id, generation](const std::string& recording, const suitecut::MediaPipelineResult& result) {
            if (!self->operations_.IsCurrent(id, generation)) return;
            if (!result.failure.empty()) self->Complete(id, false, result.failure.substr(0, 4096));
            else self->Complete(id, true, "", Json(RecordingResult(recording, result, "recording")));
          });
        // The existing fitted view is a genuine held capture; a fresh OSR paint
        // follows. Neither path manufactures an image or changes browser FPS.
        EmitFrame(); browser_->GetHost()->Invalidate(PET_VIEW);
        return;
      }
      if (message->GetType("recordingId") != VTYPE_STRING || message->GetString("recordingId").ToString().size() > 128 ||
          !controls->Owns(message->GetString("recordingId"), page_id_)) {
        Complete(id, false, "Unknown native recording for this page"); return;
      }
      const auto recording = message->GetString("recordingId").ToString();
      if (action == "stop" || action == "cancel") {
        if (action == "cancel") controls->Cancel(recording, "Native recording canceled");
        controls->Stop(id, recording, Now(), deadline,
          [self = CefRefPtr<Source>(this), id, generation, action](const std::string& name, const suitecut::MediaPipelineResult& result) {
            if (!self->operations_.IsCurrent(id, generation)) return;
            if (!result.failure.empty() && action != "cancel") self->Complete(id, false, result.failure.substr(0, 4096));
            else self->Complete(id, true, "", Json(RecordingResult(name, result, result.failure.empty() ? "complete" : "failed")));
          });
        return;
      }
      if (action == "pause") controls->Pause(recording, Now());
      else if (action == "resume") controls->Resume(recording, Now());
      else if (action != "status") { Complete(id, false, "Unknown native recording action"); return; }
      auto result = RecordingResult(recording, controls->Status(recording), controls->Phase(recording));
      result->SetDouble("timestampMs", Now()); Complete(id, true, "", Json(result));
    } catch (const std::exception& error) { Complete(id, false, std::string(error.what()).substr(0, 4096)); }
  }
