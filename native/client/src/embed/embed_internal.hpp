// SPDX-License-Identifier: MIT
// Private to the embed module: errors that become embed.h statuses, the
// framed socket to suitecut-session, and the platform parts (frames and code
// signatures) that embed_apple.cpp implements.
#pragma once
#include "suitecut/embed.h"
#include "suitecut/json.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace suitecut::embed {

// A protocol refusal: becomes SC_REFUSED with "code: message".
struct Refusal : std::runtime_error {
  Refusal(std::string code_, const std::string& message) : std::runtime_error(message), code(std::move(code_)) {}
  std::string code;
};
struct Unsupported : std::runtime_error { using std::runtime_error::runtime_error; };
struct Closed : std::runtime_error { using std::runtime_error::runtime_error; };
struct TimedOut : std::runtime_error { using std::runtime_error::runtime_error; };

// One connection to the session socket: a little-endian u32 length, then
// that many bytes of JSON, each way. A request that times out or fails
// leaves the connection unusable; the caller drops it.
class Wire {
 public:
  explicit Wire(const std::string& socket_path);
  ~Wire();
  Wire(const Wire&) = delete;
  Wire& operator=(const Wire&) = delete;
  Json Call(const Json& message, std::chrono::milliseconds timeout);

 private:
  void Send(const std::string& bytes, std::chrono::steady_clock::time_point deadline);
  void Receive(char* data, std::size_t size, std::chrono::steady_clock::time_point deadline);
  int fd_ = -1;
};

struct Program {
  int exit_code = -1;
  std::string out, err;
};
// Runs |argv| to completion (killing it after |timeout|) and collects its output.
Program Run(const std::vector<std::string>& argv, std::chrono::milliseconds timeout);

// Throws when |requirement| is not met by the bundle or programs at |paths|.
// Elsewhere than macOS any requirement is Unsupported.
void CheckSignature(const std::string& bundle, const std::vector<std::string>& programs, const std::string& requirement);

// A library-owned frame receiver (SC_SINK_LIBRARY). Only macOS has one.
class FrameReceiver {
 public:
  virtual ~FrameReceiver() = default;
  virtual std::string Name() const = 0;          // what the engine is told to send to
  virtual void ExpectSender(int pid) = 0;        // frames from any other process are dropped
  virtual bool Next(std::chrono::milliseconds wait, sc_frame& out) = 0;
  static std::unique_ptr<FrameReceiver> Create(); // throws Unsupported elsewhere
};
void ReleaseFrame(sc_frame& frame);

}  // namespace suitecut::embed
