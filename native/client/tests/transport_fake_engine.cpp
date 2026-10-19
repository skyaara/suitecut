// SPDX-License-Identifier: MIT
#include "suitecut/json.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

using suitecut::Json;
namespace {
void bytes(const void* data, std::size_t size, bool fragmented = false) {
  const auto* p = static_cast<const char*>(data);
  while (size) {
    const auto count = ::write(STDOUT_FILENO, p, fragmented ? 1 : size);
    if (count <= 0) std::_Exit(0);
    p += count; size -= static_cast<std::size_t>(count);
  }
}
void packet(Json header, std::string body = {}, bool fragmented = false) {
  header["version"] = 1; header["bytes"] = body.size();
  const auto encoded = header.dump();
  const auto size = static_cast<std::uint32_t>(encoded.size());
  const std::array<unsigned char, 4> prefix{static_cast<unsigned char>(size),
    static_cast<unsigned char>(size >> 8), static_cast<unsigned char>(size >> 16), static_cast<unsigned char>(size >> 24)};
  bytes(prefix.data(), prefix.size(), fragmented); bytes(encoded.data(), encoded.size(), fragmented);
  bytes(body.data(), body.size(), fragmented);
}
void response(int id, std::string body = {}, bool fragmented = false, bool ok = true) {
  packet(Json::object{{"type", "response"}, {"id", id}, {"ok", ok}, {"error", ok ? "" : "fixture rejection"}}, std::move(body), fragmented);
}
} // namespace
int main(int argc, char** argv) {
  std::string mode, profile;
  bool on_demand = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.starts_with("--transport-test-mode=")) mode = arg.substr(22);
    if (arg.starts_with("--suitecut-profile=")) profile = arg.substr(19);
    if (arg == "--suitecut-frame-transport=on-demand") on_demand = true;
  }
  if (mode == "stderr-silent") {
    std::string diagnostic(256 * 1024, 'x');
    for (std::size_t offset = 0; offset < diagnostic.size();) {
      const auto written = ::write(STDERR_FILENO, diagnostic.data() + offset, diagnostic.size() - offset);
      if (written <= 0) break;
      offset += static_cast<std::size_t>(written);
    }
    for (;;) pause();
  }
  if (mode == "truncated") {
    const std::array<unsigned char, 6> broken{50,0,0,0,'{','"'};
    bytes(broken.data(), broken.size()); close(STDOUT_FILENO); for (;;) pause();
  }
  packet(Json::object{{"type", "ready"}, {"pageId", 1}, {"profile", profile}, {"onDemand", on_demand}, {"pid", getpid()}});
  if (mode == "closed-input") { close(STDIN_FILENO); for (;;) pause(); }
  if (mode == "blocked") { signal(SIGTERM, SIG_IGN); for (;;) pause(); }
  pid_t descendant = -1;
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto message = Json::parse(line);
    const auto id = static_cast<int>(message.at("id").as_int());
    const auto command = message.at("command").as_string();
    if (command == "hang") continue;
    if (command == "cancel") { response(id); continue; }
    if (command == "late") std::this_thread::sleep_for(std::chrono::milliseconds(message.at("delay").as_int()));
    if (command == "error") { response(id, "{\"detail\":9}", false, false); continue; }
    if (command == "large-frame") packet(Json::object{{"type", "frame"}, {"pageId", 1}, {"width", 3840}, {"height", 2160}}, std::string(3840 * 2160 * 4, 'f'));
    if (command == "flood-critical" || command == "flood-media") {
      const int count = message.contains("count") ? static_cast<int>(message.at("count").as_int()) : 100;
      for (int index = 0; index < count; ++index) {
        if (command == "flood-media") packet(Json::object{{"type", "frame"}, {"pageId", 1}, {"width", 2}, {"height", 2}, {"seq", index}}, std::string(16, 'f'));
        else packet(Json::object{{"type", "dialog"}, {"pageId", 1}, {"seq", index}});
      }
      if (command == "flood-media") packet(Json::object{{"type", "dialog"}, {"pageId", 1}, {"marker", "must-survive"}});
    }
    if (command == "ready-page") packet(Json::object{{"type", "ready"}, {"pageId", message.at("page")}});
    if (command == "close-page") packet(Json::object{{"type", "context"}}, Json(Json::object{{"event", "page-closed"}, {"pageId", message.at("page")}}).dump());
    if (command == "descendant") {
      descendant = fork();
      if (descendant == 0) { close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO); for (;;) pause(); }
      signal(SIGTERM, SIG_IGN);
      response(id, Json(Json::object{{"pid", descendant}}).dump()); continue;
    }
    response(id, message.dump(), command == "fragment");
  }
  if (descendant > 0) while (waitpid(descendant, nullptr, 0) < 0 && errno == EINTR) {}
}
