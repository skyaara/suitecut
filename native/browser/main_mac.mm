// SPDX-License-Identifier: MIT
#import <Cocoa/Cocoa.h>
#include <cstring>
#include "include/cef_application_mac.h"
#include "include/cef_id_mappers.h"
#include "include/wrapper/cef_library_loader.h"
#include "media_pipeline.h"
#include "media_activity.h"

#include "process/main.h"

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

static void InstallChromeMenu() {
  const auto shift = NSEventModifierFlagCommand | NSEventModifierFlagShift;
  const auto option = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  NSString* name = NSProcessInfo.processInfo.processName;
  NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
  AddMenu(bar, name, @[
    CocoaItem([@"Hide " stringByAppendingString:name], @selector(hide:), @"h"),
    CocoaItem(@"Hide Others", @selector(hideOtherApplications:), @"h", option),
    CocoaItem(@"Show All", @selector(unhideAllApplications:), @""),
    NSMenuItem.separatorItem,
    ChromeItem([@"Quit " stringByAppendingString:name], "IDC_EXIT", @"q"),
  ]);
  AddMenu(bar, @"File", @[
    ChromeItem(@"New Tab", "IDC_NEW_TAB", @"t"),
    ChromeItem(@"New Window", "IDC_NEW_WINDOW", @"n"),
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
    bool chrome_windows = false;
    for (int i = 1; i < argc; ++i)
      if (std::strcmp(argv[i], "--suitecut-window-mode=chrome") == 0) chrome_windows = true;
    [NSApp setActivationPolicy:chrome_windows ? NSApplicationActivationPolicyRegular
                                              : NSApplicationActivationPolicyProhibited];
    if (chrome_windows) {
      InstallChromeMenu();
      // Launched from a script rather than the Dock, it would open behind the
      // caller's windows; come forward like a browser the person just opened.
      [NSApp activateIgnoringOtherApps:YES];
    }
    return RunSuiteCutBrowser(argc, argv);
  }
}
