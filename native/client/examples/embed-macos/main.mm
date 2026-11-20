// SPDX-License-Identifier: MIT
// suitecut-embed-sample: a small macOS app that shows one tab of a SuiteCut
// session in its own window and forwards the person's input, using only
// suitecut/embed.h. It checks the runtime, starts (or joins) its session as
// owner "sample", opens one tab, and draws the engine's IOSurface frames in a
// layer.
//
// usage: suitecut-embed-sample --runtime /path/SuiteCut.app [--signing-requirement REQ]
//          [--session NAME] [--session-dir DIR] [--profile DIR] [--url URL]
//          [--exit-after SECONDS] [--snapshot PNG] [--click X,Y] [--type TEXT] [--keep-session]
//
// --exit-after, --snapshot, --click and --type are for unattended runs: the
// click and the typing happen a second after the first frame, and the
// snapshot is the newest frame when the app quits.
#import <AppKit/AppKit.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>
#import <IOSurface/IOSurfaceRef.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "suitecut/embed.h"

namespace {
struct Options {
  std::string runtime, requirement, session = "suitecut-sample", directory, profile,
      url = "https://example.com/", snapshot, typed;
  double exit_after = 0, click_x = -1, click_y = -1;
  bool keep = false;
};
Options options;
sc_session* session = nullptr;
sc_view* view = nullptr;
int32_t page = 0;
std::atomic<bool> stopping{false};

void fail(const char* what, const sc_error& error) {
  std::fprintf(stderr, "suitecut-embed-sample: %s: %s\n", what, error.message);
}
void write_png(IOSurfaceRef surface, const std::string& path) {
  CIImage* image = [CIImage imageWithIOSurface:surface];
  CIContext* context = [CIContext contextWithOptions:nil];
  CGImageRef cg = [context createCGImage:image fromRect:image.extent];
  if (!cg) return;
  NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
  CGImageDestinationRef out = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
  if (out) {
    CGImageDestinationAddImage(out, cg, nullptr);
    CGImageDestinationFinalize(out);
    CFRelease(out);
    std::fprintf(stderr, "suitecut-embed-sample: wrote %s (%zux%zu)\n", path.c_str(), CGImageGetWidth(cg),
                 CGImageGetHeight(cg));
  }
  CGImageRelease(cg);
}
// The DOM key for keys that are not text.
const char* special_key(unsigned short code) {
  switch (code) {
    case 36: case 76: return "Enter";
    case 48: return "Tab";
    case 51: return "Backspace";
    case 117: return "Delete";
    case 53: return "Escape";
    case 123: return "ArrowLeft";
    case 124: return "ArrowRight";
    case 125: return "ArrowDown";
    case 126: return "ArrowUp";
    case 115: return "Home";
    case 119: return "End";
    case 116: return "PageUp";
    case 121: return "PageDown";
    default: return nullptr;
  }
}
}  // namespace

// The tab's frames, and the person's input to it.
@interface TabView : NSView
@end

