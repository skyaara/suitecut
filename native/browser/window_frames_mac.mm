// SPDX-License-Identifier: MIT
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>
#include "window_frames.h"

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
  size_t width = 0, height = 0;
  OSType format = 0;
};

struct Probe {
  dispatch_source_t timer = nil;
  CALayer* root = nil;
  double started = 0;
  int duration_ms = 0;
  std::string png_path;
  std::function<void(std::string)> done;
  std::map<void*, LayerFrames> layers;
  std::vector<uint8_t> last_pixels;
  size_t last_row = 0, last_width = 0, last_height = 0;
  OSType last_format = 0;
  int remote_hosts = 0, layer_count = 0;
};

double Now() { return CACurrentMediaTime() * 1000.0; }

std::string FourCC(OSType code) {
  std::string text(4, ' ');
  for (int i = 0; i < 4; ++i) text[i] = static_cast<char>((code >> (24 - 8 * i)) & 0xff);
  return text;
}

void Walk(CALayer* layer, const std::function<void(CALayer*)>& visit) {
  visit(layer);
  for (CALayer* child in layer.sublayers) Walk(child, visit);
}

void Sample(Probe& probe) {
  Class host = NSClassFromString(@"CALayerHost");
  int hosts = 0, count = 0;
  Walk(probe.root, [&](CALayer* layer) {
    ++count;
    if (host && [layer isKindOfClass:host]) ++hosts;
    id contents = layer.contents;
    if (!contents || CFGetTypeID((__bridge CFTypeRef)contents) != IOSurfaceGetTypeID()) return;
    auto surface = (__bridge IOSurfaceRef)contents;
    auto& seen = probe.layers[(__bridge void*)layer];
    const uint32_t seed = IOSurfaceGetSeed(surface);
    if (seen.last_surface == surface && seen.last_seed == seed) return;
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
  });
  probe.remote_hosts = std::max(probe.remote_hosts, hosts);
  probe.layer_count = std::max(probe.layer_count, count);
}

bool WritePng(const Probe& probe) {
  if (probe.last_pixels.empty() || probe.last_format != 'BGRA') return false;
  auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  auto data = CFDataCreate(nullptr, probe.last_pixels.data(), probe.last_pixels.size());
  auto provider = CGDataProviderCreateWithCFData(data);
  auto image = CGImageCreate(probe.last_width, probe.last_height, 8, 32, probe.last_row, space,
                             static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) |
                                 static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst), provider,
                             nullptr, false, kCGRenderingIntentDefault);
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
      @"avgCopyMs" : @(seen.copy_ms.empty() ? 0 : copy / seen.copy_ms.size()),
    }];
  }
  NSDictionary* report = @{
    @"layers" : layers,
    @"layerCount" : @(probe.layer_count),
    @"remoteLayerHosts" : @(probe.remote_hosts),
    @"png" : @(WritePng(probe)),
  };
  NSData* json = [NSJSONSerialization dataWithJSONObject:report options:0 error:nil];
  return std::string(static_cast<const char*>(json.bytes), json.length);
}
}  // namespace

void ProbeWindowFrames(void* handle, int duration_ms, const std::string& png_path,
                       std::function<void(std::string)> done) {
  id object = (__bridge id)handle;
  NSView* view = [object isKindOfClass:[NSWindow class]] ? [(NSWindow*)object contentView] : (NSView*)object;
  if (!view.layer) return done("{\"error\":\"view has no layer\"}");
  auto probe = std::make_shared<Probe>();
  probe->root = view.layer;
  probe->started = Now();
  probe->duration_ms = duration_ms;
  probe->png_path = png_path;
  probe->done = std::move(done);
  // Poll at 1 kHz on the main thread, where AppKit commits Chromium's layers.
  probe->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(probe->timer, DISPATCH_TIME_NOW, NSEC_PER_MSEC, 0);
  dispatch_source_set_event_handler(probe->timer, ^{
    Sample(*probe);
    if (Now() - probe->started < probe->duration_ms) return;
    dispatch_source_cancel(probe->timer);
    probe->timer = nil;
    probe->done(Report(*probe));
  });
  dispatch_resume(probe->timer);
}
}  // namespace suitecut
