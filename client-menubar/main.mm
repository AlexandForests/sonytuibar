// Sony Headphones Bar — macOS menu bar client on libmdr.
//
// Threading model matches the TUI: everything (UI + libmdr + connection) runs
// on the main thread. IOBluetooth delivers RFCOMM callbacks on the main run
// loop, which AppKit runs natively here; an NSTimer (in common modes, so it
// keeps firing while the menu is open) drives libmdr.
#import <Cocoa/Cocoa.h>
#import "MenuController.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate
{
    MenuController* _controller;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    _controller = [[MenuController alloc] init];
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    [_controller shutdown];
}
@end

int main()
{
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
