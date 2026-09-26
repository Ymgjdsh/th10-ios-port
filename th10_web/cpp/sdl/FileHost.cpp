#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "Exports.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../game/Types.hpp"
using th10::u32;using th10::i32;using th10::u8;
extern "C" SDL_IOStream* th10_music_stream();
#ifdef __EMSCRIPTEN__
EM_JS(void, browser_save_changed, (), { Module['runtimeFileChanged']?.(); });
#else
static void browser_save_changed(){}
#endif
namespace {
std::string save_root="/savesth10/jp",resource_root,save_base,file_error;u32 next=1;
std::map<u32,SDL_IOStream*> handles;
std::set<u32> writers;
std::set<u32> failed_writes;
std::map<u32,std::pair<std::string,std::string>> pending_writes;
void configure_paths(){
    if(resource_root.empty()){
#ifdef __EMSCRIPTEN__
        resource_root="";
#else
        const char* base=SDL_GetBasePath();resource_root=std::string(base?base:"./")+"assets";
#endif
    }
}
std::string resource(const std::string& name){configure_paths();return resource_root+"/"+name;}
std::string normalize(const char* value){
    if(!value||!*value)return {};
    std::string out,part;auto flush=[&](){if(part=="..")return false;if(!part.empty()&&part!="."){if(!out.empty())out+='/';out+=part;}part.clear();return true;};
    for(const auto* p=value;*p;p++){char c=*p;if(c==':')return {};if(c=='/'||c=='\\'){if(!flush())return {};}else part+=c>='A'&&c<='Z'?char(c+32):c;}
    if(!flush())return {};return out;
}
void parents(const std::string& path){for(size_t i=1;i<path.size();i++)if(path[i]=='/')mkdir(path.substr(0,i).c_str(),0777);}
bool match(const char* pat,const char* s){
    if(std::strcmp(pat,"*.*")==0)pat="*";
    const char* star=nullptr;const char* retry=nullptr;
    while(*s){if(*pat=='?'||*pat==*s){++pat;++s;}else if(*pat=='*'){star=pat++;retry=s;}else if(star){pat=star+1;s=++retry;}else return false;}
    while(*pat=='*')++pat;return !*pat;
}
}
extern "C" {
void sdl_paths(const char* resources,const char* saves){resource_root=resources?resources:"";save_base=saves?saves:"";configure_paths();}
const char* sdl_resource_path(const char* relative){static thread_local std::string result;result=resource(normalize(relative));return result.c_str();}
const char* sdl_save_path(){return save_root.c_str();}
const char* sdl_file_error(){return file_error.c_str();}
TH_SDL_EXPORT("sdl_file_open") u32 browser_open(const char* raw,u32 write){const auto name=normalize(raw);if(name.empty())return ~0u;SDL_IOStream* f=nullptr;
    std::string temporary,destination;
    if(write){destination=save_root+"/"+name;parents(destination);
#ifdef __EMSCRIPTEN__
        f=SDL_IOFromFile(destination.c_str(),"wb");
#else
        // Same-directory rename commits a complete file atomically. An app
        // interruption leaves the previous score/replay intact.
        temporary=destination+".pending-"+std::to_string(next);f=SDL_IOFromFile(temporary.c_str(),"wb");
#endif
    }
    else if(name=="thbgm.dat")f=th10_music_stream();
    else {f=SDL_IOFromFile((save_root+"/"+name).c_str(),"rb");if(!f)f=SDL_IOFromFile(resource("game/"+name).c_str(),"rb");}
    if(!f){if(write)file_error=SDL_GetError();return ~0u;}const auto id=next++;handles[id]=f;if(write){writers.insert(id);if(!temporary.empty())pending_writes[id]={temporary,destination};}return id;
}
TH_SDL_EXPORT("sdl_file_close") void browser_close(u32 id){auto it=handles.find(id);if(it==handles.end())return;
    const bool closed=SDL_CloseIO(it->second);handles.erase(it);
    auto pending=pending_writes.find(id);if(pending!=pending_writes.end()){
        if(!closed||failed_writes.count(id)||std::rename(pending->second.first.c_str(),pending->second.second.c_str())!=0){file_error="Could not commit saved data";SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"%s",file_error.c_str());}
        pending_writes.erase(pending);
    }
    failed_writes.erase(id);
    if(writers.erase(id)&&closed)browser_save_changed();
}
u32 browser_size(u32 id){auto it=handles.find(id);return it==handles.end()?~0u:u32(SDL_GetIOSize(it->second));}
TH_SDL_EXPORT("sdl_file_seek") u32 browser_seek(u32 id,i32 offset,u32 origin){auto it=handles.find(id);return it==handles.end()||origin>2?~0u:u32(SDL_SeekIO(it->second,offset,static_cast<SDL_IOWhence>(origin)));}
TH_SDL_EXPORT("sdl_file_read") u32 browser_read(u32 id,u8* out,u32 size){auto it=handles.find(id);return it==handles.end()?0:SDL_ReadIO(it->second,out,size);}
u32 browser_write(u32 id,const u8* in,u32 size){auto it=handles.find(id);if(it==handles.end())return 0;const auto written=SDL_WriteIO(it->second,in,size);if(written!=size){failed_writes.insert(id);file_error=SDL_GetError();}return u32(written);}
u32 browser_list(const char* directory,const char* pattern,u32 index,char* out,u32 capacity){
    const auto dir=normalize(directory);std::vector<std::string> names;
    for(const auto& root:{resource("game"),save_root})if(auto* d=opendir((root+"/"+dir).c_str())){
        while(auto* e=readdir(d)){const std::string name=e->d_name;if(name=="."||name==".."||!match(pattern,name.c_str()))continue;
            struct stat st{};if(!stat((root+"/"+dir+"/"+name).c_str(),&st)&&S_ISREG(st.st_mode))names.push_back(name);
        }closedir(d);
    }
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    if(index>=names.size()||names[index].size()+1>capacity)return 0;std::memcpy(out,names[index].c_str(),names[index].size()+1);return 1;
}
TH_SDL_EXPORT("sdl_files_root") void sdl_files_root(u32 chinese){
#ifdef __EMSCRIPTEN__
    save_root=chinese?"/savesth10/chs":"/savesth10/jp";
#else
    if(save_base.empty()){char* path=SDL_GetPrefPath("TouhouPortable","TH10");if(path){save_base=path;SDL_free(path);}else{file_error=SDL_GetError();return;}}
    save_root=save_base+"/"+(chinese?"chs":"jp");
#endif
    parents(save_root+"/replay/");
}
TH_SDL_EXPORT("sdl_file_handles") u32 sdl_file_handles(){return handles.size();}
}
