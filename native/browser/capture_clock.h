// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <optional>

// CEF video timestamps are relative to a capture stream. A navigation can
// restart both the timestamp and capture counter at zero, even when the last
// blank-document paint was also zero. A strict timestamp decrease misses it.
class SuiteCutCaptureClock {
 public:
  double Map(double native_ms,double received_ms,bool recording,
             std::optional<uint64_t> counter=std::nullopt,double capture_fps=0) {
    const bool restarted=counter&&last_counter_&&*counter<=*last_counter_;
    const bool reset=!offset_||native_ms<last_native_||restarted;
    if(reset)normalized_=native_ms;
    else {
      double delta=native_ms-last_native_;
      // CEF quantizes its capture period to whole microseconds (8333 at
      // 120 Hz). Restore only exact multiples of that known period; otherwise
      // a long continuous stream eventually loses a CFR slot to rounding drift.
      // Non-quantized source intervals and genuine missing intervals survive.
      if(capture_fps>0&&capture_fps<=120){
        const double period=std::floor(1000000/capture_fps);
        const double count=std::round(delta*1000/period);
        if(count>0&&std::abs(delta*1000-count*period)<.01)delta=count*1000/capture_fps;
      }
      normalized_+=delta;
    }
    if(reset)offset_=std::max(received_ms,last_mapped_)-normalized_;
    else if(!recording)offset_=std::min(*offset_,received_ms-normalized_);
    // Oracle catch-up after a restarted stream may initially advance its
    // media clock faster than delivery. Captured images cannot be in the
    // future; lower the offset instead of making stop precede the last image.
    if(normalized_+*offset_>received_ms)offset_=received_ms-normalized_;
    last_native_=native_ms;last_counter_=counter;
    last_mapped_=std::max(last_mapped_,normalized_+*offset_);
    return last_mapped_;
  }
  double Offset()const{return offset_.value_or(0);}
 private:
  std::optional<double> offset_;
  std::optional<uint64_t> last_counter_;
  double last_native_=0,last_mapped_=0,normalized_=0;
};
