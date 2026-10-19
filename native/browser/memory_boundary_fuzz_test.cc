// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
#include <brotli/encode.h>
#include "capture_buffer.h"
#include "http_content_decoder.h"
#include "http_multipart.h"
#include "screenshot_pixels.h"
#include "storage_utf16.h"

using Bytes = std::vector<unsigned char>;
using Decoder = suitecut::NativeHTTPContentDecoder;

Bytes Compress(const Bytes& input, unsigned kind) {
  if (kind == 3) {
    size_t size = BrotliEncoderMaxCompressedSize(input.size());
    Bytes output(size);
    assert(BrotliEncoderCompress(3, 20, BROTLI_MODE_GENERIC, input.size(), input.data(), &size, output.data()));
    output.resize(size);
    return output;
  }
  z_stream stream{};
  const int bits = kind == 0 ? MAX_WBITS + 16 : kind == 1 ? MAX_WBITS : -MAX_WBITS;
  assert(deflateInit2(&stream, 3, Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY) == Z_OK);
  Bytes output(compressBound(input.size()) + 64);
  stream.next_in = const_cast<unsigned char*>(input.data());
  stream.avail_in = input.size();
  stream.next_out = output.data();
  stream.avail_out = output.size();
  assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
  output.resize(stream.total_out);
  deflateEnd(&stream);
  return output;
}

int main(int argc, char** argv) {
  const unsigned iterations = argc > 1 ? std::stoul(argv[1]) : 10000;
  assert(iterations && iterations <= 1000000);
  std::mt19937 random(0x6d656d31);
  size_t valid_streams = 0, malformed_streams = 0, capture_cases = 0, multipart_cases = 0;
  size_t peak_codec_bytes = 0;
  const char* encoding[] = {"gzip", "deflate", "deflate", "br"};
  for (unsigned iteration = 0; iteration < iterations; ++iteration) {
    Bytes input(random() % 8193);
    for (auto& byte : input) byte = random();
    const unsigned kind = random() % 4;
    const auto compressed = Compress(input, kind);
    {
      Bytes output;
      Decoder decoder([&](auto chunk) { output.insert(output.end(), chunk.begin(), chunk.end()); return true; });
      assert(decoder.Configure(encoding[kind]));
      for (size_t offset = 0; offset < compressed.size();) {
        const size_t count = std::min(compressed.size() - offset, size_t(1 + random() % 1024));
        assert(decoder.Push(std::span(compressed).subspan(offset, count)));
        offset += count;
      }
      assert(decoder.Finish() && decoder.Finish() && output == input);
      peak_codec_bytes = std::max(peak_codec_bytes, decoder.peak());
      ++valid_streams;
    }
    assert(Decoder::GlobalBytes() == 0 && Decoder::Instances() == 0);
    {
      // Random content plus tight allocation budgets, arbitrary chunking,
      // sink rejection, explicit cancellation and genuine truncated EOF.
      Bytes mutated = compressed;
      switch (iteration % 3) {
        case 0: mutated.resize(random() % mutated.size()); break;
        case 1: mutated[random() % mutated.size()] ^= 1u << (random() % 8); break;
        case 2: for (auto& byte : mutated) byte = random(); break;
      }
      size_t delivered = 0;
      Decoder decoder([&](auto chunk) { delivered += chunk.size(); return iteration % 7 != 0; },
                      iteration % 5 == 0 ? 32768 : Decoder::kPerCodecBytes);
      if (decoder.Configure(encoding[kind])) {
        for (size_t offset = 0; offset < mutated.size();) {
          const size_t count = std::min(mutated.size() - offset, size_t(1 + random() % 511));
          if (!decoder.Push(std::span(mutated).subspan(offset, count))) break;
          offset += count;
        }
        decoder.Finish();
      }
      assert(delivered <= Decoder::kDecodedBytes && decoder.bytes() <= Decoder::kPerCodecBytes);
      peak_codec_bytes = std::max(peak_codec_bytes, decoder.peak());
      decoder.Cancel();
      assert(Decoder::GlobalBytes() == 0 && Decoder::Instances() == 0);
      ++malformed_streams;
    }
    {
      struct Rect { int x, y, width, height; };
      const int width = 1 + random() % 64, height = 1 + random() % 64;
      Bytes surface(size_t(width) * height * 4), retained;
      for (auto& byte : surface) byte = random();
      std::vector<Rect> dirty;
      assert(SuiteCutUpdateCapture(retained, surface.data(), width, height, 0, 0, dirty));
      for (unsigned paint = 0; paint < 8; ++paint) {
        const int x = random() % width, y = random() % height;
        const int w = 1 + random() % (width - x), h = 1 + random() % (height - y);
        dirty = {{x, y, w, h}};
        for (int row = y; row < y + h; ++row)
          for (int column = x * 4; column < (x + w) * 4; ++column)
            surface[size_t(row) * width * 4 + column] = random();
        if (paint == 7) dirty.push_back({-1, 0, 1, 1});
        assert(SuiteCutUpdateCapture(retained, surface.data(), width, height, width, height, dirty));
        assert(surface == retained);
        ++capture_cases;
      }
    }
    {
      // Failure must release previous wire capacity, and range arithmetic
      // must reject arbitrary size_t inputs before slicing source memory.
      std::vector<suitecut::NativeHTTPMultipartPart> parts{{"field", "", "", 0, input.size(), false}};
      Bytes wire;
      std::string error;
      assert(suitecut::EncodeNativeHTTPMultipart(input, parts, "nativeMemoryBoundary1234", 65536, wire, error));
      assert(wire.size() >= input.size());
      parts[0].offset = size_t(random()) << 32 | random();
      if (!parts[0].offset) parts[0].offset = 1;
      assert(!suitecut::EncodeNativeHTTPMultipart(input, parts, "nativeMemoryBoundary1234", 65536, wire, error));
      assert(wire.empty() && wire.capacity() == 0);
      ++multipart_cases;
    }
    {
      std::string arbitrary;
      for (unsigned i = 0, count = random() % 512; i < count; ++i) arbitrary += char(random());
      const auto projected = suitecut::StorageJSONValidationProjection(arbitrary);
      assert(projected.size() == arbitrary.size());
      assert(suitecut::StorageJSONValidationProjection(projected) == projected);
      auto budget = std::make_shared<suitecut::ScreenshotByteBudget>(65536);
      std::vector<std::shared_ptr<suitecut::ScreenshotByteBudget::Lease>> leases;
      size_t owned = 0;
      for (unsigned action = 0; action < 16; ++action) {
        const size_t bytes = random() % 8192;
        auto lease = budget->Reserve(bytes);
        assert(bool(lease) == (bytes <= 65536 - owned));
        if (lease) { owned += bytes; leases.push_back(std::move(lease)); }
        assert(budget->Used() == owned);
        if (!leases.empty() && random() % 3 == 0) {
          const size_t remaining = random() % (leases.back()->bytes + 1);
          owned -= leases.back()->bytes - remaining;
          leases.back()->Shrink(remaining);
          assert(budget->Used() == owned);
        }
      }
      leases.clear();
      assert(budget->Used() == 0);
    }
  }
  std::printf("PASS seed 0x6d656d31: %zu valid codec streams, %zu malformed/lifetime codec schedules, %zu damage-model paints, %zu multipart failure/release cases, %u storage and screenshot-budget schedules; peak codec charged bytes %zu\n",
              valid_streams, malformed_streams, capture_cases, multipart_cases, iterations, peak_codec_bytes);
}
