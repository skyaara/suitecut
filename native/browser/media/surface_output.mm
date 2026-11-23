// SPDX-License-Identifier: MIT
#include "media/surface_output.h"
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <servers/bootstrap.h>
#include <cstring>
#include <mutex>
#include <vector>

namespace suitecut {
namespace {
constexpr uint32_t kMessageId = 0x47425331;  // 'GBS1'
constexpr uint32_t kBgra = 0x42475241;
constexpr size_t kRingCapacity = 4;

struct SurfaceMessage {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t surface;
  uint32_t magic, width, height, reserved;
  uint64_t timestamp;
};
static_assert(sizeof(SurfaceMessage) == 64, "receivers expect a 64-byte surface message");

struct Parameters {
  uint32_t origin[2];
  int32_t popup_origin[2];
  uint32_t popup_size[2];
  uint32_t has_popup;
};

// BT.601 full range on sRGB-encoded values, which is what the receiver's
// '420f' decode inverts. The popup, when there is one, replaces the view's
// pixels inside its rectangle before conversion.
constexpr const char* kKernel = R"metal(
#include <metal_stdlib>
using namespace metal;
struct Parameters { uint2 origin; int2 popup_origin; uint2 popup_size; uint has_popup; };
float3 pixel(texture2d<float,access::read> view, texture2d<float,access::read> popup,
             constant Parameters& p, uint2 at) {
  if (p.has_popup) {
    int2 local = int2(at) - p.popup_origin;
    if (local.x >= 0 && local.y >= 0 && uint(local.x) < p.popup_size.x && uint(local.y) < p.popup_size.y)
      return popup.read(uint2(local)).rgb;
  }
  return view.read(at + p.origin).rgb;
}
kernel void nv12_full_601(texture2d<float,access::read> view [[texture(0)]],
                          texture2d<float,access::write> luma [[texture(1)]],
                          texture2d<float,access::write> chroma [[texture(2)]],
                          texture2d<float,access::read> popup [[texture(3)]],
                          constant Parameters& p [[buffer(0)]],
                          uint2 at [[thread_position_in_grid]]) {
  if (at.x >= luma.get_width() || at.y >= luma.get_height()) return;
  float3 rgb = pixel(view, popup, p, at);
  luma.write(float4(dot(rgb, float3(0.299, 0.587, 0.114)), 0, 0, 1), at);
  if (((at.x | at.y) & 1) == 0) {
    float3 mean = (rgb + pixel(view, popup, p, at + uint2(1, 0)) + pixel(view, popup, p, at + uint2(0, 1)) +
                   pixel(view, popup, p, at + uint2(1, 1))) * 0.25;
    float u = 0.5 + dot(mean, float3(-0.168736, -0.331264, 0.5));
    float v = 0.5 + dot(mean, float3(0.5, -0.418688, -0.081312));
    chroma.write(float4(u, v, 0, 1), at / 2);
  }
}
)metal";

id<MTLComputePipelineState> Converter(id<MTLDevice> device) {
  static std::once_flag once;
  static id<MTLComputePipelineState> shared = nil;
  std::call_once(once, [device] {
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:@(kKernel) options:nil error:&error];
    if (library)
      shared = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"nv12_full_601"]
                                                     error:&error];
  });
  return shared;
}

// A bare, private NV12 surface: shared only by passing its Mach port. The
// pixel buffer that makes it is released at once, since a live CVPixelBuffer
// counts as a use and would keep the surface from ever reading as free.
IOSurfaceRef MakeSurface(int width, int height) {
  NSDictionary* attributes = @{
    (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
    (id)kCVPixelBufferMetalCompatibilityKey : @YES
  };
  CVPixelBufferRef buffer = nullptr;
  if (CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                          kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
                          (__bridge CFDictionaryRef)attributes, &buffer) != kCVReturnSuccess)
    return nullptr;
  IOSurfaceRef surface = CVPixelBufferGetIOSurface(buffer);
  if (surface) CFRetain(surface);
  CVPixelBufferRelease(buffer);
  return surface;
}

uint64_t NowMicroseconds() {
  static const double scale = [] {
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    return double(info.numer) / info.denom / 1000.0;
  }();
  return static_cast<uint64_t>(mach_absolute_time() * scale);
}

