// SPDX-License-Identifier: MIT
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#include "app/window_frames.h"

#include <algorithm>
#include <map>
#include <vector>

namespace suitecut {
namespace {
IOSurfaceRef SurfaceOf(CALayer* layer) {
  id contents = layer.contents;
  if (!contents || CFGetTypeID((__bridge CFTypeRef)contents) != IOSurfaceGetTypeID())
    return nullptr;
  return (__bridge IOSurfaceRef)contents;
}

void Walk(CALayer* layer, const std::function<void(CALayer*)>& visit) {
  visit(layer);
  for (CALayer* child in layer.sublayers) Walk(child, visit);
}
}  // namespace

double WindowFramesNow() { return CACurrentMediaTime() * 1000.0; }

struct WindowFrames::Impl {
  NSView* view = nil;
  Frame frame;
  dispatch_source_t rescan = nil;
  std::vector<CALayer*> hooked;  // retained so unhooking never touches a freed layer
  std::map<void*, Class> original;
  CALayer* primary = nil;
  bool remote = false;

  void Deliver(CALayer* layer) {
    if (layer != primary) return;
    IOSurfaceRef surface = SurfaceOf(layer);
    if (!surface) return;
    frame(surface, static_cast<int>(IOSurfaceGetWidth(surface)),
          static_cast<int>(IOSurfaceGetHeight(surface)), WindowFramesNow());
  }
  void Discover(const std::shared_ptr<Impl>& self, bool deliver);
  void Unhook();
};

namespace {
// Layers being watched, keyed by layer. Only the main thread touches this.
std::map<void*, std::weak_ptr<WindowFrames::Impl>>& Watched() {
  static std::map<void*, std::weak_ptr<WindowFrames::Impl>> watched;
  return watched;
}

void Updated(CALayer* layer) {
  const auto found = Watched().find((__bridge void*)layer);
  if (found == Watched().end()) return;
  if (auto impl = found->second.lock()) impl->Deliver(layer);
}

void Forward(Class hooked, Class base, SEL selector, id block) {
  Method method = class_getInstanceMethod(base, selector);
  if (method)
    class_addMethod(hooked, selector, imp_implementationWithBlock(block),
                    method_getTypeEncoding(method));
}

// A per-instance subclass, the way KVO observes one object: only the layers we
// hook change class, and each update runs the original method first.
Class HookedClass(Class base) {
  NSString* name = [@"SuiteCutFrames_" stringByAppendingString:NSStringFromClass(base)];
  if (Class existing = NSClassFromString(name)) return existing;
  Class hooked = objc_allocateClassPair(base, name.UTF8String, 0);
  if (!hooked) return nil;
  SEL set_contents = @selector(setContents:);
  IMP original_set = class_getMethodImplementation(base, set_contents);
  Forward(hooked, base, set_contents, ^(CALayer* layer, id contents) {
    reinterpret_cast<void (*)(id, SEL, id)>(original_set)(layer, set_contents, contents);
    Updated(layer);
  });
  // Private CALayer method Chromium calls when it redrew the surface in place.
  SEL changed = NSSelectorFromString(@"setContentsChanged");
  IMP original_changed = class_getMethodImplementation(base, changed);
  Forward(hooked, base, changed, ^(CALayer* layer) {
    reinterpret_cast<void (*)(id, SEL)>(original_changed)(layer, changed);
    Updated(layer);
  });
  objc_registerClassPair(hooked);
  return hooked;
}
}  // namespace

void WindowFrames::Impl::Discover(const std::shared_ptr<Impl>& self, bool deliver) {
  if (!view.layer) return;
  Class host = NSClassFromString(@"CALayerHost");
  CALayer* largest = nil;
  size_t largest_area = 0;
  Walk(view.layer, [&](CALayer* layer) {
    if (host && [layer isKindOfClass:host]) remote = true;
    IOSurfaceRef surface = SurfaceOf(layer);
    if (!surface) return;
    const size_t area = IOSurfaceGetWidth(surface) * IOSurfaceGetHeight(surface);
    if (area > largest_area) largest = layer, largest_area = area;
    void* key = (__bridge void*)layer;
    if (original.count(key)) return;
    Class base = object_getClass(layer);
    Class hooked_class = HookedClass(base);
    if (!hooked_class) return;
    object_setClass(layer, hooked_class);
    original[key] = base;
    hooked.push_back(layer);
    Watched()[key] = self;
  });
  // The window's own compositor surface is the largest; follow it if Chromium
  // replaces it (first frame, resize).
  if (largest && largest != primary) {
    primary = largest;
    if (deliver) Deliver(primary);
  }
}

void WindowFrames::Impl::Unhook() {
  for (CALayer* layer : hooked) {
    void* key = (__bridge void*)layer;
    Watched().erase(key);
    // Leave the class alone if something else has since subclassed the layer.
    if ([NSStringFromClass(object_getClass(layer)) hasPrefix:@"SuiteCutFrames_"])
      object_setClass(layer, original[key]);
  }
  hooked.clear();
  original.clear();
  primary = nil;
}

WindowFrames::WindowFrames(void* handle, Frame frame) : impl_(std::make_shared<Impl>()) {
  id object = (__bridge id)handle;
  impl_->view =
      [object isKindOfClass:[NSWindow class]] ? [(NSWindow*)object contentView] : (NSView*)object;
  impl_->frame = std::move(frame);
  impl_->Discover(impl_, true);
  // Frames come from the hook; this only finds layers Chromium creates later.
  std::weak_ptr<Impl> weak = impl_;
  impl_->rescan =
      dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(impl_->rescan, dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC),
                            250 * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
  dispatch_source_set_event_handler(impl_->rescan, ^{
    if (auto impl = weak.lock()) impl->Discover(impl, true);
  });
  dispatch_resume(impl_->rescan);
}

WindowFrames::~WindowFrames() {
  dispatch_source_cancel(impl_->rescan);
  impl_->rescan = nil;
  impl_->Unhook();
}

bool WindowFrames::Remote() const { return impl_->remote; }

void WindowFrames::Redeliver() {
  if (impl_->primary) impl_->Deliver(impl_->primary);
}

namespace {
struct Probe {
  std::unique_ptr<WindowFrames> frames;
  std::vector<double> arrivals, copy_ms;
  std::map<void*, int> surfaces;
  std::vector<uint8_t> pixels;
  size_t row = 0, width = 0, height = 0;
  OSType format = 0;
  std::string png_path;
};

std::string FourCC(OSType code) {
  std::string text(4, ' ');
  for (int i = 0; i < 4; ++i) text[i] = static_cast<char>((code >> (24 - 8 * i)) & 0xff);
  return text;
}

bool WritePng(const Probe& probe) {
  if (probe.pixels.empty() || probe.format != 'BGRA') return false;
  auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  auto data = CFDataCreate(nullptr, probe.pixels.data(), probe.pixels.size());
  auto provider = CGDataProviderCreateWithCFData(data);
  auto image = CGImageCreate(probe.width, probe.height, 8, 32, probe.row, space,
                             static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) |
                                 static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst),
                             provider, nullptr, false, kCGRenderingIntentDefault);
  auto url = (__bridge CFURLRef)[NSURL fileURLWithPath:@(probe.png_path.c_str())];
  auto destination = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
  bool ok = false;
  if (destination && image) {
    CGImageDestinationAddImage(destination, image, nullptr);
    ok = CGImageDestinationFinalize(destination);
  }
  if (destination) CFRelease(destination);
  if (image) CGImageRelease(image);
  CGDataProviderRelease(provider);
  CFRelease(data);
  CGColorSpaceRelease(space);
  return ok;
}

