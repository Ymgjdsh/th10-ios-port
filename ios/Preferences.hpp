#pragma once
#import <Foundation/Foundation.h>
namespace touhou::ios {
inline NSDictionary* defaults(){return @{@"movement":@0,@"hidden":@NO,@"toggleFire":@NO,@"toggleSlow":@NO,@"autoFire":@NO,@"autoSlow":@NO,@"autoBomb":@NO,@"dragFire":@NO,@"opacity":@1.0,@"size":@1.0,@"sensitivity":@1.0,@"deadZone":@.16,@"leftHand":@NO,@"haptics":@NO,@"fps":@60,@"quality":@1.0,@"smooth":@NO,@"performance":@NO,@"hitbox":@NO,@"developer":@NO};}
inline NSString* preferenceKey(NSString* key){return [@"TH10." stringByAppendingString:key];}
inline double preference(NSString* key){id value=[NSUserDefaults.standardUserDefaults objectForKey:preferenceKey(key)];return [(value?:defaults()[key]) doubleValue];}
inline void setPreference(NSString* key,id value){[NSUserDefaults.standardUserDefaults setObject:value forKey:preferenceKey(key)];}
inline void resetPreferences(){for(NSString* key in NSUserDefaults.standardUserDefaults.dictionaryRepresentation)if([key hasPrefix:@"TH10."]&&![key isEqualToString:@"TH10.unlocked"])[NSUserDefaults.standardUserDefaults removeObjectForKey:key];}
void applyPreferences();
void editControlLayout(void (^finished)(void));
}