@implementation TabView {
  CALayer* _content;
  sc_frame _shown;  // the frame on screen, held so the engine does not draw into it
  NSEventModifierFlags _held;
  NSTrackingArea* _tracking;
}
- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.wantsLayer = YES;
    self.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;
    _content = [CALayer layer];
    _content.contentsGravity = kCAGravityTopLeft;
    _content.anchorPoint = CGPointZero;
    [self.layer addSublayer:_content];
    sc_frame_init(&_shown);
  }
  return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)updateTrackingAreas {
  if (_tracking) [self removeTrackingArea:_tracking];
  _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                           options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                             owner:self
                                          userInfo:nil];
  [self addTrackingArea:_tracking];
  [super updateTrackingAreas];
}
// Called on the main thread with a frame the view now owns.
- (void)show:(sc_frame)frame {
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  const CGFloat scale = self.window.backingScaleFactor ?: 2;
  _content.contentsScale = scale;
  _content.frame = CGRectMake(0, 0, frame.width / scale, frame.height / scale);
  _content.contents = (__bridge id)static_cast<IOSurfaceRef>(frame.surface);
  [CATransaction commit];
  sc_frame_release(&_shown);
  _shown = frame;
}
- (IOSurfaceRef)shownSurface { return static_cast<IOSurfaceRef>(_shown.surface); }
- (void)setFrameSize:(NSSize)size {
  [super setFrameSize:size];
  if (!view || size.width < 1 || size.height < 1) return;
  sc_error error;
  if (sc_view_resize(view, static_cast<int>(size.width), static_cast<int>(size.height), &error) != SC_OK)
    fail("resize", error);
}
- (void)pointer:(NSEvent*)event action:(sc_pointer_action)action button:(sc_button)button {
  if (!view) return;
  const NSPoint at = [self convertPoint:event.locationInWindow fromView:nil];
  sc_pointer_event pointer;
  sc_pointer_event_init(&pointer);
  pointer.action = action;
  pointer.x = at.x;
  pointer.y = at.y;
  pointer.button = button;
  pointer.click_count = action == SC_POINTER_MOVE ? 1 : static_cast<uint32_t>(std::max<NSInteger>(1, event.clickCount));
  sc_error error;
  if (sc_view_pointer(view, &pointer, &error) != SC_OK) fail("pointer", error);
}
- (void)mouseDown:(NSEvent*)e { [self.window makeFirstResponder:self]; [self pointer:e action:SC_POINTER_DOWN button:SC_BUTTON_LEFT]; }
- (void)mouseUp:(NSEvent*)e { [self pointer:e action:SC_POINTER_UP button:SC_BUTTON_LEFT]; }
- (void)rightMouseDown:(NSEvent*)e { [self pointer:e action:SC_POINTER_DOWN button:SC_BUTTON_RIGHT]; }
- (void)rightMouseUp:(NSEvent*)e { [self pointer:e action:SC_POINTER_UP button:SC_BUTTON_RIGHT]; }
- (void)otherMouseDown:(NSEvent*)e { [self pointer:e action:SC_POINTER_DOWN button:SC_BUTTON_MIDDLE]; }
- (void)otherMouseUp:(NSEvent*)e { [self pointer:e action:SC_POINTER_UP button:SC_BUTTON_MIDDLE]; }
- (void)mouseMoved:(NSEvent*)e { [self pointer:e action:SC_POINTER_MOVE button:SC_BUTTON_LEFT]; }
- (void)mouseDragged:(NSEvent*)e { [self pointer:e action:SC_POINTER_MOVE button:SC_BUTTON_LEFT]; }
- (void)rightMouseDragged:(NSEvent*)e { [self pointer:e action:SC_POINTER_MOVE button:SC_BUTTON_RIGHT]; }
- (void)scrollWheel:(NSEvent*)event {
  if (!view) return;
  const NSPoint at = [self convertPoint:event.locationInWindow fromView:nil];
  sc_wheel_event wheel;
  sc_wheel_event_init(&wheel);
  wheel.x = at.x;
  wheel.y = at.y;
  const double step = event.hasPreciseScrollingDeltas ? 1 : 40;
  wheel.delta_x = -event.scrollingDeltaX * step;
  wheel.delta_y = -event.scrollingDeltaY * step;
  sc_error error;
  if (sc_view_wheel(view, &wheel, &error) != SC_OK) fail("wheel", error);
}
- (void)key:(const char*)name down:(int)down {
  sc_key_event key;
  sc_key_event_init(&key);
  key.key = name;
  key.down = down;
  sc_error error;
  if (sc_view_key(view, &key, &error) != SC_OK) fail("key", error);
}
- (void)keyDown:(NSEvent*)event {
  if (!view) return;
  if (const char* name = special_key(event.keyCode)) {
    [self key:name down:1];
    return;
  }
  NSString* text = event.characters;
  if (text.length && !(event.modifierFlags & NSEventModifierFlagCommand)) {
    const char* utf8 = text.UTF8String;
    sc_error error;
    if (sc_view_text(view, utf8, std::strlen(utf8), &error) != SC_OK) fail("text", error);
  }
}
- (void)keyUp:(NSEvent*)event {
  if (!view) return;
  if (const char* name = special_key(event.keyCode)) [self key:name down:0];
}
- (void)flagsChanged:(NSEvent*)event {
  if (!view) return;
  const struct { NSEventModifierFlags flag; const char* key; } modifiers[] = {
      {NSEventModifierFlagShift, "Shift"},
      {NSEventModifierFlagControl, "Control"},
      {NSEventModifierFlagOption, "Alt"},
      {NSEventModifierFlagCommand, "Meta"}};
  for (const auto& modifier : modifiers) {
    const bool now = event.modifierFlags & modifier.flag, before = _held & modifier.flag;
    if (now != before) [self key:modifier.key down:now];
  }
  _held = event.modifierFlags;
}
@end

