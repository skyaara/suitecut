// SPDX-License-Identifier: MIT
// Included inside authored Source; Engine retains actual publisher children.
#if defined(OS_LINUX) || defined(__APPLE__)
  static Dict StreamingResult(const std::string& stream, const suitecut::StreamPipelineResult& result,
                              const std::string& phase, const std::string& client_token) {
    auto info = CefDictionaryValue::Create();
    info->SetString("clientToken", client_token); info->SetString("id", stream); info->SetString("phase",phase!="stopping"&&result.reconnecting?"reconnecting":phase);
    info->SetString("failure", result.failure); info->SetString("diagnostics", result.diagnostics);
    info->SetDouble("firstCapturedTimestampMs",result.first_captured_timestamp_ms);
    info->SetDouble("attemptNumber",static_cast<double>(result.attempt_number));info->SetDouble("reconnectAttempts",static_cast<double>(result.reconnect_attempts));
    info->SetBool("reconnecting",result.reconnecting);info->SetString("lastAttemptFailure",result.last_attempt_failure);
    info->SetDouble("cumulativeSubmittedFrames",static_cast<double>(result.cumulative_submitted_frames));
    info->SetDouble("cumulativeEncodedFrames",static_cast<double>(result.cumulative_encoded_frames));
    info->SetDouble("cumulativeAudioSubmittedFrames",static_cast<double>(result.cumulative_audio_submitted_frames));
    info->SetDouble("cumulativeMuxedBytes",static_cast<double>(result.cumulative_muxed_bytes));
    info->SetDouble("disconnectedVideoFrames",static_cast<double>(result.disconnected_video_frames));
    info->SetDouble("disconnectedAudioFrames",static_cast<double>(result.disconnected_audio_frames));
    auto attempts=CefListValue::Create();
    for(size_t index=0;index<result.attempts.size();++index){
      const auto& recorded=result.attempts[index];auto attempt=CefDictionaryValue::Create();
      attempt->SetDouble("number",static_cast<double>(recorded.number));attempt->SetInt("encoderPid",recorded.encoder_pid);
      attempt->SetDouble("firstTimestampMs",recorded.first_timestamp_ms);attempt->SetDouble("firstCapturedTimestampMs",recorded.first_captured_timestamp_ms);attempt->SetDouble("finishedTimestampMs",recorded.finished_timestamp_ms);
      attempt->SetDouble("submittedFrames",static_cast<double>(recorded.submitted_frames));attempt->SetDouble("encodedFrames",static_cast<double>(recorded.encoded_frames));
      attempt->SetDouble("audioSubmittedFrames",static_cast<double>(recorded.audio_submitted_frames));attempt->SetDouble("muxedBytes",static_cast<double>(recorded.muxed_bytes));
      attempt->SetString("failure",recorded.failure);attempt->SetString("diagnostics",recorded.diagnostics);attempt->SetBool("diagnosticTruncated",recorded.diagnostic_truncated);
      attempt->SetBool("childReaped",recorded.child_reaped);attempt->SetBool("childExitObserved",recorded.child_exit_observed);attempts->SetDictionary(index,attempt);
    }
    info->SetList("attempts",attempts);
    info->SetDouble("firstTimestampMs", result.first_timestamp_ms); info->SetDouble("durationMs", result.duration_ms);
    info->SetDouble("inputFrames", static_cast<double>(result.input_frames));
    info->SetDouble("submittedFrames", static_cast<double>(result.submitted_frames));
    info->SetDouble("heldFrames", static_cast<double>(result.held_frames));
    info->SetDouble("supersededFrames", static_cast<double>(result.superseded_frames));
    info->SetDouble("encodedFrames", static_cast<double>(result.encoded_frames));
    info->SetDouble("audioInputFrames", static_cast<double>(result.audio_input_frames));
    info->SetDouble("audioSubmittedFrames", static_cast<double>(result.audio_submitted_frames));
    info->SetDouble("audioGapFrames", static_cast<double>(result.audio_gap_frames));
    info->SetDouble("audioOverlapFrames", static_cast<double>(result.audio_overlap_frames));
    info->SetDouble("pausedAudioFrames", static_cast<double>(result.paused_audio_frames));
    info->SetDouble("pausedInputAudioFrames", static_cast<double>(result.paused_input_audio_frames));
    info->SetDouble("switchedAudioFrames", static_cast<double>(result.switched_audio_frames));
    info->SetDouble("stoppedAudioFrames", static_cast<double>(result.stopped_audio_frames));
    info->SetDouble("stoppedVideoFrames", static_cast<double>(result.stopped_video_frames));
    info->SetDouble("pausedInputVideoFrames", static_cast<double>(result.paused_input_video_frames));
    info->SetDouble("capturedFrameBytes", static_cast<double>(result.captured_frame_bytes));
    info->SetDouble("peakCapturedFrameBytes", static_cast<double>(result.peak_captured_frame_bytes));
    info->SetDouble("captureLatencyMs", result.capture_latency_ms);
    if(result.video_interval_closure_ms>0)info->SetDouble("videoIntervalClosureMs", result.video_interval_closure_ms);
    info->SetDouble("maxCaptureToPipeMs", result.max_capture_to_pipe_ms);
    info->SetDouble("muxedBytes", static_cast<double>(result.muxed_bytes));
    info->SetBool("encodedProgressObserved", result.encoded_progress_observed);
    info->SetBool("muxedProgressObserved", result.muxed_progress_observed);
    info->SetInt("physicalEncoderPid", result.physical_encoder_pid);
    return info;
  }
  void StreamingCommand(int id, uint64_t generation, Dict message) {
    const auto operation = operations_.Get(id);
    if (!operation || operation->generation != generation) return;
    const auto action = message->GetString("action").ToString();
    CefDictionaryValue::KeyList keys; message->GetKeys(keys);
    const std::set<std::string> common{"version", "id", "pageId", "command", "timeoutMs", "action", "locatorHandlerOrigin"};
    const std::set<std::string> start{"clientToken", "ffmpegPath", "destination", "framesPerSecond", "width", "height", "audio", "videoCodec", "audioCodec", "container", "bitrateKbps", "maxWriteLagMs", "tcpSendBufferBytes", "captureLatencyMs", "reconnect"};
    for (const auto& key : keys) {
      const auto name = key.ToString();
      if (!common.contains(name) && !(action == "start" ? start.contains(name) : name == "streamId" || (action == "select" && name == "sourcePageId"))) {
        Complete(id, false, "Unexpected native streaming option"); return;
      }
    }
    const auto deadline = operation->deadline_ms == 0 ? Clock::time_point{} :
      Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(std::max(0.0, operation->deadline_ms - Now())));
    try {
      auto controls = engine_.streaming();
      if (action == "start") {
        if (message->GetType("clientToken") != VTYPE_STRING || message->GetString("clientToken").ToString().empty() || message->GetString("clientToken").ToString().size() > 128 ||
            message->GetType("ffmpegPath") != VTYPE_STRING || message->GetType("destination") != VTYPE_STRING ||
            message->GetType("width") != VTYPE_INT || message->GetType("height") != VTYPE_INT || message->GetType("audio") != VTYPE_BOOL ||
            (message->GetType("framesPerSecond") != VTYPE_INT && message->GetType("framesPerSecond") != VTYPE_DOUBLE) ||
            message->GetType("videoCodec") != VTYPE_STRING || message->GetType("audioCodec") != VTYPE_STRING ||
            message->GetType("container") != VTYPE_STRING || message->GetType("bitrateKbps") != VTYPE_INT ||
            (message->HasKey("maxWriteLagMs") && message->GetType("maxWriteLagMs") != VTYPE_INT) ||
            (message->HasKey("tcpSendBufferBytes") && message->GetType("tcpSendBufferBytes") != VTYPE_INT)) {
          Complete(id, false, "Invalid native streaming start options"); return;
        }
        suitecut::StreamPipelineOptions options;
        options.launcher_path = CefCommandLine::GetGlobalCommandLine()->GetProgram().ToString();
        options.ffmpeg_path = message->GetString("ffmpegPath"); options.destination = message->GetString("destination");
        options.source_width = capture_width_; options.source_height = capture_height_;
        options.output_width = message->GetInt("width"); options.output_height = message->GetInt("height");
        options.frames_per_second = Number(message, "framesPerSecond"); options.audio = message->GetBool("audio");
        options.video_codec = message->GetString("videoCodec"); options.audio_codec = message->GetString("audioCodec");
        options.container = message->GetString("container"); options.bitrate_kbps = message->GetInt("bitrateKbps");
        options.i420 = i420_; options.start_timestamp_ms = Now(); options.first_frame_deadline = deadline;
        if (message->HasKey("captureLatencyMs")) options.capture_latency = std::chrono::milliseconds(message->GetInt("captureLatencyMs"));
        if (message->HasKey("maxWriteLagMs")) options.max_write_lag = std::chrono::milliseconds(message->GetInt("maxWriteLagMs"));
        if (message->HasKey("tcpSendBufferBytes")) options.tcp_send_buffer_bytes = message->GetInt("tcpSendBufferBytes");
        if(message->HasKey("reconnect")){
          auto policy=message->GetDictionary("reconnect");if(!policy){Complete(id,false,"Invalid native reconnect policy");return;}
          CefDictionaryValue::KeyList fields;policy->GetKeys(fields);const std::set<std::string> allowed{"maxAttempts","initialDelayMs","maxDelayMs","timeoutMs"};
          for(const auto&field:fields)if(!allowed.contains(field.ToString())||policy->GetType(field)!=VTYPE_INT){Complete(id,false,"Invalid native reconnect policy field");return;}
          suitecut::StreamReconnectOptions bounded;
          if(policy->HasKey("maxAttempts"))bounded.max_attempts=static_cast<unsigned>(policy->GetInt("maxAttempts"));
          if(policy->HasKey("initialDelayMs"))bounded.initial_delay=std::chrono::milliseconds(policy->GetInt("initialDelayMs"));
          if(policy->HasKey("maxDelayMs"))bounded.max_delay=std::chrono::milliseconds(policy->GetInt("maxDelayMs"));
          if(policy->HasKey("timeoutMs"))bounded.timeout=std::chrono::milliseconds(policy->GetInt("timeoutMs"));
          options.reconnect=bounded;
        }
        const auto stream=controls->Start(id, page_id_, engine_.contexts()->ContextId(page_id_), std::move(options),
          [self = CefRefPtr<Source>(this), id, generation](const std::string& stream, const suitecut::StreamPipelineResult& result) {
            if (!self->operations_.IsCurrent(id, generation)) return;
            if (!result.failure.empty()) self->Complete(id, false, result.failure.substr(0, 4096));
            else self->Complete(id, true, "", Json(StreamingResult(stream, result, "streaming", self->engine_.streaming()->ClientToken(stream))));
          }, message->GetString("clientToken").ToString());
        // This is the Engine's actual allocated identity, before encoded ACK.
        // It permits native status observation of a real initial outage without
        // guessing stream IDs or extending the original start deadline.
        const auto starting=Json(StreamingResult(stream,controls->Status(stream),"starting",controls->ClientToken(stream)));
        auto packet=Header("streaming");SendPacket(packet,std::vector<uint8_t>(starting.begin(),starting.end()));
        EmitFrame(); browser_->GetHost()->Invalidate(PET_VIEW); return;
      }
      if (message->GetType("streamId") != VTYPE_STRING || message->GetString("streamId").ToString().size() > 128 ||
          !controls->Owns(message->GetString("streamId"), page_id_)) {
        Complete(id, false, "Unknown native stream for this page"); return;
      }
      const auto stream = message->GetString("streamId").ToString();
      if (action == "stop" || action == "cancel") {
        if (action == "cancel") controls->Cancel(stream, "Native stream canceled");
        controls->Stop(id, stream, deadline,
          [self = CefRefPtr<Source>(this), id, generation, action](const std::string& name, const suitecut::StreamPipelineResult& result) {
            if (!self->operations_.IsCurrent(id, generation)) return;
            if (!result.failure.empty() && action != "cancel") self->Complete(id, false, result.failure.substr(0, 4096));
            else self->Complete(id, true, "", Json(StreamingResult(name, result, result.failure.empty() ? "complete" : "failed", self->engine_.streaming()->ClientToken(name))));
          }); return;
      }
      if (action == "pause") controls->Pause(stream, true);
      else if (action == "resume") controls->Pause(stream, false);
      else if (action == "select") {
        if (message->GetType("sourcePageId") != VTYPE_INT) { Complete(id, false, "Native stream source must be a real Engine page"); return; }
        std::string error;
        if (!engine_.SelectStreamingSource(stream, message->GetInt("sourcePageId"), error)) { Complete(id, false, error); return; }
      } else if (action != "status") { Complete(id, false, "Unknown native streaming action"); return; }
      auto result = StreamingResult(stream, controls->Status(stream), controls->Phase(stream), controls->ClientToken(stream));
      result->SetInt("selectedPageId", controls->SelectedPage(stream));
      result->SetDouble("framesPerSecond", controls->FramesPerSecond(stream));
      result->SetDouble("timestampMs", Now()); Complete(id, true, "", Json(result));
    } catch (const std::exception& error) { Complete(id, false, std::string(error.what()).substr(0, 4096)); }
  }
#else
  void StreamingCommand(int id, uint64_t, Dict) { Complete(id, false, "Native streaming subprocess ownership currently requires Linux glibc"); }
#endif