bool Inside(IOSurfaceRef surface, const SurfaceOutput::Rect& rect) {
  return surface && rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 &&
         size_t(rect.x) + rect.width <= IOSurfaceGetWidth(surface) &&
         size_t(rect.y) + rect.height <= IOSurfaceGetHeight(surface) &&
         IOSurfaceGetPixelFormat(surface) == kBgra;
}
}  // namespace

struct SurfaceOutput::Impl {
  mach_port_t port = MACH_PORT_NULL;
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> queue = device ? [device newCommandQueue] : nil;
  id<MTLComputePipelineState> converter = device ? Converter(device) : nil;
  id<MTLTexture> none = nil;
  id<MTLTexture> popup = nil;
  Rect popup_at;
  int width = 0, height = 0;
  // Least to most recently sent.
  std::vector<IOSurfaceRef> ring;

  ~Impl() {
    Clear();
    if (port != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), port);
  }
  void Clear() {
    for (auto surface : ring) CFRelease(surface);
    ring.clear();
  }
  IOSurfaceRef Next(int w, int h) {
    if (w != width || h != height) {
      Clear();
      width = w;
      height = h;
    }
    // The surface sent last can read as free before the receiver has looked
    // it up, so it is never drawn into next.
    const size_t candidates = ring.size() > 1 ? ring.size() - 1 : 0;
    for (size_t i = 0; i < candidates; ++i)
      if (!IOSurfaceIsInUse(ring[i])) {
        auto surface = ring[i];
        ring.erase(ring.begin() + i);
        ring.push_back(surface);
        return surface;
      }
    if (ring.size() >= kRingCapacity) return nullptr;
    auto surface = MakeSurface(w, h);
    if (surface) ring.push_back(surface);
    return surface;
  }
  id<MTLTexture> Plane(IOSurfaceRef surface, size_t plane, MTLPixelFormat format, int w, int h,
                       MTLTextureUsage usage) {
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                         width:w
                                                                        height:h
                                                                     mipmapped:NO];
    descriptor.usage = usage;
    descriptor.storageMode = MTLStorageModeShared;
    return [device newTextureWithDescriptor:descriptor iosurface:surface plane:plane];
  }
  id<MTLTexture> Empty() {
    if (!none) {
      auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                           width:1
                                                                          height:1
                                                                       mipmapped:NO];
      descriptor.usage = MTLTextureUsageShaderRead;
      descriptor.storageMode = MTLStorageModePrivate;
      none = [device newTextureWithDescriptor:descriptor];
    }
    return none;
  }
};

SurfaceOutput::SurfaceOutput(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SurfaceOutput::~SurfaceOutput() = default;

bool SurfaceOutput::ValidName(const std::string& name) {
  if (name.empty() || name.size() > 128) return false;
  for (const unsigned char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
          c == '-' || c == '_'))
      return false;
  return true;
}

