// SPDX-License-Identifier: MIT
#include "stream_pipeline.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main() {
  suitecut::StreamPipelineOptions options;
  options.launcher_path = "/unused-launcher";
  options.ffmpeg_path = "/unused-encoder";
  options.destination = "tcp://127.0.0.1:1";
  options.source_width = options.source_height = 64;
  options.output_width = options.output_height = 64;
  options.start_timestamp_ms = 1000;
  options.first_frame_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  {
    suitecut::StreamPipeline stream(options);
    stream.Frame(std::vector<uint8_t>(64 * 64 * 4), 999);
    const auto stale = stream.Snapshot();
    assert(stale.failure.empty() && stale.input_frames == 1 && stale.superseded_frames == 1);
    assert(stale.physical_encoder_pid == 0 && stale.captured_frame_bytes == 0);
    stream.Cancel("controlled shutdown after valid pre-start capture");
    stream.Join();
    assert(stream.Snapshot().failure == "controlled shutdown after valid pre-start capture");
  }
  {
    suitecut::StreamPipeline stream(options);
    stream.Frame(std::vector<uint8_t>(7), 1000);
    stream.Join();
    assert(stream.Snapshot().failure == "Malformed native publisher capture frame");
  }
  std::cout << "Valid pre-start capture is discarded; malformed bytes still fail; no encoder spawned\n";
}
