// SPDX-License-Identifier: MIT
#include "suitecut/transport.hpp"
#include "suitecut/json.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
using suitecut::Json;
using namespace std::chrono_literals;
void check(bool ok, const char* error) {
  if (!ok) throw std::runtime_error(error);
}
int main(int argc, char** argv) {
  try {
    suitecut::LaunchOptions options;
    options.executable =
        (argc > 1 ? std::filesystem::absolute(argv[1])
                  : std::filesystem::absolute(argv[0]).parent_path() / "transport_fake_engine")
            .string();
    options.max_pending = 1;
    options.startup_timeout = 5s;
    options.shutdown_timeout = 250ms;
    {
      auto transport = suitecut::Transport::launch(options);
      // No poll loop runs while the engine holds this reply behind exact-body
      // receipts. Internal ACKs must bypass the occupied public request slot.
      auto reply = transport->call_json(Json{{"command", "network-body"}}, 2s);
      check(Json::parse(reply.header).at("ok").as_bool(), "Publication did not progress");
      std::map<std::string, std::vector<std::uint8_t>> copies;
      for (int i = 0; i < 6; ++i) {
        auto packet = transport->poll(1s);
        check(packet.has_value(), "Exact body chunk lost");
        auto h = Json::parse(packet->header);
        auto key = h.at("type") == Json("networkbody")
                       ? "page:" + std::to_string(h.at("pageId").as_int())
                       : "worker:" + h.at("owner").as_string();
        auto& bytes = copies[key];
        check(h.at("offset").as_int() == static_cast<std::int64_t>(bytes.size()),
              "Body chunk reordered");
        bytes.insert(bytes.end(), packet->payload.begin(), packet->payload.end());
      }
      check(copies.size() == 3, "Page and worker body IDs collided");
      for (auto& [key, bytes] : copies) {
        (void)key;
        check(bytes == std::vector<std::uint8_t>({0, 255, 'x', '\r', '\n'}),
              "Binary POST body changed");
      }
      check(!transport->poll(0ms), "Internal ACK replies leaked as events");
      transport->close();
    }
    {
      auto transport = suitecut::Transport::launch(options);
      auto reply = transport->call_json(Json{{"command", "body-retire"}}, 2s);
      check(Json::parse(reply.header).at("ok").as_bool(),
            "Closed pages or contexts leaked partial-body reservations");
      transport->close();
    }
    {
      // No suitecut body or chunk size cap: a body past the old 2 MiB limit,
      // sent in a chunk past the old 24 KiB limit, passes through whole.
      options.arguments = {"--transport-test-mode=body-large"};
      auto transport = suitecut::Transport::launch(options);
      auto reply = transport->call_json(Json{{"command", "network-body"}}, 5s);
      check(Json::parse(reply.header).at("ok").as_bool(),
            "Large body publication did not progress");
      std::map<std::string, std::size_t> sizes;
      for (int i = 0; i < 6; ++i) {
        auto packet = transport->poll(1s);
        check(packet.has_value(), "Large body chunk lost");
        auto h = Json::parse(packet->header);
        auto key = h.at("type") == Json("networkbody")
                       ? "page:" + std::to_string(h.at("pageId").as_int())
                       : "worker:" + h.at("owner").as_string();
        sizes[key] += packet->payload.size();
      }
      check(sizes.size() == 3, "Large page and worker body IDs collided");
      for (auto& [key, size] : sizes) {
        (void)key;
        check(size == 2097153, "Body past the old 2 MiB cap was clipped or rejected");
      }
      transport->close();
      options.arguments = {};
    }
    // A bad body costs only that body: no events for an unidentifiable one, a
    // canceled chunk for one that went wrong, and the transport stays usable.
    for (auto [mode, chunks, cancels] :
         std::initializer_list<std::tuple<const char*, int, int>>{{"body-gap", 0, 3},
                                                                  {"body-identity", 0, 0},
                                                                  {"body-duplicate", 3, 3},
                                                                  {"body-size-change", 3, 3}}) {
      options.arguments = {std::string("--transport-test-mode=") + mode};
      auto transport = suitecut::Transport::launch(options);
      bool timed = false;
      try {
        transport->call_json(Json{{"command", "network-body"}}, 500ms);
      } catch (const std::exception& error) {
        timed = std::string(error.what()).find("timed out") != std::string::npos;
      }
      check(timed, "A dropped body's publication should only time out its own request");
      auto echo = transport->call_json(Json{{"command", "echo"}}, 2s);
      check(Json::parse(echo.header).at("ok").as_bool(), "A bad body closed the shared transport");
      int seen = 0, canceled = 0;
      while (auto packet = transport->poll(100ms)) {
        auto h = Json::parse(packet->header);
        if (h.contains("canceled") && h.at("canceled").as_bool()) {
          check(h.at("dropped").as_bool() && packet->payload.empty(),
                "Drop notice is not an empty canceled chunk");
          ++canceled;
        } else
          ++seen;
      }
      check(seen == chunks && canceled == cancels, "Unexpected events for a dropped body");
      check(transport->stats().dropped_bodies > 0, "Dropped body was not counted");
      transport->close();
    }
    std::cout << "Automatic page/worker body receipts, uncapped bodies, binary ownership and bad "
                 "bodies dropped alone passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