std::unique_ptr<SurfaceOutput> SurfaceOutput::Open(const std::string& name, std::string& error) {
  if (!ValidName(name)) {
    error = "Invalid surface port name";
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  if (!impl->queue || !impl->converter) {
    error = "Metal is unavailable for surface output";
    return nullptr;
  }
  if (bootstrap_look_up(bootstrap_port, name.c_str(), &impl->port) != KERN_SUCCESS) {
    impl->port = MACH_PORT_NULL;
    error = "No receiver is checked in under the surface port name";
    return nullptr;
  }
  return std::unique_ptr<SurfaceOutput>(new SurfaceOutput(std::move(impl)));
}

void SurfaceOutput::SetPopup(void* handle, Rect visible, Rect at) {
  @autoreleasepool {
    auto surface = static_cast<IOSurfaceRef>(handle);
    if (!Inside(surface, visible)) {
      impl_->popup = nil;
      return;
    }
    // The borrowed surface expires after the paint callback, so the visible
    // part is copied into a texture this output owns.
    auto source = impl_->Plane(surface, 0, MTLPixelFormatBGRA8Unorm, int(IOSurfaceGetWidth(surface)),
                               int(IOSurfaceGetHeight(surface)), MTLTextureUsageShaderRead);
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                         width:visible.width
                                                                        height:visible.height
                                                                     mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> copy = [impl_->device newTextureWithDescriptor:descriptor];
    id<MTLCommandBuffer> command = [impl_->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    if (!source || !copy || !blit) {
      impl_->popup = nil;
      return;
    }
    [blit copyFromTexture:source
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(visible.x, visible.y, 0)
               sourceSize:MTLSizeMake(visible.width, visible.height, 1)
                toTexture:copy
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [command commit];
    [command waitUntilCompleted];
    impl_->popup = command.status == MTLCommandBufferStatusCompleted ? copy : nil;
    impl_->popup_at = Rect{at.x, at.y, visible.width, visible.height};
  }
}

SurfaceOutput::Sent SurfaceOutput::Send(void* handle, Rect visible) {
  @autoreleasepool {
    auto surface = static_cast<IOSurfaceRef>(handle);
    const int width = visible.width & ~1, height = visible.height & ~1;
    if (width < 2 || height < 2 || !Inside(surface, Rect{visible.x, visible.y, width, height}))
      return Sent::dropped;
    auto target = impl_->Next(width, height);
    if (!target) return Sent::dropped;
    auto view = impl_->Plane(surface, 0, MTLPixelFormatBGRA8Unorm, int(IOSurfaceGetWidth(surface)),
                             int(IOSurfaceGetHeight(surface)), MTLTextureUsageShaderRead);
    auto luma = impl_->Plane(target, 0, MTLPixelFormatR8Unorm, width, height, MTLTextureUsageShaderWrite);
    auto chroma =
        impl_->Plane(target, 1, MTLPixelFormatRG8Unorm, width / 2, height / 2, MTLTextureUsageShaderWrite);
    id<MTLCommandBuffer> command = [impl_->queue commandBuffer];
    id<MTLComputeCommandEncoder> compute = [command computeCommandEncoder];
    if (!view || !luma || !chroma || !compute) return Sent::dropped;
    Parameters parameters{};
    parameters.origin[0] = uint32_t(visible.x);
    parameters.origin[1] = uint32_t(visible.y);
    if (impl_->popup) {
      parameters.has_popup = 1;
      parameters.popup_origin[0] = impl_->popup_at.x;
      parameters.popup_origin[1] = impl_->popup_at.y;
      parameters.popup_size[0] = uint32_t(impl_->popup_at.width);
      parameters.popup_size[1] = uint32_t(impl_->popup_at.height);
    }
    [compute setComputePipelineState:impl_->converter];
    [compute setTexture:view atIndex:0];
    [compute setTexture:luma atIndex:1];
    [compute setTexture:chroma atIndex:2];
    [compute setTexture:impl_->popup ? impl_->popup : impl_->Empty() atIndex:3];
    [compute setBytes:&parameters length:sizeof parameters atIndex:0];
    [compute dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 8, 1)];
    [compute endEncoding];
    [command commit];
    // CEF's surface is borrowed for this callback only, so the copy finishes
    // before returning.
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return Sent::dropped;

    mach_port_t right = IOSurfaceCreateMachPort(target);
    if (right == MACH_PORT_NULL) return Sent::dropped;
    SurfaceMessage message;
    std::memset(&message, 0, sizeof message);
    message.header.msgh_bits =
        MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, MACH_MSGH_BITS_COMPLEX);
    message.header.msgh_size = sizeof message;
    message.header.msgh_remote_port = impl_->port;
    message.header.msgh_id = kMessageId;
    message.body.msgh_descriptor_count = 1;
    message.surface.name = right;
    message.surface.disposition = MACH_MSG_TYPE_MOVE_SEND;
    message.surface.type = MACH_MSG_PORT_DESCRIPTOR;
    message.magic = kMessageId;
    message.width = uint32_t(width);
    message.height = uint32_t(height);
    message.timestamp = NowMicroseconds();
    const auto result = mach_msg(&message.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof message, 0,
                                 MACH_PORT_NULL, 0, MACH_PORT_NULL);
    switch (result) {
      case MACH_MSG_SUCCESS:
        return Sent::delivered;
      case MACH_SEND_TIMED_OUT:
      case MACH_SEND_INTERRUPTED:
        // Pseudo-received: the kernel handed both rights back to this task.
        mach_msg_destroy(&message.header);
        return Sent::dropped;
      case MACH_SEND_INVALID_DEST:
        mach_port_deallocate(mach_task_self(), right);
        return Sent::closed;
      default:
        mach_port_deallocate(mach_task_self(), right);
        return Sent::dropped;
    }
  }
}
}  // namespace suitecut
