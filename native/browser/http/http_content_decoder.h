// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <zlib.h>
#include <brotli/decode.h>
namespace suitecut {
// Incremental HTTP *content* decoding. The caller supplies actual transfer-
// decoded body bytes, owns the original HTTP deadline, and retains this owner
// until its genuine transport callback/cancel returns. Only the deflate format
// probe (bytes before the first decoded output) is retained; response bodies
// are delivered incrementally. suitecut sets no memory, size or chain limit.
class NativeHTTPContentDecoder {
 public:
  using Sink = std::function<bool(std::span<const unsigned char>)>;
  using Continue = std::function<bool()>;

 private:
  // Library allocation requests, including our allocation header, are counted
  // for diagnostics. Allocator arena/RSS overhead is not this retained-byte count.
  struct Memory {
    struct alignas(std::max_align_t) Allocation {
      size_t bytes;
      Memory* owner;
    };
    size_t bytes = 0, peak = 0;
    static inline std::atomic<size_t> global{0};
    void* Allocate(size_t amount) {
      if (amount > std::numeric_limits<size_t>::max() - sizeof(Allocation)) return nullptr;
      const size_t cost = amount + sizeof(Allocation);
      auto* allocation = static_cast<Allocation*>(std::malloc(cost));
      if (!allocation) return nullptr;
      global.fetch_add(cost);
      allocation->bytes = cost;
      allocation->owner = this;
      bytes += cost;
      if (bytes > peak) peak = bytes;
      return allocation + 1;
    }
    static void Free(void* pointer) {
      if (!pointer) return;
      auto* allocation = static_cast<Allocation*>(pointer) - 1;
      allocation->owner->bytes -= allocation->bytes;
      global.fetch_sub(allocation->bytes);
      std::free(allocation);
    }
    static void* ZAllocate(void* opaque, uInt count, uInt size) {
      if (size && count > std::numeric_limits<size_t>::max() / size) return nullptr;
      return static_cast<Memory*>(opaque)->Allocate(size_t(count) * size);
    }
    static void ZFree(void*, void* pointer) { Free(pointer); }
    static void* BAllocate(void* opaque, size_t size) {
      return static_cast<Memory*>(opaque)->Allocate(size);
    }
    static void BFree(void*, void* pointer) { Free(pointer); }
  };
  enum class Kind { Gzip, Deflate, Brotli };
  struct Stage {
    Memory& memory;
    Kind kind;
    z_stream z{};
    BrotliDecoderState* b = nullptr;
    bool z_ready = false, finished = false, started = false, z_invalid = false, raw_deflate = false;
    std::string prefix;
    size_t produced = 0;
    std::array<unsigned char, 16384> output{};
    Stage(Memory& owner, Kind value) : memory(owner), kind(value) {}
    ~Stage() {
      if (z_ready) inflateEnd(&z);
      if (b) BrotliDecoderDestroyInstance(b);
    }
    bool ZInit(int bits) {
      z = {};
      z.zalloc = Memory::ZAllocate;
      z.zfree = Memory::ZFree;
      z.opaque = &memory;
      z_ready = inflateInit2(&z, bits) == Z_OK;
      return z_ready;
    }
    bool Init() {
      if (kind == Kind::Brotli) {
        b = BrotliDecoderCreateInstance(Memory::BAllocate, Memory::BFree, &memory);
        return b && BrotliDecoderSetParameter(b, BROTLI_DECODER_PARAM_LARGE_WINDOW, 0);
      }
      return ZInit(kind == Kind::Deflate ? MAX_WBITS : MAX_WBITS + 16);
    }
    bool Emit(size_t count, const Sink& next, std::string& error) {
      produced = count > std::numeric_limits<size_t>::max() - produced
                     ? std::numeric_limits<size_t>::max()
                     : produced + count;
      if (count && !next({output.data(), count})) {
        if (error.empty()) error = "Native HTTP decoded body was rejected";
        return false;
      }
      return true;
    }
    bool ZPush(std::span<const unsigned char> input, const Sink& next, std::string& error) {
      // zlib takes at most uInt bytes per call: split, never reject.
      constexpr size_t piece = std::numeric_limits<uInt>::max();
      while (input.size() > piece) {
        if (!ZPushPiece(input.first(piece), next, error)) return false;
        input = input.subspan(piece);
      }
      return ZPushPiece(input, next, error);
    }
    bool ZPushPiece(std::span<const unsigned char> input, const Sink& next, std::string& error) {
      z.next_in = const_cast<Bytef*>(input.data());
      z.avail_in = static_cast<uInt>(input.size());
      while (z.avail_in) {
        if (finished) {
          if (kind != Kind::Gzip) {
            error = "Trailing bytes after native HTTP compressed stream";
            return false;
          }
          if (inflateReset2(&z, MAX_WBITS + 16) != Z_OK) {
            error = "Could not reset native gzip member";
            return false;
          }
          finished = false;
        }
        z.next_out = output.data();
        z.avail_out = static_cast<uInt>(output.size());
        const auto before = z.avail_in;
        const int result = inflate(&z, Z_NO_FLUSH);
        started = true;
        if (result == Z_STREAM_END) finished = true;
        if (result != Z_OK && result != Z_BUF_ERROR && result != Z_STREAM_END) {
          z_invalid = result == Z_DATA_ERROR || result == Z_NEED_DICT;
          error = "Invalid native HTTP zlib compressed stream";
          return false;
        }
        if (!Emit(output.size() - z.avail_out, next, error)) return false;
        if (before == z.avail_in && z.avail_out == output.size()) {
          error = "Native HTTP zlib decoder made no progress";
          return false;
        }
        // Input can be exhausted while output remains pending in zlib. Drain it
        // before accepting another body callback or testing genuine EOF.
        while (!z.avail_in && !z.avail_out && !finished) {
          z.next_out = output.data();
          z.avail_out = static_cast<uInt>(output.size());
          const int drained = inflate(&z, Z_NO_FLUSH);
          if (drained == Z_STREAM_END) finished = true;
          if (drained != Z_OK && drained != Z_BUF_ERROR && drained != Z_STREAM_END) {
            z_invalid = drained == Z_DATA_ERROR || drained == Z_NEED_DICT;
            error = "Invalid native HTTP zlib compressed stream";
            return false;
          }
          if (!Emit(output.size() - z.avail_out, next, error)) return false;
          if (z.avail_out == output.size()) break;
        }
      }
      return true;
    }
    bool Push(std::span<const unsigned char> input, const Sink& next, std::string& error) {
      if (input.empty()) return true;
      if (kind == Kind::Brotli) {
        if (finished) {
          error = "Trailing bytes after native HTTP Brotli stream";
          return false;
        }
        size_t available = input.size();
        const uint8_t* cursor = input.data();
        while (true) {
          size_t room = output.size();
          uint8_t* destination = output.data();
          const auto before = available;
          const auto result =
              BrotliDecoderDecompressStream(b, &available, &cursor, &room, &destination, nullptr);
          started = true;
          if (result == BROTLI_DECODER_RESULT_SUCCESS) finished = true;
          if (result == BROTLI_DECODER_RESULT_ERROR) {
            error = "Invalid native HTTP Brotli stream or decoder allocation failed";
            return false;
          }
          if (!Emit(output.size() - room, next, error)) return false;
          if (finished) {
            if (available) {
              error = "Trailing bytes after native HTTP Brotli stream";
              return false;
            }
            return true;
          }
          if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
            if (available) {
              error = "Native HTTP Brotli decoder left unconsumed input";
              return false;
            }
            return true;
          }
          if (before == available && room == output.size()) {
            error = "Native HTTP Brotli decoder made no progress";
            return false;
          }
        }
      }
      if (kind == Kind::Deflate && !raw_deflate && produced == 0) {
        // Retain the format probe until an actual wrapped decoder accepts
        // output. A legal raw stream can begin with a valid CMF/FLG pair; do
        // not guess its format from those two bytes.
        prefix.append(reinterpret_cast<const char*>(input.data()), input.size());
        z_invalid = false;
        const bool accepted = ZPush(input, next, error);
        if (accepted || !z_invalid || produced) {
          if (produced) std::string().swap(prefix);
          return accepted;
        }
        inflateEnd(&z);
        z_ready = false;
        raw_deflate = true;
        error.clear();
        if (!ZInit(-MAX_WBITS)) {
          error = "Could not allocate native raw deflate decoder";
          return false;
        }
        const std::string probe = std::move(prefix);
        prefix.clear();
        return ZPush({reinterpret_cast<const unsigned char*>(probe.data()), probe.size()}, next,
                     error);
      }
      return ZPush(input, next, error);
    }
  };
  struct State {
    Memory memory;
    Sink sink;
    Continue checkpoint;
    std::vector<Stage*> stages;
    size_t count = 0, total = 0;
    bool configured = false, canceled = false, sealed = false, successful = false,
         sink_rejected = false;
    std::string error;
    static inline std::atomic<size_t> instances{0};
    explicit State(Sink target, Continue check)
        : sink(std::move(target)), checkpoint(std::move(check)) {}
    ~State() {
      ClearStages();
      instances.fetch_sub(1);
    }
    void ClearStages() {
      for (auto*& stage : stages)
        if (stage) {
          stage->~Stage();
          Memory::Free(stage);
          stage = nullptr;
        }
      stages.clear();
      count = 0;
    }
    bool Add(Kind kind) {
      stages.reserve(count + 1);
      auto* storage = memory.Allocate(sizeof(Stage));
      if (!storage) {
        error = "Native HTTP decoder allocation failed";
        return false;
      }
      auto* stage = new (storage) Stage(memory, kind);
      stages.push_back(stage);
      ++count;
      if (!stage->Init()) {
        error = "Could not allocate native HTTP decoder";
        return false;
      }
      return true;
    }
    bool Feed(size_t index, std::span<const unsigned char> bytes) {
      if (canceled) return false;
      if (checkpoint && !checkpoint()) {
        error = "Native HTTP decoding stopped at its original deadline or cancellation";
        return false;
      }
      if (index < count)
        return stages[index]->Push(
            bytes, [&](auto output) { return Feed(index + 1, output); }, error);
      if (bytes.empty()) return true;
      total = bytes.size() > std::numeric_limits<size_t>::max() - total
                  ? std::numeric_limits<size_t>::max()
                  : total + bytes.size();
      const bool accepted = sink(bytes);
      if (!accepted) {
        sink_rejected = true;
        if (error.empty()) error = "Native HTTP decoded body was rejected";
      }
      return accepted && !canceled;
    }
  };
  std::shared_ptr<State> state_;
  static std::shared_ptr<State> Create(Sink sink, Continue checkpoint) {
    State::instances.fetch_add(1);
    try {
      return std::make_shared<State>(std::move(sink), std::move(checkpoint));
    } catch (...) {
      State::instances.fetch_sub(1);
      return {};
    }
  }
  bool capacity_unavailable_ = false;