std::string Report(const Probe& probe) {
  std::vector<double> gaps;
  for (size_t i = 1; i < probe.arrivals.size(); ++i)
    gaps.push_back(probe.arrivals[i] - probe.arrivals[i - 1]);
  std::sort(gaps.begin(), gaps.end());
  const double span =
      probe.arrivals.size() > 1 ? probe.arrivals.back() - probe.arrivals.front() : 0;
  double copy = 0;
  for (double ms : probe.copy_ms) copy += ms;
  NSDictionary* report = @{
    @"frames" : @(probe.arrivals.size()),
    @"fps" : @(span > 0 ? (probe.arrivals.size() - 1) * 1000.0 / span : 0),
    @"p50GapMs" : @(gaps.empty() ? 0 : gaps[gaps.size() / 2]),
    @"p95GapMs" : @(gaps.empty() ? 0 : gaps[gaps.size() * 95 / 100]),
    @"size" : [NSString stringWithFormat:@"%zux%zu", probe.width, probe.height],
    @"format" : @(FourCC(probe.format).c_str()),
    @"distinctSurfaces" : @(probe.surfaces.size()),
    @"avgCopyMs" : @(probe.copy_ms.empty() ? 0 : copy / probe.copy_ms.size()),
    @"remote" : @(probe.frames->Remote()),
    @"png" : @(WritePng(probe)),
  };
  NSData* json = [NSJSONSerialization dataWithJSONObject:report options:0 error:nil];
  return std::string(static_cast<const char*>(json.bytes), json.length);
}
}  // namespace

void ProbeWindowFrames(void* handle, int duration_ms, const std::string& png_path,
                       std::function<void(std::string)> done) {
  auto probe = std::make_shared<Probe>();
  probe->png_path = png_path;
  Probe* raw = probe.get();
  probe->frames = std::make_unique<WindowFrames>(
      handle, [raw](void* io_surface, int width, int height, double now) {
        auto surface = static_cast<IOSurfaceRef>(io_surface);
        raw->arrivals.push_back(now);
        raw->surfaces[surface]++;
        raw->width = width;
        raw->height = height;
        raw->format = IOSurfaceGetPixelFormat(surface);
        const double start = WindowFramesNow();
        IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
        raw->row = IOSurfaceGetBytesPerRow(surface);
        const auto* base = static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(surface));
        raw->pixels.assign(base, base + raw->row * height);
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
        raw->copy_ms.push_back(WindowFramesNow() - start);
      });
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(duration_ms) * NSEC_PER_MSEC),
      dispatch_get_main_queue(), ^{
        const auto report = Report(*probe);
        probe->frames.reset();
        done(report);
      });
}
}  // namespace suitecut
