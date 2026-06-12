#pragma once
// Menu bar controller: owns the status item, the dropdown menu, and the libmdr
// pump. Plain ObjC interface; all C++ state lives in the implementation.
#import <Cocoa/Cocoa.h>

@interface MenuController : NSObject <NSMenuDelegate>
- (instancetype)init;
- (void)shutdown; // invalidate the pump timer + disconnect
@end
