// SPDX-License-Identifier: MIT
#import <Cocoa/Cocoa.h>
#include <cstring>
#include <utility>
#include <vector>
#include "include/cef_application_mac.h"
#include "include/cef_id_mappers.h"
#include "include/wrapper/cef_library_loader.h"
#include "media/media_pipeline.h"
#include "media/media_activity.h"

#include "app/context_menus.h"
#include "process/main.h"
#include "process/prototype_tabs.h"
#include "process/standalone.h"

namespace standalone = suitecut::browser::standalone;
static bool g_standalone = false;

@interface SuiteCutApplication : NSApplication <CefAppProtocol> {
  BOOL handling_;
}
@end
@implementation SuiteCutApplication
- (BOOL)isHandlingSendEvent { return handling_; }
- (void)setHandlingSendEvent:(BOOL)value { handling_ = value; }
- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sending;
  [super sendEvent:event];
}
// Cmd+Q, the Dock's Quit and logging out all arrive here. Opened from Finder
// the windows close first, so pages can ask to keep unsaved work.
- (void)terminate:(id)sender {
  if (g_standalone) standalone::Quit();
  else [super terminate:sender];
}
@end

static NSString* String(const std::string& value) {
  return [NSString stringWithUTF8String:value.c_str()] ?: @"";
}

// Opened from Finder: the Dock icon, Chrome's Profiles menu and the profile a
// window belongs to. SDK launches never create this.
@interface SuiteCutStandalone : NSObject <NSApplicationDelegate, NSMenuDelegate>
@end
@implementation SuiteCutStandalone
- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)visible {
  standalone::Reopen();
  return NO;
}
// The profile of the key window, else the one used last.
- (std::string)currentProfile {
  NSWindow* key = NSApp.keyWindow ?: NSApp.mainWindow;
  for (const auto& [view, dir] : standalone::Windows())
    if (view && [(NSView*)view window] == key) return dir;
  return "";
}
- (NSWindow*)windowFor:(const std::string&)dir {
  for (const auto& [view, open] : standalone::Windows())
    if (view && open == dir && [(NSView*)view window]) return [(NSView*)view window];
  return nil;
}
- (void)menuNeedsUpdate:(NSMenu*)menu {
  [menu removeAllItems];
  const auto current = [self currentProfile];
  for (const auto& profile : standalone::Profiles()) {
    NSMenuItem* item = [[[NSMenuItem alloc] initWithTitle:String(profile.name)
                                                   action:@selector(openProfile:)
                                            keyEquivalent:@""] autorelease];
    item.target = self;
    item.representedObject = String(profile.dir);
    item.state = profile.dir == current ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:item];
  }
  [menu addItem:NSMenuItem.separatorItem];
  NSMenuItem* add = [[[NSMenuItem alloc] initWithTitle:@"Add Profile…"
                                                action:@selector(addProfile:)
                                         keyEquivalent:@""] autorelease];
  add.target = self;
  [menu addItem:add];
  NSMenuItem* rename = [[[NSMenuItem alloc] initWithTitle:@"Rename This Profile…"
                                                   action:@selector(renameProfile:)
                                            keyEquivalent:@""] autorelease];
  rename.target = self;
  [menu addItem:rename];
}
// As in Chrome, choosing a profile that already has a window brings it forward.
- (void)openProfile:(NSMenuItem*)item {
  const std::string dir = [(NSString*)item.representedObject UTF8String];
  if (NSWindow* window = [self windowFor:dir]) {
    [window makeKeyAndOrderFront:nil];
    return;
  }
  standalone::Open(dir);
}
- (void)newWindow:(id)sender {
  standalone::Open([self currentProfile]);
}
- (NSString*)askName:(NSString*)title value:(NSString*)value button:(NSString*)button {
  NSAlert* alert = [[[NSAlert alloc] init] autorelease];
  alert.messageText = title;
  [alert addButtonWithTitle:button];
  [alert addButtonWithTitle:@"Cancel"];
  NSTextField* field = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)] autorelease];
  field.stringValue = value;
  field.placeholderString = @"Name";
  alert.accessoryView = field;
  alert.window.initialFirstResponder = field;
  if ([alert runModal] != NSAlertFirstButtonReturn) return nil;
  return field.stringValue;
}
- (void)addProfile:(id)sender {
  if (NSString* name = [self askName:@"Add a profile" value:@"" button:@"Add"])
    standalone::Add(name.UTF8String);
}
- (void)renameProfile:(id)sender {
  auto dir = [self currentProfile];
  NSString* name = @"";
  for (const auto& profile : standalone::Profiles())
    if (profile.dir == dir) name = String(profile.name);
  if (dir.empty()) return;
  if (NSString* renamed = [self askName:@"Rename this profile" value:name button:@"Rename"])
    standalone::Rename(dir, renamed.UTF8String);
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
  if (item.action == @selector(renameProfile:)) return ![self currentProfile].empty();
  return YES;
}
@end

