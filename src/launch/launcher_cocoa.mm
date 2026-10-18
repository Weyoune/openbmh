// SPDX-License-Identifier: GPL-3.0-only
#import <AppKit/AppKit.h>
#include "launch/launcher_ui.hpp"
#include "launch/archive.hpp"
#include <functional>

@interface OBMLauncherBridge : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate> {
  @public
    std::function<void(NSInteger)> action;
    std::function<void()> selection;
}
@property(strong) NSArray<NSDictionary *> *rows;
- (void)activate:(id)sender;
@end
@implementation OBMLauncherBridge
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table {
    (void)table;
    return self.rows.count;
}
- (id)tableView:(NSTableView *)table objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    (void)table;
    return self.rows[row][column.identifier];
}
- (void)tableViewSelectionDidChange:(NSNotification *)note {
    (void)note;
    if (selection)
        selection();
}
- (void)activate:(id)sender {
    if (action)
        action([sender tag]);
}
- (BOOL)windowShouldClose:(NSWindow *)window {
    (void)window;
    if (action)
        action(0);
    return NO;
}
@end

namespace bmh::launch {
namespace {
NSString *string(const std::string &s) {
    auto value = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    if (!value)
        throw std::runtime_error("Invalid UTF-8 launcher text");
    return value;
}
std::string utf8(NSString *s) { return std::string(s.UTF8String ?: ""); }
NSTextField *label(NSString *text, NSRect frame) {
    auto *field = [NSTextField labelWithString:text];
    field.frame = frame;
    return field;
}
NSTextField *input(NSString *text, NSRect frame) {
    auto *field = [[NSTextField alloc] initWithFrame:frame];
    field.stringValue = text;
    return field;
}
class CocoaLauncher final : public LauncherUI {
    NSWindow *window_;
    NSTableView *table_;
    OBMLauncherBridge *bridge_;
    NSMutableArray<NSButton *> *buttons_;
    NSWindow *progress_ = nil;
    NSTextField *progress_label_ = nil;
    OBMLauncherBridge *progress_bridge_ = nil;
    bool cancelled_ = false;

