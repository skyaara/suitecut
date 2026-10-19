// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace suitecut {
class Json;
enum class BrowserBackend { cef, chromium, firefox };
struct LaunchOptions {
  std::string executable;
  BrowserBackend backend = BrowserBackend::cef;
  // Attach to a caller-owned debugging port or ws(s):// browser endpoint.
  // HTTP(S) endpoints discover /json/version. Empty owns a local launch.
  std::string cdp_endpoint;
  // Applied to discovery and WebSocket upgrades; never logged by the wire.
  std::map<std::string, std::string> connect_headers;
  // TLS always verifies the peer chain and hostname/IP. Empty CA uses system
  // trust. Client certificate and private key must be supplied together (PEM).
  std::string tls_ca_file, tls_client_certificate, tls_client_key;
  std::string profile; // Empty creates an owned temporary profile.
  std::vector<std::string> arguments;
  int width = 1280, height = 720;
  double frames_per_second = 60;
  bool headless = true;
  std::chrono::milliseconds startup_timeout{30000};
  std::chrono::milliseconds shutdown_timeout{5000};
  std::size_t max_pending = 256;
  std::size_t max_event_bytes = 64 * 1024 * 1024;
  std::size_t max_payload_bytes = 192 * 1024 * 1024;
};
struct Packet {
  std::string header;
  std::vector<std::uint8_t> payload;
};
struct Request {
  int id = 0;
  std::future<Packet> result;
};
struct ClientStats {
  std::uint64_t commands = 0, responses = 0, dropped_events = 0;
  std::size_t pending = 0, queued_event_bytes = 0;
};
// Thread-safe requests and event polling. No application callback runs on the
// reader thread. Futures own response buffers; event queue storage is bounded.
class Transport {
 public:
  static std::shared_ptr<Transport> launch(const LaunchOptions& options);
  ~Transport();
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;
  // JSON object with command/pageId/fields; version and id are assigned here.
  Request submit(std::string fields_json,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});
  Request submit_json(Json fields,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});
  Packet call(std::string fields_json,
              std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});
  Packet call_json(Json fields,
              std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});
  void cancel(int request_id);
  std::optional<Packet> poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
  Packet ready() const;
  Packet wait_ready(int page_id,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds{30000});
  ClientStats stats() const;
  void close();
 private:
  struct Impl;
  explicit Transport(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
} // namespace suitecut
