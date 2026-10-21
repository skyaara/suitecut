// SPDX-License-Identifier: MIT
#include "suitecut/client.h"
#include "suitecut/json.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void send_packet(const suitecut::Json& header) {
  const auto text = header.dump();
  const auto size = static_cast<std::uint32_t>(text.size());
  const char prefix[] = {static_cast<char>(size), static_cast<char>(size >> 8),
                        static_cast<char>(size >> 16), static_cast<char>(size >> 24)};
  std::cout.write(prefix, 4);
  std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
  std::cout.flush();
}
int fake_engine() {
  send_packet(suitecut::Json::object{{"version", 1}, {"type", "ready"}, {"pageId", 1}, {"bytes", 0}});
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto command = suitecut::Json::parse(line);
    if (command.at("command").as_string() == "hold") std::this_thread::sleep_for(300ms);
    send_packet(suitecut::Json::object{{"version", 1}, {"type", "response"},
      {"id", command.at("id")}, {"ok", true}, {"bytes", 0}});
  }
  return 0;
}
using Client = std::unique_ptr<sc_client, decltype(&sc_client_destroy)>;
using Request = std::unique_ptr<sc_request, decltype(&sc_request_destroy)>;
}

int main(int argc, char** argv) {
  try {
    for (int index = 1; index < argc; ++index)
      if (std::string(argv[index]) == "--review-fake-engine") return fake_engine();
    sc_launch_options options;
    sc_launch_options_init(&options);
    const auto executable = std::filesystem::canonical(argv[0]).string();
    const char* arguments[] = {"--review-fake-engine"};
    options.executable = executable.c_str();
    options.arguments = arguments;
    options.argument_count = 1;
    options.startup_timeout_ms = 3000;
    options.shutdown_timeout_ms = 1000;
    sc_error error{};
    sc_client* raw_client = nullptr;
    require(sc_client_launch(&options, &raw_client, &error) == SC_OK, error.message);
    Client client(raw_client, sc_client_destroy);
    constexpr char command[] = "{\"command\":\"hold\"}";
    sc_request* raw_request = nullptr;
    require(sc_client_submit(client.get(), command, sizeof(command) - 1, 1500,
      &raw_request, &error) == SC_OK, error.message);
    Request request(raw_request, sc_request_destroy);
    std::atomic<bool> started{false}, done{false};
    std::atomic<sc_status> first_status{SC_ERROR};
    std::jthread first([&] {
      sc_error first_error{};
      sc_packet* packet = nullptr;
      started = true;
      first_status = sc_request_wait(request.get(), 1500, &packet, &first_error);
      sc_packet_destroy(packet);
      done = true;
    });
    while (!started.load()) std::this_thread::yield();
    std::this_thread::sleep_for(30ms);
    require(!done.load(), "Fixture returned before concurrent waiter started");
    sc_packet* packet = reinterpret_cast<sc_packet*>(1);
    const auto begin = std::chrono::steady_clock::now();
    const auto status = sc_request_wait(request.get(), 1, &packet, &error);
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    require(status == SC_TIMEOUT && packet == nullptr,
      "Short concurrent waiter must time out independently of long waiter");
    require(elapsed < 100ms, "Concurrent waiter exceeded its own deadline budget");
    const auto zero_begin = std::chrono::steady_clock::now();
    require(sc_request_wait(request.get(), 0, &packet, &error) == SC_TIMEOUT,
      "Zero-time concurrent poll must not wait for another waiter");
    require(std::chrono::steady_clock::now() - zero_begin < 100ms,
      "Zero-time concurrent poll blocked on another waiter");
    first.join();
    require(first_status.load() == SC_OK, "Original waiter did not receive response");
    require(sc_request_wait(request.get(), 0, &packet, &error) == SC_ALREADY_CONSUMED,
      "A response must be consumed exactly once");
    sc_request* surviving_raw = nullptr;
    require(sc_client_submit(client.get(), command, sizeof(command) - 1, 1500,
      &surviving_raw, &error) == SC_OK, error.message);
    Request surviving(surviving_raw, sc_request_destroy);
    require(sc_client_close(client.get(), &error) == SC_OK, error.message);
    client.reset();
    require(sc_request_wait(surviving.get(), 0, &packet, &error) == SC_ERROR,
      "A request may outlive the closed/destroyed client and report its close error");
    require(sc_request_wait(surviving.get(), 0, &packet, &error) == SC_ALREADY_CONSUMED,
      "A close error must also be consumed exactly once");
    std::cout << "Concurrent C ABI waits and surviving-request lifecycle passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
