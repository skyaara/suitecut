// SPDX-License-Identifier: MIT
#import <AudioToolbox/AudioToolbox.h>
#include "audio_monitor.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>

namespace suitecut {
namespace {
constexpr int kRate = 48000;
constexpr size_t kFrameBytes = 4;                     // stereo s16
constexpr size_t kBufferBytes = 480 * kFrameBytes;    // 10 ms per queue buffer
constexpr size_t kMaxQueued = kRate / 5 * kFrameBytes;  // 200 ms
constexpr int kBuffers = 3;
}  // namespace

struct AudioMonitor::Impl {
  AudioQueueRef queue = nullptr;
  std::mutex mutex;
  std::deque<uint8_t> pending;

  static void Fill(void* self, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    auto* impl = static_cast<Impl*>(self);
    auto* out = static_cast<uint8_t*>(buffer->mAudioData);
    size_t taken = 0;
    {
      std::lock_guard lock(impl->mutex);
      taken = std::min(kBufferBytes, impl->pending.size());
      std::copy_n(impl->pending.begin(), taken, out);
      impl->pending.erase(impl->pending.begin(), impl->pending.begin() + static_cast<std::ptrdiff_t>(taken));
    }
    std::memset(out + taken, 0, kBufferBytes - taken);  // underrun plays silence
    buffer->mAudioDataByteSize = kBufferBytes;
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  }
};

AudioMonitor::AudioMonitor() : impl_(std::make_unique<Impl>()) {
  AudioStreamBasicDescription format{};
  format.mSampleRate = kRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 16;
  format.mBytesPerFrame = kFrameBytes;
  format.mFramesPerPacket = 1;
  format.mBytesPerPacket = kFrameBytes;
  if (AudioQueueNewOutput(&format, &Impl::Fill, impl_.get(), nullptr, nullptr, 0, &impl_->queue) != noErr) {
    impl_->queue = nullptr;
    return;
  }
  for (int i = 0; i < kBuffers; ++i) {
    AudioQueueBufferRef buffer = nullptr;
    if (AudioQueueAllocateBuffer(impl_->queue, kBufferBytes, &buffer) != noErr) continue;
    std::memset(buffer->mAudioData, 0, kBufferBytes);
    buffer->mAudioDataByteSize = kBufferBytes;
    AudioQueueEnqueueBuffer(impl_->queue, buffer, 0, nullptr);
  }
  if (AudioQueueStart(impl_->queue, nullptr) != noErr) {
    AudioQueueDispose(impl_->queue, true);
    impl_->queue = nullptr;
  }
}

AudioMonitor::~AudioMonitor() {
  if (impl_->queue) AudioQueueDispose(impl_->queue, true);  // stops synchronously before Impl goes
}

bool AudioMonitor::Ok() const { return impl_->queue != nullptr; }

void AudioMonitor::Play(const std::vector<uint8_t>& pcm) {
  if (!impl_->queue) return;
  std::lock_guard lock(impl_->mutex);
  impl_->pending.insert(impl_->pending.end(), pcm.begin(), pcm.end());
  if (impl_->pending.size() > kMaxQueued)
    impl_->pending.erase(impl_->pending.begin(),
                         impl_->pending.begin() + static_cast<std::ptrdiff_t>(impl_->pending.size() - kMaxQueued));
}
}  // namespace suitecut
