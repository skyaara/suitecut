// SPDX-License-Identifier: MIT
#include "suitecut/transport.hpp"
#include "suitecut/json.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace std::chrono_literals;
using suitecut::Json;
using suitecut::Transport;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& operation, const char* message) {
  bool rejected = false; try { operation(); } catch (const std::exception&) { rejected = true; }
  check(rejected, message);
}
std::size_t descriptors() {
#ifdef __linux__
  std::size_t count = 0; for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) { (void)entry; ++count; } return count;
#else
  return 0;
#endif
}
Json body(const suitecut::Packet& packet) {
  return Json::parse(std::string_view(reinterpret_cast<const char*>(packet.payload.data()), packet.payload.size()));
}
} // namespace

int main(int argc, char** argv) {
  try {
    const auto executable = argc > 1 ? std::filesystem::absolute(argv[1]) : std::filesystem::absolute(argv[0]).parent_path() / "transport_fake_engine";
    suitecut::LaunchOptions options;
    options.executable = executable.string(); options.startup_timeout = 5s; options.shutdown_timeout = 250ms;
    const auto baseline_fds = descriptors();
    {
      auto transport = Transport::launch(options);
      const auto ready = Json::parse(transport->ready().header);
      const auto profile = ready.at("profile").as_string();
      check(ready.at("onDemand").as_bool(), "Default launch must request on-demand frame transport");
      check(std::filesystem::is_directory(profile), "Temporary profile does not exist");
      const auto response = transport->call("{\"command\":\"echo\",\"id\":999,\"version\":99,\"text\":\"line\\nvalue\"}");
      const auto echoed = body(response);
      check(echoed.at("id").as_int() != 999 && echoed.at("version").as_int() == 1, "Reserved envelope identity was not assigned by transport");
      check(echoed.at("text").as_string() == "line\nvalue", "Escaped newline corrupted framing");
      check(body(transport->call_json(Json::object{{"command", "echo"}, {"prepared", true}})).at("prepared").as_bool(), "Prepared Json request path failed");
      const auto error = transport->call("{\"command\":\"error\"}");
      check(!Json::parse(error.header).at("ok").as_bool() && body(error).at("detail").as_int() == 9, "Raw command errors lost their structured packet");
      rejects([&] { transport->submit(std::string("{\"command\":\"echo\",\"text\":\"") + std::string(65500, 'x') + "\"}"); }, "Oversized final wire envelope accepted");
      check(transport->stats().pending == 0, "Rejected command retained pending state");
      auto canceled = transport->submit("{\"command\":\"hang\"}"); transport->cancel(canceled.id);
      rejects([&] { canceled.result.get(); }, "Canceled future did not reject");
      auto timed = transport->submit("{\"command\":\"late\",\"delay\":30}", 3ms);
      rejects([&] { timed.result.get(); }, "Expired future did not reject");
      transport->call("{\"command\":\"echo\"}");
      check(transport->stats().pending == 0, "Late response retained pending request");
      // Page readiness is independent of event polling and cache entries are
      // retired by the engine's real context/page-closed event payload.
      for (int page = 2; page < 1100; ++page) {
        transport->call(Json(Json::object{{"command", "ready-page"}, {"page", page}}).dump());
        check(Json::parse(transport->wait_ready(page, 100ms).header).at("pageId").as_int() == page, "Page ready handshake missing");
        transport->call(Json(Json::object{{"command", "close-page"}, {"page", page}}).dump());
        while (transport->poll()) {}
      }
      rejects([&] { transport->wait_ready(2, 2ms); }, "Closed page retained readiness cache");
      // Deterministic seeded payload fuzz, including binary-safe escaped JSON
      // and one-byte packet fragmentation, uses actual child pipe I/O.
      std::mt19937 random(17);
      for (int round = 0; round < 600; ++round) {
        std::string value;
        const auto size = random() % 1024;
        for (unsigned index = 0; index < size; ++index) value.push_back(static_cast<char>(random() % 128));
        const auto echoed_value = body(transport->call(Json(Json::object{{"command", round % 17 ? "echo" : "fragment"}, {"value", value}, {"round", round}}).dump()));
        check(echoed_value.at("value").as_string() == value, "Pipe fuzz payload mismatch");
      }
      std::atomic<int> completed{0};
      std::atomic<bool> failed{false};
      std::vector<std::thread> threads;
      for (int thread = 0; thread < 4; ++thread) threads.emplace_back([&, thread] {
        try {
          for (int round = 0; round < 200; ++round) {
            const auto reply = body(transport->call(Json(Json::object{{"command", "echo"}, {"thread", thread}, {"round", round}}).dump()));
            if (reply.at("thread").as_int() != thread || reply.at("round").as_int() != round) failed = true;
            ++completed;
          }
        } catch (...) { failed = true; }
      });
      for (auto& thread : threads) thread.join();
      check(!failed && completed == 800, "Concurrent request identities crossed or future failed");
      transport->close(); transport->close();
      check(!std::filesystem::exists(profile), "Owned profile survived close");
      check(descriptors() == baseline_fds, "Close retained pipe descriptors while wrapper remained alive");
      rejects([&] { transport->submit("{\"command\":\"echo\"}"); }, "Closed transport accepted command");
    }
    {
      auto bounded = options; bounded.max_pending = 2;
      auto transport = Transport::launch(bounded);
      auto first = transport->submit("{\"command\":\"hang\"}");
      auto second = transport->submit("{\"command\":\"hang\"}");
      rejects([&] { transport->submit("{\"command\":\"echo\"}"); }, "Pending limit did not reject");
      std::thread closer([&] { transport->close(); }); transport->close(); closer.join();
      rejects([&] { first.result.get(); }, "Close did not reject pending future");
      rejects([&] { second.result.get(); }, "Concurrent close lost pending future");
    }
    {
      const auto pattern = (std::filesystem::temp_directory_path() / "suitecut-persistent-test-XXXXXX").string();
      std::vector<char> path(pattern.begin(), pattern.end()); path.push_back(0);
      check(mkdtemp(path.data()) != nullptr, "Could not create persistent-profile fixture");
      const std::filesystem::path profile(path.data());
      std::ofstream(profile / "user-data") << "preserve";
      auto persistent = options; persistent.profile = profile.string();
      auto transport = Transport::launch(persistent); transport->close();
      check(std::filesystem::is_regular_file(profile / "user-data"), "User-supplied persistent profile was removed");
      std::filesystem::remove_all(profile);
    }
    {
      auto transport = Transport::launch(options);
      std::atomic<bool> admitted{false};
      std::vector<suitecut::Request> requests;
      std::thread submitter([&] {
        for (int round = 0; round < 500; ++round) {
          try { requests.push_back(transport->submit("{\"command\":\"echo\"}")); admitted = true; }
          catch (const std::exception&) { admitted = true; }
        }
      });
      while (!admitted) std::this_thread::yield();
      transport->close(); submitter.join();
      for (auto& request : requests) {
        check(request.result.wait_for(100ms) == std::future_status::ready, "Submit racing close left unresolved future");
        try { request.result.get(); } catch (const std::exception&) {}
      }
      check(transport->stats().pending == 0, "Submit racing close retained pending work");
    }
    {
      auto transport = Transport::launch(options);
      transport->call("{\"command\":\"large-frame\"}");
      const auto frame = transport->poll();
      check(frame && frame->payload.size() == 3840 * 2160 * 4, "Default event budget dropped supported 4K BGRA frame");
      transport->close();
    }
    {
      auto bounded = options; bounded.max_event_bytes = 350;
      auto transport = Transport::launch(bounded);
      transport->call("{\"command\":\"flood-media\",\"count\":1000}");
      bool critical_seen = false;
      while (const auto event = transport->poll()) if (Json::parse(event->header).at("type").as_string() == "dialog") critical_seen = true;
      check(critical_seen && transport->stats().dropped_events > 0, "Disposable media displaced critical event or was unbounded");
      transport->close();
    }
    {
      auto bounded = options; bounded.max_event_bytes = 150;
      auto transport = Transport::launch(bounded);
      rejects([&] { transport->call("{\"command\":\"flood-critical\",\"count\":100}"); }, "Critical overflow silently dropped events");
      transport->close(); check(transport->stats().pending == 0, "Overflow left pending futures");
    }
    {
      auto blocked = options; blocked.arguments = {"--transport-test-mode=blocked"}; blocked.max_pending = 256;
      auto transport = Transport::launch(blocked);
      std::vector<suitecut::Request> requests;
      bool full = false;
      for (int count = 0; count < 256; ++count) {
        try { requests.push_back(transport->submit(Json(Json::object{{"command", "echo"}, {"large", std::string(60000, 'x')}}).dump())); }
        catch (...) { full = true; break; }
      }
      check(full, "Blocked child writer did not exert bounded backpressure");
      const auto start = std::chrono::steady_clock::now(); transport->close();
      check(std::chrono::steady_clock::now() - start < 2s, "Blocked writer prevented bounded close");
      for (auto& request : requests) rejects([&] { request.result.get(); }, "Blocked writer future survived close");
    }
    {
      auto broken = options; broken.arguments = {"--transport-test-mode=closed-input"};
      auto transport = Transport::launch(broken);
      rejects([&] { transport->call("{\"command\":\"echo\"}"); }, "Broken stdin did not reject safely without SIGPIPE");
      transport->close();
    }
    {
      auto silent = options; silent.arguments = {"--transport-test-mode=stderr-silent"}; silent.startup_timeout = 100ms;
      try { Transport::launch(silent); check(false, "Silent startup succeeded"); }
      catch (const std::exception& error) { check(std::string(error.what()).size() < 17000, "Stderr diagnostics exceeded bounded tail"); }
      silent.arguments = {"--transport-test-mode=truncated"};
      rejects([&] { Transport::launch(silent); }, "Truncated response startup accepted");
    }
    {
      auto transport = Transport::launch(options);
      const auto descendant = body(transport->call("{\"command\":\"descendant\"}")).at("pid").as_int();
      transport->close();
      check(kill(static_cast<pid_t>(descendant), 0) < 0 && errno == ESRCH, "Owned group renderer was not terminated and reaped by its parent");
    }
    for (int round = 0; round < 30; ++round) {
      auto transport = Transport::launch(options);
      auto abandoned = transport->submit("{\"command\":\"hang\"}", 2ms);
      // Abandoning a future does not abandon its deadline or pending capacity.
      abandoned.result = {};
      std::this_thread::sleep_for(4ms);
      transport->call("{\"command\":\"echo\"}");
      check(transport->stats().pending == 0, "Abandoned future leaked its pending entry");
      transport->close();
    }
    check(descriptors() == baseline_fds, "Transport lifecycle leaked file descriptors");
    auto invalid = options; invalid.executable = "relative";
    rejects([&] { Transport::launch(invalid); }, "Relative engine executable accepted");
    invalid = options; invalid.frames_per_second = 60.5;
    rejects([&] { Transport::launch(invalid); }, "Fractional native capture ceiling accepted");
    invalid = options; invalid.arguments = {"--suitecut-profile=/tmp/foreign"};
    rejects([&] { Transport::launch(invalid); }, "Managed profile override accepted");
    std::cout << "transport lifecycle, concurrency, bounded queues, 1400 seeded I/O cases and readiness churn passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "transport lifecycle failure: " << error.what() << '\n'; return 1; }
}
