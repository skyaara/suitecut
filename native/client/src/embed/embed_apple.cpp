// SPDX-License-Identifier: MIT
// The macOS parts of embed.h: receiving the engine's IOSurface frames on a
// Mach port the library checks in, and checking a runtime's code signature.
// Elsewhere both are unsupported.
#include "embed_internal.hpp"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <Security/Security.h>
#include <bsm/libbsm.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#endif

namespace suitecut::embed {

#ifdef __APPLE__
namespace {
// The engine's frame message (surface_output.h): one IOSurface send right and
// its size, 64 bytes in all.
constexpr std::uint32_t kFrameMagic = 0x47425331;  // 'GBS1'
struct FrameMessage {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t surface;
  std::uint32_t magic, width, height, reserved;
  std::uint64_t timestamp;
};
static_assert(sizeof(FrameMessage) == 64, "the engine sends 64-byte frame messages");
struct ReceiveBuffer {
  FrameMessage message;
  mach_msg_audit_trailer_t trailer;
};

void drop(IOSurfaceRef surface) {
  if (!surface) return;
  IOSurfaceDecrementUseCount(surface);
  CFRelease(surface);
}

class MachReceiver final : public FrameReceiver {
 public:
  MachReceiver() {
    char name[128];
    std::snprintf(name, sizeof name, "com.suitecut.embed.%d.%08x%08x", static_cast<int>(::getpid()), arc4random(),
                  arc4random());
    name_ = name;
    const auto checked = bootstrap_check_in(bootstrap_port, name_.c_str(), &port_);
    if (checked != KERN_SUCCESS)
      throw std::runtime_error("Cannot check in the frame receiver: " + std::string(bootstrap_strerror(checked)));
    thread_ = std::thread([this] { Loop(); });
  }
  ~MachReceiver() override {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    if (newest_) drop(static_cast<IOSurfaceRef>(newest_->surface));
    // Dropping the receive right withdraws the bootstrap name too.
    mach_port_mod_refs(mach_task_self(), port_, MACH_PORT_RIGHT_RECEIVE, -1);
  }
  std::string Name() const override { return name_; }
  void ExpectSender(int pid) override { sender_ = pid; }
  bool Next(std::chrono::milliseconds wait, sc_frame& out) override {
    std::unique_lock lock(mutex_);
    if (!ready_.wait_for(lock, wait, [&] { return newest_.has_value(); })) return false;
    const auto size = out.struct_size;
    const auto version = out.abi_version;
    out = *newest_;
    out.struct_size = size;
    out.abi_version = version;
    out.received = received_;
    newest_.reset();  // the caller owns the surface's use count now
    return true;
  }

 private:
  void Loop() {
    while (!stop_) {
      ReceiveBuffer buffer{};
      const auto received = mach_msg(&buffer.message.header,
                                     MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
                                         MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
                                     0, sizeof buffer, port_, 100, MACH_PORT_NULL);
      if (received == MACH_RCV_TIMED_OUT || received == MACH_RCV_INTERRUPTED) continue;
      if (received != MACH_MSG_SUCCESS) continue;  // MACH_RCV_TOO_LARGE: the kernel destroyed it
      const auto& message = buffer.message;
      const bool shaped = message.header.msgh_size == sizeof(FrameMessage) &&
                          message.header.msgh_id == static_cast<mach_msg_id_t>(kFrameMagic) &&
                          (message.header.msgh_bits & MACH_MSGH_BITS_COMPLEX) && message.body.msgh_descriptor_count == 1 &&
                          message.surface.type == MACH_MSG_PORT_DESCRIPTOR &&
                          message.surface.disposition == MACH_MSG_TYPE_PORT_SEND &&
                          message.surface.name != MACH_PORT_NULL && message.magic == kFrameMagic &&
                          message.reserved == 0 && message.width > 0 && message.height > 0;
      // Only the engine may send frames. Until its pid is known, none is taken.
      const int sender = sender_;
      const bool audited = buffer.trailer.msgh_trailer_type == MACH_MSG_TRAILER_FORMAT_0 &&
                           buffer.trailer.msgh_trailer_size >= sizeof(mach_msg_audit_trailer_t) && sender > 0 &&
                           audit_token_to_pid(buffer.trailer.msgh_audit) == sender;
      if (!shaped || !audited) {
        mach_msg_destroy(&buffer.message.header);
        continue;
      }
      IOSurfaceRef surface = IOSurfaceLookupFromMachPort(message.surface.name);
      mach_port_deallocate(mach_task_self(), message.surface.name);
      if (!surface) continue;
      if (IOSurfaceGetPixelFormat(surface) != SC_PIXEL_NV12_FULL_601) {
        CFRelease(surface);
        continue;
      }
      // Raised before anyone can read it, so the engine never draws into a
      // surface that is waiting here or being read.
      IOSurfaceIncrementUseCount(surface);
      sc_frame frame{};
      frame.width = message.width;
      frame.height = message.height;
      frame.pixel_format = SC_PIXEL_NV12_FULL_601;
      frame.timestamp_us = message.timestamp;
      frame.surface = const_cast<void*>(static_cast<const void*>(surface));
      std::lock_guard lock(mutex_);
      ++received_;
      if (newest_) drop(static_cast<IOSurfaceRef>(newest_->surface));
      newest_ = frame;
      ready_.notify_all();
    }
  }

