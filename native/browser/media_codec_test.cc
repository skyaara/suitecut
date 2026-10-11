// SPDX-License-Identifier: MIT
#include "media_engine.h"
#include "media_file.h"
#include "media_render.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
void Reject(suitecut::MediaOptions options, const char* expected) {
  try {
    suitecut::MediaOutput output(options, [] { return 0.0; });
    throw std::logic_error("Invalid codec configuration was accepted");
  } catch (const std::runtime_error& error) {
    Require(std::string(error.what()).find(expected) != std::string::npos, error.what());
  }
}
}

int main() {
  const char* saved = std::getenv("SUITECUT_CODEC_TEST_OUTPUT");
  const auto directory = saved ? std::filesystem::path(saved) :
      std::filesystem::temp_directory_path() / ("suitecut-codecs-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  try {
    const auto encoders = suitecut::ListMediaEncoders();
    Require(encoders.size() >= 14, "Missing native codec capabilities");
    struct Case { const char *video, *audio, *container, *decoded; };
    const Case cases[] = {
      {"libx264", "aac", "mp4", "h264"},
      {"libx265", "aac", "mp4", "hevc"},
      {"libvpx", "libopus", "webm", "vp8"},
      {"libvpx-vp9", "libopus", "webm", "vp9"},
      {"libsvtav1", "aac", "mp4", "av1"},
      {"libaom-av1", "aac", "mp4", "av1"},
      {"ffv1", "flac", "mkv", "ffv1"},
      {"ffv1", "pcm_s16le", "mkv", "ffv1"},
      {"libx265", "aac", "mov", "hevc"},
      {"libaom-av1", "libopus", "webm", "av1"},
      {"libvpx-vp9", "flac", "mkv", "vp9"},
      {"libx264", "pcm_s16le", "mov", "h264"},
    };
    const char* repeats = std::getenv("SUITECUT_CODEC_TEST_REPETITIONS");
    const int repetitions = repeats ? std::stoi(repeats) : 1;
    Require(repetitions >= 1 && repetitions <= 100, "Invalid codec test repetition count");
    for (int iteration = 0; iteration < repetitions; ++iteration) {
    for (const auto& test : cases) {
      if (!suitecut::EncoderAvailable(test.video) || !suitecut::EncoderAvailable(test.audio)) {
        std::cout << "SKIP " << test.video << "/" << test.audio << ": encoder not linked\n";
        continue;
      }
      suitecut::MediaOptions options;
      options.destination = (directory / (std::string(test.video) + "-" + test.audio + "." + test.container)).string();
      options.video_codec = test.video; options.audio_codec = test.audio;
      options.width = 320; options.height = 180; options.offline = true;
      std::atomic<double> clock{0};
      suitecut::MediaOutput output(options, [&] { return clock.load(); });
      // Entire synthetic audio is available before the offline video frontier advances.
      auto samples = std::make_shared<std::vector<int16_t>>(48000 * 2);
      for (size_t i = 0; i < 48000; ++i)
        (*samples)[2*i] = (*samples)[2*i+1] = static_cast<int16_t>(12000 * std::sin(i*2*3.141592653589793*440/48000));
      if (saved) {
        std::ofstream reference(directory / "reference.s16", std::ios::binary);
        reference.write(reinterpret_cast<const char*>(samples->data()), samples->size()*sizeof(int16_t));
      }
      output.Audio(samples, 0);
      for (int frame = 0; frame < 30; ++frame) {
        auto pixels = std::make_shared<std::vector<uint8_t>>(320*180*3/2, 128);
        std::fill_n(pixels->begin(), 320*180, static_cast<uint8_t>(16 + frame));
        clock = frame * 1000.0 / 30;
        output.Video(pixels, 320, 180, clock.load());
      }
      clock = 1000;
      const auto status = output.Stop();
      Require(status.error.empty(), std::string(test.video) + ": " + status.error);
      Require(status.video_frames == 30 && status.audio_frames >= 48000 &&
          !status.queued_audio_bytes && !status.queued_video_bytes && !status.audio_dropped, "Codec counters/cleanup are incorrect");
      if (std::string(test.audio) == "flac" || std::string(test.audio) == "pcm_s16le")
        Require(status.audio_frames == 48000, "Lossless audio extended the input with padding");
      const auto probe = suitecut::ProbeMedia(options.destination);
      Require(probe.width == 320 && probe.height == 180 && probe.audio, "Codec output metadata is incorrect");
      Require(probe.streams[0].codec == test.decoded, "Unexpected encoded video codec");
      Require(output.Stop().video_frames == status.video_frames, "Codec stop is not idempotent");
      // Also exercise the native decoder/filter/encoder job for each codec.
      suitecut::RenderOptions render;
      render.inputs = {options.destination}; render.destination = options.destination + ".render." + test.container;
      render.container = std::string(test.container) == "mkv" ? "matroska" : test.container;
      render.video_codec = test.video; render.audio_codec = test.audio; render.audio = true; render.duration_ms = 500;
      render.graph = "[0:v]trim=duration=0.5,setpts=PTS-STARTPTS,fps=30,format=yuv420p[video0];[0:a]atrim=duration=0.5,asetpts=PTS-STARTPTS,aresample=48000[audioout]";
      if (std::string(test.video) == "libx264" || std::string(test.video) == "libx265") render.video_options = {{"preset","ultrafast"},{"crf","23"}};
      if (std::string(test.video) == "libsvtav1") render.video_options = {{"preset","12"},{"crf","32"}};
      if (std::string(test.video) == "libaom-av1") render.video_options = {{"cpu-used","8"},{"crf","32"},{"lag-in-frames","0"}};
      const auto rendered = suitecut::RenderMedia(render, [] { return false; });
      // WebM's millisecond time base rounds the 6.5ms Opus pre-skip.
      const auto minimum_audio = std::string(test.audio) == "libopus" ? 23952 : 24000;
      Require(rendered.video_frames == 15 && rendered.audio_samples >= minimum_audio,
          std::string(test.video) + " rendering lost media: video=" + std::to_string(rendered.video_frames) + " audio=" + std::to_string(rendered.audio_samples));
      std::cout << "PASS " << test.video << "/" << test.audio << ": 30 captured + 15 rendered frames; render audio=" << rendered.audio_samples << '\n';
    }
    std::ifstream memory("/proc/self/status");
    for (std::string line; std::getline(memory,line);)
      if (line.starts_with("VmRSS:") || line.starts_with("Threads:"))
        std::cout << "ITERATION " << iteration + 1 << ": " << line << '\n';
    std::cout.flush();
    }
    // Exercise allocation, slice-worker ownership and cleanup at the new 4K
    // policy in sanitizer runs, without requiring a browser or a long capture.
    for(int iteration=0;iteration<3;++iteration) {
      suitecut::MediaOptions parallel;parallel.width=3840;parallel.height=2160;
      parallel.audio=false;parallel.offline=true;
      parallel.destination=(directory/("4k-slices-"+std::to_string(iteration)+".mp4")).string();
      suitecut::MediaOutput output(parallel,[] {return 34.0;});
      output.Video(std::make_shared<const std::vector<uint8_t>>(16*16*3/2,128),16,16,0);
      const auto status=output.Stop();
      Require(status.error.empty() && status.video_frames==2 && status.video_encoder_threads==3 && !status.queued_video_bytes,"4K parallel encoder failed to encode and release ownership");
      const auto probe=suitecut::ProbeMedia(parallel.destination);
      Require(probe.width==3840 && probe.height==2160 && !probe.audio,"4K parallel output metadata is incorrect");
    }
    std::cout<<"PASS three 4K slice-encoder allocation/encode/cleanup cycles\n";
    suitecut::MediaOptions invalid; invalid.destination = (directory / "invalid.webm").string();
    Reject(invalid, "incompatible");
    Require(!std::filesystem::exists(invalid.destination), "Invalid configuration created an output file");
    invalid.destination = (directory / "invalid.mp4").string(); invalid.crf = 64; Reject(invalid, "CRF");
    invalid.crf = -1; invalid.container = "unknown"; Reject(invalid, "container");
    invalid.container.clear(); invalid.streaming = true; invalid.destination = "rtmp://127.0.0.1:1/live/test";
    invalid.video_codec = "libx265"; Reject(invalid, "RTMP requires");
    invalid.streaming = false; invalid.destination = (directory / "invalid.mkv").string(); invalid.video_codec = "ffv1"; invalid.crf = 18; Reject(invalid, "CRF");
    invalid.crf = -1; invalid.audio_codec = "libx264"; Reject(invalid, "audio encoder");
    if (!saved) std::filesystem::remove_all(directory);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (!saved) std::filesystem::remove_all(directory);
    return 1;
  }
}
