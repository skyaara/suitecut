// SPDX-License-Identifier: MIT
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#include "window_frames.h"

#include <sys/resource.h>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace suitecut {
namespace {
struct LayerFrames {
  void* last_surface = nullptr;
  uint32_t last_seed = 0;
  std::vector<double> arrivals;
  std::vector<double> copy_ms;
  std::map<void*, int> surfaces;
  int repeats = 0;
  size_t width = 0, height = 0;
  OSType format = 0;
};

struct Probe {
  bool poll = false;
  dispatch_source_t timer = nil;
  CALayer* root = nil;
  double started = 0, cpu_started = 0;
  int duration_ms = 0;
  std::string png_path;
  std::function<void(std::string)> done;
  std::map<void*, LayerFrames> layers;
  std::map<void*, Class> hooked;  // layer -> class it had before the hook
  std::vector<uint8_t> last_pixels;
  size_t last_row = 0, last_width = 0, last_height = 0;
  OSType last_format = 0;
  int remote_hosts = 0, layer_count = 0, notifications = 0;
};

double Now() { return CACurrentMediaTime() * 1000.0; }

double CpuMs() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
         (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}

std::string FourCC(OSType code) {
  std::string text(4, ' ');
  for (int i = 0; i < 4; ++i) text[i] = static_cast<char>((code >> (24 - 8 * i)) & 0xff);
  return text;
}

void Walk(CALayer* layer, const std::function<void(CALayer*)>& visit) {
  visit(layer);
  for (CALayer* child in layer.sublayers) Walk(child, visit);
}

IOSurfaceRef SurfaceOf(CALayer* layer) {
  id contents = layer.contents;
  if (!contents || CFGetTypeID((__bridge CFTypeRef)contents) != IOSurfaceGetTypeID()) return nullptr;
  return (__bridge IOSurfaceRef)contents;
}

// Records one frame from |layer|. Polling skips a surface it has already seen;
// the hook counts every update, because Chromium may redraw into the surface
// the layer already shows and only mark the contents changed.
void Capture(Probe& probe, CALayer* layer, bool skip_repeats) {
  IOSurfaceRef surface = SurfaceOf(layer);
  if (!surface) return;
  auto& seen = probe.layers[(__bridge void*)layer];
  const uint32_t seed = IOSurfaceGetSeed(surface);
  const bool repeat = seen.last_surface == surface && seen.last_seed == seed;
  if (repeat && skip_repeats) return;
  if (repeat) ++seen.repeats;
  seen.last_surface = surface;
  seen.last_seed = seed;
  seen.arrivals.push_back(Now());
  seen.surfaces[surface]++;
  seen.width = IOSurfaceGetWidth(surface);
  seen.height = IOSurfaceGetHeight(surface);
  seen.format = IOSurfaceGetPixelFormat(surface);
  // Copy now: Chromium recycles these surfaces for later frames.
  const double copy_start = Now();
  IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
  const size_t row = IOSurfaceGetBytesPerRow(surface);
  const auto* base = static_cast<const uint8_t*>(IOSurfaceGetBaseAddress(surface));
  probe.last_pixels.assign(base, base + row * seen.height);
  IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
  seen.copy_ms.push_back(Now() - copy_start);
  probe.last_row = row;
  probe.last_width = seen.width;
  probe.last_height = seen.height;
  probe.last_format = seen.format;
}

// Layers being watched, keyed by layer. Only the main thread touches this.
std::map<void*, std::weak_ptr<Probe>>& Watched() {
  static std::map<void*, std::weak_ptr<Probe>> watched;
  return watched;
}

void Updated(CALayer* layer) {
  const auto found = Watched().find((__bridge void*)layer);
  if (found == Watched().end()) return;
  if (auto probe = found->second.lock()) {
    ++probe->notifications;
    Capture(*probe, layer, false);
  }
}

// A per-instance subclass, the way KVO observes one object: only the layers we
// hook change class, and each update still runs the original method first.
void Forward(Class hooked, Class base, SEL selector, id block) {
  Method method = class_getInstanceMethod(base, selector);
  if (method) class_addMethod(hooked, selector, imp_implementationWithBlock(block), method_getTypeEncoding(method));
}

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

void Discover(const std::shared_ptr<Probe>& probe) {
  Class host = NSClassFromString(@"CALayerHost");
  int hosts = 0, count = 0;
  Walk(probe->root, [&](CALayer* layer) {
    ++count;
    if (host && [layer isKindOfClass:host]) ++hosts;
    if (probe->poll) return Capture(*probe, layer, true);
    void* key = (__bridge void*)layer;
    if (!SurfaceOf(layer) || probe->hooked.count(key)) return;
    Class base = object_getClass(layer);
    Class hooked = HookedClass(base);
    if (!hooked) return;
    object_setClass(layer, hooked);
    probe->hooked[key] = base;
    Watched()[key] = probe;
    Capture(*probe, layer, false);  // the frame already on screen
  });
  probe->remote_hosts = std::max(probe->remote_hosts, hosts);
  probe->layer_count = std::max(probe->layer_count, count);
}

void Unhook(Probe& probe) {
  for (const auto& [key, base] : probe.hooked) {
    Watched().erase(key);
    auto layer = (__bridge CALayer*)key;
    // Leave the class alone if something else has since subclassed the layer.
    if ([NSStringFromClass(object_getClass(layer)) hasPrefix:@"SuiteCutFrames_"]) object_setClass(layer, base);
  }
  probe.hooked.clear();
}

bool WritePng(const Probe& probe) {
  if (probe.last_pixels.empty() || probe.last_format != 'BGRA') return false;
  auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  auto data = CFDataCreate(nullptr, probe.last_pixels.data(), probe.last_pixels.size());
  auto provider = CGDataProviderCreateWithCFData(data);
  auto image = CGImageCreate(probe.last_width, probe.last_height, 8, 32, probe.last_row, space,
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
  NSMutableArray* layers = [NSMutableArray array];
  for (const auto& [layer, seen] : probe.layers) {
    std::vector<double> gaps;
    for (size_t i = 1; i < seen.arrivals.size(); ++i) gaps.push_back(seen.arrivals[i] - seen.arrivals[i - 1]);
    std::sort(gaps.begin(), gaps.end());
    const double span = seen.arrivals.size() > 1 ? seen.arrivals.back() - seen.arrivals.front() : 0;
    double copy = 0;
    for (double ms : seen.copy_ms) copy += ms;
    [layers addObject:@{
      @"frames" : @(seen.arrivals.size()),
      @"fps" : @(span > 0 ? (seen.arrivals.size() - 1) * 1000.0 / span : 0),
      @"p50GapMs" : @(gaps.empty() ? 0 : gaps[gaps.size() / 2]),
      @"p95GapMs" : @(gaps.empty() ? 0 : gaps[gaps.size() * 95 / 100]),
      @"size" : [NSString stringWithFormat:@"%zux%zu", seen.width, seen.height],
      @"format" : @(FourCC(seen.format).c_str()),
      @"distinctSurfaces" : @(seen.surfaces.size()),
      @"sameSurfaceUpdates" : @(seen.repeats),
      @"avgCopyMs" : @(seen.copy_ms.empty() ? 0 : copy / seen.copy_ms.size()),
    }];
  }
  NSDictionary* report = @{
    @"mode" : probe.poll ? @"poll" : @"hook",
    @"layers" : layers,
    @"layerCount" : @(probe.layer_count),
    @"remoteLayerHosts" : @(probe.remote_hosts),
    @"hookedLayers" : @(probe.hooked.size()),
    @"notifications" : @(probe.notifications),
    @"processCpuMs" : @(CpuMs() - probe.cpu_started),
    @"png" : @(WritePng(probe)),
  };
  NSData* json = [NSJSONSerialization dataWithJSONObject:report options:0 error:nil];
  return std::string(static_cast<const char*>(json.bytes), json.length);
}
}  // namespace

void ProbeWindowFrames(void* handle, int duration_ms, bool poll, const std::string& png_path,
                       std::function<void(std::string)> done) {
  id object = (__bridge id)handle;
  NSView* view = [object isKindOfClass:[NSWindow class]] ? [(NSWindow*)object contentView] : (NSView*)object;
  if (!view.layer) return done("{\"error\":\"view has no layer\"}");
  auto probe = std::make_shared<Probe>();
  probe->poll = poll;
  probe->root = view.layer;
  probe->started = Now();
  probe->cpu_started = CpuMs();
  probe->duration_ms = duration_ms;
  probe->png_path = png_path;
  probe->done = std::move(done);
  // Polling checks every layer at 1 kHz. The hook only rescans at 4 Hz to find
  // layers Chromium creates later (first frame, resize); frames come from the hook.
  const uint64_t interval = poll ? NSEC_PER_MSEC : 250 * NSEC_PER_MSEC;
  probe->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(probe->timer, DISPATCH_TIME_NOW, interval, 0);
  dispatch_source_set_event_handler(probe->timer, ^{
    Discover(probe);
    if (Now() - probe->started < probe->duration_ms) return;
    dispatch_source_cancel(probe->timer);
    probe->timer = nil;
    const auto report = Report(*probe);
    Unhook(*probe);
    probe->done(report);
  });
  dispatch_resume(probe->timer);
}
}  // namespace suitecut