  public:
    CocoaLauncher() {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        window_ = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 660, 340)
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskMiniaturizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        window_.title = @"OpenBMH";
        [window_ center];
        window_.releasedWhenClosed = NO;
        bridge_ = [OBMLauncherBridge new];
        bridge_.rows = @[];
        window_.delegate = bridge_;
        auto *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(16, 16, 490, 308)];
        scroll.hasVerticalScroller = YES;
        scroll.borderType = NSBezelBorder;
        table_ = [[NSTableView alloc] initWithFrame:scroll.bounds];
        table_.allowsMultipleSelection = NO;
        table_.dataSource = bridge_;
        table_.delegate = bridge_;
        for (NSString *identifier in @[ @"Name", @"Art" ]) {
            auto *column = [[NSTableColumn alloc] initWithIdentifier:identifier];
            column.title = identifier;
            column.width = [identifier isEqualToString:@"Name"] ? 350 : 110;
            [table_ addTableColumn:column];
        }
        table_.target = bridge_;
        table_.doubleAction = @selector(activate:);
        table_.tag = 1;
        scroll.documentView = table_;
        [window_.contentView addSubview:scroll];
        buttons_ = [NSMutableArray new];
        NSArray *titles = @[ @"Start", @"Add", @"Delete", @"Rename", @"End" ];
        for (NSInteger i = 0; i < 5; ++i) {
            auto *button = [NSButton buttonWithTitle:titles[i] target:bridge_ action:@selector(activate:)];
            button.tag = i + 1;
            button.frame = NSMakeRect(522, 282 - i * 50, 120, 34);
            [window_.contentView addSubview:button];
            [buttons_ addObject:button];
        }
        bridge_->action = [this](NSInteger code) {
            if ((code == 1 || code == 3 || code == 4) && table_.selectedRow < 0)
                return;
            [NSApp stopModalWithCode:code];
        };
        bridge_->selection = [this] {
            for (NSInteger i : {0, 2, 3})
                buttons_[i].enabled = table_.selectedRow >= 0;
        };
        bridge_->selection();
        [window_ makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
    ~CocoaLauncher() override {
        finish_progress();
        bridge_->action = {};
        bridge_->selection = {};
        window_.delegate = nil;
        [window_ orderOut:nil];
        [window_ close];
    }
    LauncherChoice next(const std::vector<ModEntry> &mods) override {
        const NSInteger old = table_.selectedRow;
        auto *rows = [NSMutableArray new];
        for (const auto &mod : mods)
            [rows addObject:@{
                @"Name" : string(mod.name + (mod.error.empty() ? "" : " ⚠")),
                @"Art" : string(mod.type)
            }];
        bridge_.rows = rows;
        [table_ reloadData];
        if (old >= 0 && size_t(old) < mods.size())
            [table_ selectRowIndexes:[NSIndexSet indexSetWithIndex:old] byExtendingSelection:NO];
        bridge_->selection();
        [window_ makeKeyAndOrderFront:nil];
        const NSInteger response = [NSApp runModalForWindow:window_];
        return {response == 1   ? LauncherAction::start
                : response == 2 ? LauncherAction::add
                : response == 3 ? LauncherAction::remove
                : response == 4 ? LauncherAction::rename
                                : LauncherAction::end,
                int(table_.selectedRow)};
    }
    std::optional<std::filesystem::path> choose_folder(const std::string &title,
                                                       const std::filesystem::path &initial) override {
        auto *panel = [NSOpenPanel openPanel];
        panel.title = string(title);
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        if (!initial.empty())
            panel.directoryURL = [NSURL fileURLWithPath:string(path_to_utf8(initial)) isDirectory:YES];
        if ([panel runModal] != NSModalResponseOK)
            return {};
        return path_from_utf8(utf8(panel.URL.path));
    }
    std::optional<AddMod> configure_add(AddMod initial) override {
        auto *alert = [NSAlert new];
        alert.messageText = @"Classic-Mod hinzufügen";
        [alert addButtonWithTitle:@"Importieren"];
        [alert addButtonWithTitle:@"Abbrechen"];
        auto *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 540, 180)];
        [view addSubview:label(@"Bezeichnung", NSMakeRect(0, 146, 160, 24))];
        auto *name = input(string(initial.name), NSMakeRect(170, 146, 370, 24));
        [view addSubview:name];
        [view addSubview:label(@"Startjahr (nur Anzeige)", NSMakeRect(0, 106, 165, 24))];
        auto *year = input(string(std::to_string(initial.start_year)), NSMakeRect(170, 106, 110, 24));
        [view addSubview:year];
        auto *migrate = [NSButton checkboxWithTitle:@"Spielstände/Einstellungen übernehmen"
                                             target:nil
                                             action:nil];
        migrate.frame = NSMakeRect(0, 68, 540, 24);
        migrate.state = initial.migrate ? NSControlStateValueOn : NSControlStateValueOff;
        [view addSubview:migrate];
        [view addSubview:label(@"Spielstandsordner", NSMakeRect(0, 25, 160, 24))];
        auto *saves = input(string(path_to_utf8(initial.saves)), NSMakeRect(170, 25, 245, 24));
        [view addSubview:saves];
        auto *browse_bridge = [OBMLauncherBridge new];
        browse_bridge->action = [this, saves](NSInteger) {
            if (auto p = choose_folder("Spielstandsordner wählen", path_from_utf8(utf8(saves.stringValue))))
                saves.stringValue = string(path_to_utf8(*p));
        };
        auto *browse = [NSButton buttonWithTitle:@"Auswählen …"
                                          target:browse_bridge
                                          action:@selector(activate:)];
        browse.frame = NSMakeRect(420, 22, 120, 30);
        [view addSubview:browse];
        alert.accessoryView = view;
        while ([alert runModal] == NSAlertFirstButtonReturn)
            try {
                initial.name = utf8(name.stringValue);
                ModLibrary::validate_name(initial.name);
                const auto value = utf8(year.stringValue);
                size_t end = 0;
                initial.start_year = std::stoi(value, &end);
                if (end != value.size() || initial.start_year < 1900 || initial.start_year > 9998)
                    throw std::runtime_error("Startjahr muss zwischen 1900 und 9998 liegen");
                initial.saves = path_from_utf8(utf8(saves.stringValue));
                initial.migrate = migrate.state == NSControlStateValueOn;
                return initial;
            } catch (const std::exception &e) {
                message(e.what(), true);
            }
        return {};
    }
    std::optional<std::string> rename(const std::string &old) override {
        auto *alert = [NSAlert new];
        alert.messageText = @"Mod umbenennen";
        [alert addButtonWithTitle:@"Umbenennen"];
        [alert addButtonWithTitle:@"Abbrechen"];
        auto *field = input(string(old), NSMakeRect(0, 0, 380, 26));
        alert.accessoryView = field;
        if ([alert runModal] != NSAlertFirstButtonReturn)
            return {};
        return utf8(field.stringValue);
    }
    bool confirm_delete(const std::string &name) override {
        auto *alert = [NSAlert new];
        alert.messageText = string("Mod „" + name + "“ löschen?");
        alert.informativeText = @"Spielstände und persönliche Einstellungen bleiben erhalten.";
        [alert addButtonWithTitle:@"Abbrechen"];
        [alert addButtonWithTitle:@"Löschen"];
        return [alert runModal] == NSAlertSecondButtonReturn;
    }
    void message(const std::string &value, bool error) override {
        auto *alert = [NSAlert new];
        alert.messageText = @"OpenBMH";
        alert.informativeText = string(value);
        alert.alertStyle = error ? NSAlertStyleWarning : NSAlertStyleInformational;
        [alert runModal];
    }
    void progress(const std::string &value, bool cancellable) override {
        if (!progress_) {
            cancelled_ = false;
            for (NSButton *b in buttons_)
                b.enabled = NO;
            progress_ = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 580, 125)
                                                    styleMask:NSWindowStyleMaskTitled
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
            progress_.title = @"OpenBMH";
            progress_.releasedWhenClosed = NO;
            progress_label_ = label(string(value), NSMakeRect(16, 65, 548, 40));
            [progress_.contentView addSubview:progress_label_];
            progress_bridge_ = [OBMLauncherBridge new];
            progress_bridge_->action = [this](NSInteger) { cancelled_ = true; };
            progress_.delegate = progress_bridge_;
            if (cancellable) {
                auto *button = [NSButton buttonWithTitle:@"Abbrechen"
                                                  target:progress_bridge_
                                                  action:@selector(activate:)];
                button.frame = NSMakeRect(440, 15, 120, 30);
                [progress_.contentView addSubview:button];
            }
            [window_ beginSheet:progress_ completionHandler:nil];
        }
        progress_label_.stringValue = string(value);
    }
    bool pump() override {
        @autoreleasepool {
            NSEvent *event;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate distantPast]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES]))
                [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
        return cancelled_;
    }
    void finish_progress() override {
        if (progress_) {
            progress_bridge_->action = {};
            [window_ endSheet:progress_];
            [progress_ orderOut:nil];
            [progress_ close];
            progress_ = nil;
            progress_label_ = nil;
            progress_bridge_ = nil;
            for (NSButton *b in buttons_)
                b.enabled = YES;
            bridge_->selection();
        }
    }
};
} // namespace
std::unique_ptr<LauncherUI> make_launcher_ui() { return std::make_unique<CocoaLauncher>(); }
} // namespace bmh::launch
