#import "Preferences.hpp"
#include "NativeHost.hpp"
namespace touhou::ios {
void applyPreferences(){
 sdl_native_options(preference(@"autoFire"),preference(@"autoSlow"),preference(@"dragFire"),preference(@"autoBomb"),preference(@"hitbox"));
 sdl_display_options(int(preference(@"fps")),preference(@"smooth"));
 if(!preference(@"developer"))sdl_developer_action(0,0);
 sdl_touch_options(1,0,float(preference(@"sensitivity")));
 sdl_touch_mode(preference(@"movement")==2?3:0);
 [NSNotificationCenter.defaultCenter postNotificationName:@"TH10PreferencesChanged" object:nil];
}
// Layout editing is not implemented yet; the settings row closes cleanly.
void editControlLayout(void (^finished)(void)){if(finished)finished();}
}
