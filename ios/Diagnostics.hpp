#pragma once
#import <UIKit/UIKit.h>
#include <cstdint>

namespace touhou::ios {
void diagnosticsStart();
void diagnostic(NSString* format, ...) NS_FORMAT_FUNCTION(1, 2);
void diagnosticsFlush();
void diagnosticsState(NSString* reason);
void diagnosticsFrame(double timestamp, double workSeconds, std::uint64_t callbacks);
void presentSettings(UIViewController* presenter, UIView* anchor, void (^finished)(void));
}