 public:
  explicit NativeHTTPContentDecoder(Sink sink, Continue checkpoint = {})
      : state_(Create(std::move(sink), std::move(checkpoint))), capacity_unavailable_(!state_) {}
  ~NativeHTTPContentDecoder() { Cancel(); }
  NativeHTTPContentDecoder(const NativeHTTPContentDecoder&) = delete;
  NativeHTTPContentDecoder& operator=(const NativeHTTPContentDecoder&) = delete;
  bool Configure(std::string_view encoding) {
    const auto state = state_;
    if (!state || state->canceled || state->configured) return false;
    state->configured = true;
    std::vector<Kind> chain;
    size_t begin = 0;
    while (begin < encoding.size()) {
      const auto separator = encoding.find(',', begin);
      auto token = encoding.substr(
          begin, separator == std::string_view::npos ? encoding.size() - begin : separator - begin);
      while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
        token.remove_prefix(1);
      while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
        token.remove_suffix(1);
      std::string name(token);
      for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (name != "identity") {
        if (name == "gzip" || name == "x-gzip")
          chain.push_back(Kind::Gzip);
        else if (name == "deflate")
          chain.push_back(Kind::Deflate);
        else if (name == "br")
          chain.push_back(Kind::Brotli);
        else {
          state->error = "Unsupported native HTTP content encoding";
          return false;
        }
      }
      if (separator == std::string_view::npos) break;
      begin = separator + 1;
      if (begin == encoding.size()) {
        state->error = "Invalid native HTTP content encoding list";
        return false;
      }
    }
    for (size_t i = chain.size(); i > 0; --i)
      if (!state->Add(chain[i - 1])) {
        state->ClearStages();
        return false;
      }
    return true;
  }
  bool Push(std::span<const unsigned char> bytes) {
    const auto state = state_;
    return state && state->configured && !state->canceled && !state->sealed &&
           state->error.empty() && state->Feed(0, bytes);
  }
  bool Finish() {
    const auto state = state_;
    if (!state || !state->configured || state->canceled || !state->error.empty()) return false;
    if (state->sealed) return state->successful;
    state->sealed = true;
    if (state->checkpoint && !state->checkpoint()) {
      state->error = "Native HTTP decoding stopped at its original deadline or cancellation";
      return false;
    }
    for (size_t i = 0; i < state->count; ++i)
      if (!state->stages[i]->started || !state->stages[i]->finished) {
        state->error = "Incomplete native HTTP compressed stream at actual response EOF";
        return false;
      }
    state->successful = true;
    return true;
  }
  void Cancel() {
    if (state_) state_->canceled = true;
    state_.reset();
  }
  std::string error() const {
    return state_                  ? state_->error
           : capacity_unavailable_ ? "Native HTTP decoder allocation failed"
                                   : "Native HTTP content decoder was canceled";
  }
  bool sink_rejected() const { return state_ && state_->sink_rejected; }
  size_t bytes() const { return state_ ? state_->memory.bytes : 0; }
  size_t peak() const { return state_ ? state_->memory.peak : 0; }
  static size_t GlobalBytes() { return Memory::global.load(); }
  static size_t Instances() { return State::instances.load(); }
};
}  // namespace suitecut
