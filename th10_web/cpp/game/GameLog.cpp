#include "GameLog.hpp"
namespace th10 {
const char* GameLog::append(const char* format,const u32* arguments,bool error,GameLogEnvironment& env){
    env.enter();++*env.lock_depth;
    char formatted[8192];env.format(formatted,error?512:8192,format,arguments);const u32 length=std::strlen(formatted);
    const auto used=cursor-text;
    if(used>=0&&static_cast<u64>(used)+length<8191){char* output=cursor;const char* input=formatted;char value;do{value=*input++;*output++=value;}while(value);cursor+=length;*cursor=0;}
    if(error)has_error=1;env.leave();--*env.lock_depth;return format;
}
}