// Chrome's own macOS menus send `commandDispatch:` tagged with a command id to
// the key browser window. Chrome window mode builds the same bar, so the usual
// shortcuts (new tab, close tab, find, zoom, copy and paste) work.
static NSMenuItem* ChromeItem(NSString* title, const char* command, NSString* key,
                              NSEventModifierFlags modifiers = NSEventModifierFlagCommand) {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                action:@selector(commandDispatch:)
                                         keyEquivalent:key];
  item.tag = cef_id_for_command_id_name(command);
  item.keyEquivalentModifierMask = modifiers;
  return item;
}

static NSMenuItem* CocoaItem(NSString* title, SEL action, NSString* key,
                             NSEventModifierFlags modifiers = NSEventModifierFlagCommand) {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
  item.keyEquivalentModifierMask = modifiers;
  return item;
}

static NSMenu* AddMenu(NSMenu* bar, NSString* title, NSArray<NSMenuItem*>* items) {
  NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
  for (NSMenuItem* item in items) [menu addItem:item];
  NSMenuItem* holder = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
  holder.submenu = menu;
  [bar addItem:holder];
  return menu;
}

static void InstallChromeMenu(SuiteCutStandalone* own) {
  const auto shift = NSEventModifierFlagCommand | NSEventModifierFlagShift;
  const auto option = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  NSString* name = @"SuiteCut";
  NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
  AddMenu(bar, name, @[
    CocoaItem([@"Hide " stringByAppendingString:name], @selector(hide:), @"h"),
    CocoaItem(@"Hide Others", @selector(hideOtherApplications:), @"h", option),
    CocoaItem(@"Show All", @selector(unhideAllApplications:), @""),
    NSMenuItem.separatorItem,
    own ? CocoaItem([@"Quit " stringByAppendingString:name], @selector(terminate:), @"q")
        : ChromeItem([@"Quit " stringByAppendingString:name], "IDC_EXIT", @"q"),
  ]);
  // Chrome's New Window needs a key browser window; ours also works with none
  // open and keeps the window's profile.
  NSMenuItem* new_window = ChromeItem(@"New Window", "IDC_NEW_WINDOW", @"n");
  if (own) {
    new_window.action = @selector(newWindow:);
    new_window.target = own;
  }
  AddMenu(bar, @"File", @[
    ChromeItem(@"New Tab", "IDC_NEW_TAB", @"t"),
    new_window,
    ChromeItem(@"Reopen Closed Tab", "IDC_RESTORE_TAB", @"t", shift),
    ChromeItem(@"Open Location…", "IDC_FOCUS_LOCATION", @"l"),
    NSMenuItem.separatorItem,
    ChromeItem(@"Close Window", "IDC_CLOSE_WINDOW", @"w", shift),
    ChromeItem(@"Close Tab", "IDC_CLOSE_TAB", @"w"),
    NSMenuItem.separatorItem,
    ChromeItem(@"Print…", "IDC_PRINT", @"p"),
  ]);
  AddMenu(bar, @"Edit", @[
    CocoaItem(@"Undo", @selector(undo:), @"z"),
    CocoaItem(@"Redo", @selector(redo:), @"z", shift),
    NSMenuItem.separatorItem,
    CocoaItem(@"Cut", @selector(cut:), @"x"),
    CocoaItem(@"Copy", @selector(copy:), @"c"),
    CocoaItem(@"Paste", @selector(paste:), @"v"),
    CocoaItem(@"Paste and Match Style", @selector(pasteAndMatchStyle:), @"v",
              shift | NSEventModifierFlagOption),
    CocoaItem(@"Select All", @selector(selectAll:), @"a"),
    NSMenuItem.separatorItem,
    ChromeItem(@"Find…", "IDC_FIND", @"f"),
  ]);
  AddMenu(bar, @"View", @[
    ChromeItem(@"Reload This Page", "IDC_RELOAD", @"r"),
    NSMenuItem.separatorItem,
    ChromeItem(@"Actual Size", "IDC_ZOOM_NORMAL", @"0"),
    ChromeItem(@"Zoom In", "IDC_ZOOM_PLUS", @"="),
    ChromeItem(@"Zoom Out", "IDC_ZOOM_MINUS", @"-"),
    NSMenuItem.separatorItem,
    CocoaItem(@"Enter Full Screen", @selector(toggleFullScreen:), @"f",
              NSEventModifierFlagCommand | NSEventModifierFlagControl),
  ]);
  AddMenu(bar, @"History", @[
    ChromeItem(@"Back", "IDC_BACK", @"["),
    ChromeItem(@"Forward", "IDC_FORWARD", @"]"),
    NSMenuItem.separatorItem,
    ChromeItem(@"Show Full History", "IDC_SHOW_HISTORY", @"y"),
  ]);
  AddMenu(bar, @"Bookmarks", @[
    ChromeItem(@"Bookmark This Tab…", "IDC_BOOKMARK_THIS_TAB", @"d"),
    ChromeItem(@"Bookmark Manager", "IDC_SHOW_BOOKMARK_MANAGER", @"b", option),
  ]);
  if (own) AddMenu(bar, @"Profiles", @[]).delegate = own;
  NSMenu* window = AddMenu(bar, @"Window", @[
    CocoaItem(@"Minimize", @selector(performMiniaturize:), @"m"),
    CocoaItem(@"Zoom", @selector(performZoom:), @""),
    NSMenuItem.separatorItem,
    ChromeItem(@"Select Next Tab", "IDC_SELECT_NEXT_TAB", @"}", shift),
    ChromeItem(@"Select Previous Tab", "IDC_SELECT_PREVIOUS_TAB", @"{", shift),
    ChromeItem(@"Downloads", "IDC_SHOW_DOWNLOADS", @"l", option),
    NSMenuItem.separatorItem,
    CocoaItem(@"Bring All to Front", @selector(arrangeInFront:), @""),
  ]);
  NSApp.mainMenu = bar;
  NSApp.windowsMenu = window;
}