@interface SampleApp : NSObject <NSApplicationDelegate, NSTextFieldDelegate>
@end

@implementation SampleApp {
  NSWindow* _window;
  TabView* _tab;
  NSTextField* _address;
  NSButton* _control;
  NSTextField* _status;
  bool _person;  // the tab is the person's
  bool _first;
}
- (NSButton*)button:(NSString*)title action:(SEL)action {
  NSButton* button = [NSButton buttonWithTitle:title target:self action:action];
  button.bezelStyle = NSBezelStyleRounded;
  return button;
}
- (void)applicationDidFinishLaunching:(NSNotification*)note {
  NSMenu* bar = [NSMenu new];
  NSMenuItem* item = [NSMenuItem new];
  [bar addItem:item];
  NSMenu* menu = [NSMenu new];
  [menu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
  item.submenu = menu;
  NSApp.mainMenu = bar;

  _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1024, 720)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.title = @"SuiteCut embedding sample";
  _address = [NSTextField textFieldWithString:[NSString stringWithUTF8String:options.url.c_str()]];
  _address.target = self;
  _address.action = @selector(go:);
  _control = [self button:@"Take over" action:@selector(toggleControl:)];
  _status = [NSTextField labelWithString:@"Starting…"];
  NSStackView* bar_view = [NSStackView stackViewWithViews:@[
    [self button:@"◀" action:@selector(back:)], [self button:@"▶" action:@selector(forward:)],
    [self button:@"↻" action:@selector(reload:)], _address, _control, _status
  ]];
  bar_view.edgeInsets = NSEdgeInsetsMake(6, 8, 6, 8);
  [_address setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  _tab = [[TabView alloc] initWithFrame:NSZeroRect];
  NSStackView* column = [NSStackView stackViewWithViews:@[ bar_view, _tab ]];
  column.orientation = NSUserInterfaceLayoutOrientationVertical;
  column.spacing = 0;
  column.alignment = NSLayoutAttributeLeading;
  [bar_view.widthAnchor constraintEqualToAnchor:column.widthAnchor].active = YES;
  [_tab.widthAnchor constraintEqualToAnchor:column.widthAnchor].active = YES;
  _window.contentView = column;
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  [_window layoutIfNeeded];

  if (![self start]) {
    if (session) sc_session_close(session, 1, nullptr);
    std::exit(1);
  }
  if (options.exit_after > 0)
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(options.exit_after * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
}
- (BOOL)start {
  sc_error error;
  sc_runtime_options runtime_options;
  sc_runtime_options_init(&runtime_options);
  runtime_options.path = options.runtime.c_str();
  if (!options.requirement.empty()) runtime_options.signing_requirement = options.requirement.c_str();
  static sc_runtime_info runtime;
  sc_runtime_info_init(&runtime);
  if (sc_runtime_inspect(&runtime_options, &runtime, &error) != SC_OK) {
    fail("runtime", error);
    return NO;
  }
  std::fprintf(stderr, "suitecut-embed-sample: runtime %s, protocol %u, signature %s\n", runtime.service_version,
               runtime.protocol, runtime.signature_verified ? "verified" : "not checked");
  ::mkdir(options.directory.c_str(), 0700);
  ::mkdir(options.profile.c_str(), 0700);
  sc_session_options session_options;
  sc_session_options_init(&session_options);
  session_options.runtime = &runtime;
  session_options.name = options.session.c_str();
  session_options.directory = options.directory.c_str();
  session_options.profile = options.profile.c_str();
  session_options.owner = "sample";
  session_options.client_name = "suitecut-embed-sample";
  session_options.client_version = "1";
  session_options.flags = SC_SESSION_START;
  if (sc_session_open(&session_options, &session, &error) != SC_OK) {
    fail("session", error);
    return NO;
  }
  sc_tab tab;
  sc_tab_init(&tab);
  if (sc_tab_open(session, options.url.c_str(), 0, &tab, &error) != SC_OK) {
    fail("open tab", error);
    return NO;
  }
  page = tab.page;
  sc_surface_sink sink;
  sc_surface_sink_init(&sink);
  sc_view_options view_options;
  sc_view_options_init(&view_options);
  view_options.page = page;
  view_options.sink = &sink;
  view_options.css_width = static_cast<int>(_tab.bounds.size.width);
  view_options.css_height = static_cast<int>(_tab.bounds.size.height);
  if (sc_view_attach(session, &view_options, &view, &error) != SC_OK) {
    fail("attach", error);
    return NO;
  }
  _status.stringValue = @"Watching";
  [self.class detachFrames:_tab app:self];
  [self.class detachEvents:self];
  return YES;
}
// Frames: the newest one goes to the main thread, which holds it while it is
// on screen. dispatch_sync keeps this thread one frame behind the screen.
+ (void)detachFrames:(TabView*)tab app:(SampleApp*)app {
  std::thread([tab, app] {
    while (!stopping) {
      sc_frame frame;
      sc_frame_init(&frame);
      sc_error error;
      const auto status = sc_view_next_frame(view, 200, &frame, &error);
      if (status == SC_TIMEOUT) continue;
      if (status != SC_OK) {
        fail("frame", error);
        break;
      }
      dispatch_sync(dispatch_get_main_queue(), ^{
        [tab show:frame];
        [app firstFrame];
      });
    }
  }).detach();
}
// Events: control changes and the tab closing.
+ (void)detachEvents:(SampleApp*)app {
  std::thread([app] {
    while (!stopping) {
      sc_event* event = nullptr;
      sc_error error;
      const auto status = sc_session_next_event(session, 1000, &event, &error);
      if (status == SC_TIMEOUT) continue;
      if (status != SC_OK) break;
      size_t size = 0;
      const std::string json(sc_event_json(event, &size), size);
      const auto kind = sc_event_kind_of(event);
      const auto at = sc_event_page(event);
      sc_event_destroy(event);
      dispatch_async(dispatch_get_main_queue(), ^{ [app event:kind page:at json:json]; });
    }
  }).detach();
}
- (void)firstFrame {
  if (_first) return;
  _first = true;
  if (options.click_x < 0 && options.typed.empty()) return;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    sc_error error;
    if (options.click_x >= 0) {
      sc_pointer_event pointer;
      sc_pointer_event_init(&pointer);
      pointer.x = options.click_x;
      pointer.y = options.click_y;
      for (auto action : {SC_POINTER_MOVE, SC_POINTER_DOWN, SC_POINTER_UP}) {
        pointer.action = action;
        if (sc_view_pointer(view, &pointer, &error) != SC_OK) fail("click", error);
      }
    }
    if (!options.typed.empty() && sc_view_text(view, options.typed.c_str(), options.typed.size(), &error) != SC_OK)
      fail("type", error);
  });
}
- (void)event:(sc_event_kind)kind page:(int32_t)at json:(const std::string&)json {
  if (at != page) return;
  if (kind == SC_EVENT_TAB_CLOSED) {
    _status.stringValue = @"The tab closed";
    return;
  }
  if (kind == SC_EVENT_ENGINE_EXITED) {
    _status.stringValue = @"The browser stopped";
    return;
  }
  if (kind != SC_EVENT_TAB_CHANGED) return;
  NSData* data = [NSData dataWithBytes:json.data() length:json.size()];
  NSDictionary* parsed = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  NSDictionary* tab = parsed[@"tab"];
  _person = [tab[@"control"] isEqual:@"human"];
  _control.title = _person ? @"Hand back" : @"Take over";
  _status.stringValue = [tab[@"control"] isEqual:@"agent"] ? @"An agent is driving" : _person ? @"You have it" : @"Watching";
  if ([tab[@"url"] isKindOfClass:NSString.class] && _window.firstResponder != _address.currentEditor)
    _address.stringValue = tab[@"url"];
}
- (void)go:(id)sender {
  sc_error error;
  sc_tab tab;
  sc_tab_init(&tab);
  if (sc_tab_navigate(session, page, _address.stringValue.UTF8String, &tab, &error) != SC_OK) fail("navigate", error);
  [_window makeFirstResponder:_tab];
}
- (void)history:(sc_history)step {
  sc_error error;
  sc_tab tab;
  sc_tab_init(&tab);
  if (sc_tab_history(session, page, step, &tab, &error) != SC_OK) fail("history", error);
}
- (void)back:(id)sender { [self history:SC_HISTORY_BACK]; }
- (void)forward:(id)sender { [self history:SC_HISTORY_FORWARD]; }
- (void)reload:(id)sender { [self history:SC_HISTORY_RELOAD]; }
- (void)toggleControl:(id)sender {
  sc_error error;
  sc_tab tab;
  sc_tab_init(&tab);
  const auto status = _person ? sc_tab_release(session, page, &tab, &error) : sc_tab_take(session, page, &tab, &error);
  if (status != SC_OK) fail("control", error);
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { return YES; }
- (void)applicationWillTerminate:(NSNotification*)note {
  stopping = true;
  if (!options.snapshot.empty() && _tab.shownSurface) write_png(_tab.shownSurface, options.snapshot);
  sc_error error;
  if (view) sc_view_detach(view, &error);
  if (session) {
    if (!options.keep && sc_session_close(session, 1, &error) != SC_OK) fail("close", error);
    if (options.keep) sc_session_close(session, 0, &error);
  }
}
@end

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", argument.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (argument == "--runtime") options.runtime = value();
    else if (argument == "--signing-requirement") options.requirement = value();
    else if (argument == "--session") options.session = value();
    else if (argument == "--session-dir") options.directory = value();
    else if (argument == "--profile") options.profile = value();
    else if (argument == "--url") options.url = value();
    else if (argument == "--exit-after") options.exit_after = std::stod(value());
    else if (argument == "--snapshot") options.snapshot = value();
    else if (argument == "--type") options.typed = value();
    else if (argument == "--keep-session") options.keep = true;
    else if (argument == "--click") {
      const auto at = value();
      if (std::sscanf(at.c_str(), "%lf,%lf", &options.click_x, &options.click_y) != 2) {
        std::fprintf(stderr, "--click takes X,Y\n");
        return 2;
      }
    } else {
      std::fprintf(stderr, "Unknown argument %s\n", argument.c_str());
      return 2;
    }
  }
  if (options.runtime.empty()) {
    std::fprintf(stderr, "usage: suitecut-embed-sample --runtime /path/SuiteCut.app [options]\n");
    return 2;
  }
  if (options.directory.empty()) options.directory = "/tmp/suitecut-sample-" + std::to_string(::getuid());
  if (options.profile.empty())
    options.profile = std::string(NSHomeDirectory().UTF8String) + "/Library/Application Support/SuiteCut Sample";
  @autoreleasepool {
    NSApplication* app = NSApplication.sharedApplication;
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    SampleApp* delegate = [SampleApp new];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
