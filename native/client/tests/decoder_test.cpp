// SPDX-License-Identifier: MIT
#include "decoder.hpp"
#include "suitecut/json.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

using suitecut::Json;
using suitecut::Packet;
using suitecut::PacketDecoder;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& work) { bool rejected = false; try { work(); } catch (const std::exception&) { rejected = true; } check(rejected, "Expected decoder rejection"); }
std::vector<std::uint8_t> packet(std::string header, std::vector<std::uint8_t> body = {}) {
  std::vector<std::uint8_t> out;
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(header.size() >> (i * 8)));
  out.insert(out.end(), header.begin(), header.end()); out.insert(out.end(), body.begin(), body.end()); return out;
}
void discard(Packet&&) {}
}
int main() {
  try {
    const auto frame = packet(R"({"version":1,"type":"frame","bytes":16,"width":2,"height":2})", std::vector<std::uint8_t>(16, 42));
    const auto reply = packet(R"({"version":1,"type":"response","bytes":3,"id":9,"ok":true})", {1, 0, 255});
    const auto ready = packet(R"({"version":1,"type":"ready","bytes":0})");
    std::vector<std::uint8_t> combined = ready; combined.insert(combined.end(), frame.begin(), frame.end()); combined.insert(combined.end(), reply.begin(), reply.end());
    for (std::size_t chunk = 1; chunk <= combined.size(); ++chunk) {
      PacketDecoder decoder; std::vector<Packet> received;
      for (std::size_t at = 0; at < combined.size(); at += chunk)
        decoder.feed(std::span(combined).subspan(at, std::min(chunk, combined.size() - at)), [&](Packet&& value) { received.push_back(std::move(value)); });
      decoder.finish();
      check(received.size() == 3 && received[1].payload == std::vector<std::uint8_t>(16, 42) && received[2].payload == std::vector<std::uint8_t>({1,0,255}), "Fragmented packets changed data");
      check(Json::parse(received[2].header).at("id").as_int() == 9, "Response header changed");
      rejects([&] { decoder.feed({}, discard); });
    }
    for (std::size_t cut = 1; cut < reply.size(); ++cut) {
      PacketDecoder decoder; decoder.feed(std::span(reply).first(cut), discard);
      rejects([&] { decoder.finish(); }); rejects([&] { decoder.feed({}, discard); });
    }
    for (const std::uint32_t length : {0u, 1u, 65537u, 0xffffffffu}) {
      const std::uint8_t prefix[] = {static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length >> 16), static_cast<std::uint8_t>(length >> 24)};
      PacketDecoder decoder; rejects([&] { decoder.feed(prefix, discard); });
    }
    for (const auto header : {
      R"({"version":2,"type":"ready","bytes":0})", R"({"version":1.0,"type":"ready","bytes":0})", R"({"version":1,"type":null,"bytes":0})",
      R"({"version":1,"type":"unknown","bytes":0})", R"({"version":1,"type":"ready","bytes":-1})", R"({"version":1,"type":"ready","bytes":0.0})",
      R"({"version":1,"type":"ready","bytes":1})", R"({"version":1,"type":"ready","bytes":0,"bytes":0})",
      R"({"version":1,"type":"console","bytes":33177601})", R"({"version":1,"type":"response","bytes":201326593,"id":1,"ok":true})",
      R"({"version":1,"type":"response","bytes":0,"id":0,"ok":true})", R"({"version":1,"type":"response","bytes":0,"id":1,"ok":"yes"})",
      R"({"version":1,"type":"frame","bytes":16,"width":3841,"height":2})", R"({"version":1,"type":"frame","bytes":15,"width":2,"height":2})",
      R"({"version":1,"type":"frame","bytes":9,"width":3,"height":2,"pixelFormat":"i420"})", R"({"version":1,"type":"audio","bytes":4,"frames":2})",
      R"({"version":1,"type":"ready","bytes":0,"pageId":0})", R"([])", R"({})"}) {
      PacketDecoder decoder; auto encoded = packet(header);
      rejects([&] { decoder.feed(encoded, discard); }); rejects([&] { decoder.finish(); });
    }
    { PacketDecoder decoder(65536, 2); rejects([&] { decoder.feed(reply, discard); }); }
    rejects([] { PacketDecoder decoder(65537); }); rejects([] { PacketDecoder decoder(65536, 201326593); });
    // Advertising the largest allowed response without any payload must not
    // allocate the advertised 192 MiB; EOF still rejects the incomplete packet.
    { PacketDecoder decoder; auto encoded = packet(R"({"version":1,"type":"response","bytes":201326592,"id":1,"ok":true})"); decoder.feed(encoded, discard); rejects([&] { decoder.finish(); }); }
    { PacketDecoder decoder; rejects([&] { decoder.feed(ready, [&](Packet&&) { decoder.feed(ready, discard); }); }); rejects([&] { decoder.finish(); }); }
    { PacketDecoder decoder; rejects([&] { decoder.feed(ready, [&](Packet&&) { throw std::runtime_error("callback"); }); }); rejects([&] { decoder.finish(); }); }
    { PacketDecoder decoder; rejects([&] { decoder.feed({}, {}); }); decoder.feed(ready, discard); decoder.finish(); }
    // One-byte fragments over a sizeable payload exercise amortized growth.
    const std::size_t length = 1024 * 1024;
    const auto large = packet(Json{{"version",1},{"type","response"},{"id",1},{"ok",true},{"bytes",length}}.dump(), std::vector<std::uint8_t>(length, 7));
    PacketDecoder decoder; std::size_t completed = 0;
    for (const auto& byte : large) decoder.feed(std::span(&byte, 1), [&](Packet&& value) { check(value.payload.size() == length && value.payload.front() == 7 && value.payload.back() == 7, "Large fragmented payload changed"); check(value.payload.capacity() <= length, "Payload allocated beyond advertised length"); ++completed; });
    decoder.finish(); check(completed == 1, "Large packet callback count changed");
    std::cout << "Incremental packet fragmentation, bounds, EOF, poisoning and ownership passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