// Remembers the menus being tracked so closing a page can cancel its context
// menu first and wait for it to finish (see context_menus.h).
static void WatchMenus() {
  static NSMutableSet* tracking = [[NSMutableSet alloc] init];
  static auto* ended = new std::vector<std::function<void()>>;
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  [center addObserverForName:NSMenuDidBeginTrackingNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification* note) {
                    [tracking addObject:note.object];
                  }];
  [center addObserverForName:NSMenuDidEndTrackingNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification* note) {
                    [tracking removeObject:note.object];
                    if (tracking.count) return;
                    const auto waiting = std::exchange(*ended, {});
                    for (const auto& callback : waiting) callback();
                  }];
  suitecut::browser::ContextMenus::Get().cancel = [](std::function<void()> callback) {
    if (!tracking.count) return callback();
    ended->push_back(std::move(callback));
    for (NSMenu* menu in [tracking allObjects]) [menu cancelTrackingWithoutAnimation];
  };
}

int main(int argc, char** argv) {
  // The supervised encoder is a native process helper, not a browser. Dispatch
  // it before mapping CEF or creating NSApplication so the first paint does not
  // wait behind browser startup and framework/sandbox work a second time.
  const int encoder = suitecut::MediaEncoderHelperMain(argc, argv);
  if (encoder >= 0) return encoder;
  suitecut::RegisterMediaMainThread();
  @autoreleasepool {
    CefScopedLibraryLoader loader;
    if (!loader.LoadInMain()) return 1;
    [SuiteCutApplication sharedApplication];
    // Offscreen automation never shows UI. Chrome window mode is a browser a
    // person uses, so it needs a Dock icon, menu bar and keyboard focus.
    // Opened from Finder, the Dock or Spotlight there are no SDK arguments.
    const bool prototype = suitecut::browser::prototype::Requested(argc, argv);
    g_standalone = !prototype && standalone::Requested(argc, argv);
    bool chrome_windows = g_standalone || prototype;
    for (int i = 1; i < argc; ++i)
      if (std::strcmp(argv[i], "--suitecut-window-mode=chrome") == 0) chrome_windows = true;
    SuiteCutStandalone* own = g_standalone ? [[SuiteCutStandalone alloc] init] : nil;
    if (own) NSApp.delegate = own;
    [NSApp setActivationPolicy:chrome_windows ? NSApplicationActivationPolicyRegular
                                              : NSApplicationActivationPolicyProhibited];
    if (chrome_windows) {
      InstallChromeMenu(own);
      WatchMenus();
      // Launched from a script rather than the Dock, it would open behind the
      // caller's windows; come forward like a browser the person just opened.
      [NSApp activateIgnoringOtherApps:YES];
    }
    return RunSuiteCutBrowser(argc, argv);
  }
}
