// SPDX-License-Identifier: MIT
#include "decoder.hpp"
#include "suitecut/json.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>

using suitecut::Json;
namespace {
struct InvariantError : std::runtime_error { using std::runtime_error::runtime_error; };
std::vector<std::uint8_t> encoded(std::string text, std::vector<std::uint8_t> body = {}) {
  std::vector<std::uint8_t> out;
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(text.size() >> (i * 8)));
  out.insert(out.end(), text.begin(), text.end()); out.insert(out.end(), body.begin(), body.end()); return out;
}
}
int main() {
  const auto started = std::chrono::steady_clock::now();
  const std::size_t iterations = std::getenv("SUITECUT_CLIENT_FUZZ_ITERATIONS") ? std::stoull(std::getenv("SUITECUT_CLIENT_FUZZ_ITERATIONS")) : 20000;
  std::mt19937_64 random(0x5355495445435554ULL);
  const std::vector<std::string> seeds = {"null", "true", "-9223372036854775808", "100000000000000000000", "1.25e-8", R"({"a":[1,null,false,"text"],"b":"\u20ac\ud83d\ude00"})", R"({"a":1,"\u0061":2})", "[[[[[[[[[[0]]]]]]]]]]", std::string(31, '[') + "0" + std::string(31, ']'), std::string(33, '[') + "0" + std::string(33, ']'), "\"\\ud800\"", "\"\xff\"", "\"line\nfeed\""};
  std::size_t accepted_json = 0, rejected_json = 0, accepted_stream = 0, rejected_stream = 0, packets = 0;
  try {
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
      std::string candidate = seeds[random() % seeds.size()];
      if (iteration % 4096 == 0) {
        Json::array values;
        for (int i = 0; i < 128; ++i) values.emplace_back(Json::object{{"index",i},{"text","\\\"\n\xf0\x9f\x98\x80"}});
        candidate = Json(std::move(values)).dump();
      }
      for (std::size_t mutation = 0, count = random() % 12; mutation < count; ++mutation) {
        const auto at = random() % (candidate.size() + 1);
        switch (random() % 3) {
          case 0: if (at < candidate.size()) candidate[at] = static_cast<char>(random()); break;
          case 1: candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(at), static_cast<char>(random())); break;
          default: if (at < candidate.size()) candidate.erase(at, 1); break;
        }
      }
      try {
        const auto value = Json::parse(candidate, {16384, 32, 2048, 8192});
        const auto dumped = value.dump({65536, 32, 2048, 16384});
        if (Json::parse(dumped, {65536, 32, 2048, 16384}) != value) throw InvariantError("Accepted JSON failed exact roundtrip");
        ++accepted_json;
      } catch (const InvariantError&) { throw; } catch (const std::exception&) { ++rejected_json; }

      const bool large = iteration % 4096 == 0;
      const std::size_t length = large ? 1024 * 1024 : random() % 4097;
      const std::size_t payload_budget = large ? 1024 * 1024 : 8192;
      const Json header{{"version",1},{"type","response"},{"id",1},{"ok",true},{"bytes",length}};
      auto stream = encoded(header.dump(), std::vector<std::uint8_t>(length, static_cast<std::uint8_t>(iteration)));
      const auto valid_size = stream.size();
      const bool mutate = iteration % 3 != 0;
      if (mutate) {
        for (std::size_t change = 0, count = 1 + random() % 8; change < count; ++change) {
          const auto at = random() % (stream.size() + 1);
          if (at < stream.size()) stream[at] ^= static_cast<std::uint8_t>(1u << (random() % 8));
        }
        if (random() % 3 == 0) stream.resize(random() % (stream.size() + 1));
      }
      suitecut::PacketDecoder decoder(65536, payload_budget); std::size_t count = 0;
      try {
        for (std::size_t at = 0; at < stream.size();) {
          const auto chunk = std::min<std::size_t>(1 + random() % (large ? 8192 : 127), stream.size() - at);
          decoder.feed(std::span(stream).subspan(at, chunk), [&](suitecut::Packet&& packet) {
            if (packet.payload.capacity() > payload_budget || packet.payload.size() > payload_budget) throw InvariantError("Decoder exceeded payload budget");
            if (!mutate && (packet.payload.size() != length || packet.payload != std::vector<std::uint8_t>(length, static_cast<std::uint8_t>(iteration)))) throw InvariantError("Valid payload corruption");
            ++count; ++packets;
          });
          at += chunk;
        }
        decoder.finish();
        if (!mutate && (count != 1 || stream.size() != valid_size)) throw InvariantError("Valid packet lost");
        ++accepted_stream;
      } catch (const InvariantError&) { throw; } catch (const std::exception&) { if (!mutate) throw; ++rejected_stream; }
      if ((iteration + 1) % 50000 == 0)
        std::cout << "{\"progressIterations\":" << iteration + 1 << ",\"acceptedJSON\":" << accepted_json << ",\"deliveredPackets\":" << packets << "}" << std::endl;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "{\"iterations\":" << iterations << ",\"seed\":\"5355495445435554\",\"acceptedJSON\":" << accepted_json << ",\"rejectedJSON\":" << rejected_json << ",\"acceptedStreams\":" << accepted_stream << ",\"rejectedStreams\":" << rejected_stream << ",\"deliveredPackets\":" << packets << ",\"elapsedMs\":" << elapsed << "}\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "Fuzz invariant: " << error.what() << '\n'; return 1; }
}