  std::string name_;
  mach_port_t port_ = MACH_PORT_NULL;
  std::atomic<int> sender_{0};
  std::atomic<bool> stop_{false};
  std::mutex mutex_;
  std::condition_variable ready_;
  std::optional<sc_frame> newest_;
  std::uint64_t received_ = 0;
  std::thread thread_;
};

struct CF {
  CFTypeRef value = nullptr;
  ~CF() {
    if (value) CFRelease(value);
  }
};
std::string describe(OSStatus status) {
  CF text{SecCopyErrorMessageString(status, nullptr)};
  char buffer[512] = {};
  if (text.value) CFStringGetCString(static_cast<CFStringRef>(text.value), buffer, sizeof buffer, kCFStringEncodingUTF8);
  return std::string(buffer) + " (" + std::to_string(status) + ")";
}
void check_code(const std::string& path, SecRequirementRef requirement) {
  CF url{CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
                                                 static_cast<CFIndex>(path.size()), std::filesystem::is_directory(path))};
  SecStaticCodeRef code = nullptr;
  auto status = SecStaticCodeCreateWithPath(static_cast<CFURLRef>(url.value), kSecCSDefaultFlags, &code);
  CF owned{code};
  if (status != errSecSuccess) throw std::runtime_error("Cannot read the signature of " + path + ": " + describe(status));
  status = SecStaticCodeCheckValidity(code, kSecCSCheckAllArchitectures | kSecCSCheckNestedCode | kSecCSStrictValidate,
                                      requirement);
  if (status != errSecSuccess) throw std::runtime_error("The signature of " + path + " fails: " + describe(status));
}
}  // namespace

std::unique_ptr<FrameReceiver> FrameReceiver::Create() { return std::make_unique<MachReceiver>(); }

void ReleaseFrame(sc_frame& frame) {
  drop(static_cast<IOSurfaceRef>(frame.surface));
  frame.surface = nullptr;
}

void CheckSignature(const std::string& bundle, const std::vector<std::string>& programs, const std::string& requirement) {
  CF text{CFStringCreateWithCString(nullptr, requirement.c_str(), kCFStringEncodingUTF8)};
  if (!text.value) throw std::invalid_argument("The signing requirement is not UTF-8");
  SecRequirementRef parsed = nullptr;
  const auto status = SecRequirementCreateWithString(static_cast<CFStringRef>(text.value), kSecCSDefaultFlags, &parsed);
  CF owned{parsed};
  if (status != errSecSuccess) throw std::invalid_argument("The signing requirement does not parse: " + describe(status));
  const std::filesystem::path root(bundle);
  if (root.extension() == ".app" || std::filesystem::is_directory(root / "Contents" / "MacOS")) {
    check_code(bundle, parsed);  // the bundle, with every nested program
  } else {
    for (const auto& program : programs) check_code(program, parsed);
  }
}

#else

std::unique_ptr<FrameReceiver> FrameReceiver::Create() {
  throw Unsupported("unsupported: Library frame sinks exist only on macOS so far");
}
void ReleaseFrame(sc_frame& frame) { frame.surface = nullptr; }
void CheckSignature(const std::string&, const std::vector<std::string>&, const std::string&) {
  throw Unsupported("unsupported: Code signing requirements are checked only on macOS");
}

#endif

}  // namespace suitecut::embed
