#include "../platform/Fonts.hpp"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "Exports.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace th10;
extern "C" const char* sdl_resource_path(const char*);
namespace {
struct Object {
    enum Kind{Bitmap,Context,Font} kind;u32 font=0,bitmap=0,color=0,mode=2,charset=0;
    int width=0,height=0,pitch=0,bpp=0;std::vector<u8> pixels;TTF_Font* face=nullptr;SDL_Surface* raster=nullptr;std::string text;
    ~Object(){if(raster)SDL_DestroySurface(raster);if(face)TTF_CloseFont(face);}
};
std::map<u32,std::unique_ptr<Object>> objects;u32 next=1,failures=0;
std::vector<u8> blend,codepages;bool initialized=false;
Object& get(u32 id){auto it=objects.find(id);if(it==objects.end())std::abort();return *it->second;}
u32 add(Object::Kind kind){const auto id=next++;auto value=std::make_unique<Object>();value->kind=kind;objects[id]=std::move(value);return id;}
bool load(const char* name,std::vector<u8>& output){size_t size=0;void* bytes=SDL_LoadFile(sdl_resource_path(name),&size);if(!bytes)return false;output.assign(static_cast<u8*>(bytes),static_cast<u8*>(bytes)+size);SDL_free(bytes);return true;}
void initialize(){if(initialized)return;if(!TTF_Init()||!load("/fonts/blend.bin",blend)||!load("/fonts/codepages.bin",codepages)||codepages.size()!=262144)std::abort();initialized=true;}
void utf8(std::string& out,u32 c){if(c<128)out+=char(c);else if(c<2048){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}else{out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
std::string decode(const char* bytes,u32 length,u32 charset){std::string result;const auto* data=reinterpret_cast<const u8*>(bytes);const auto* map=codepages.data()+(charset==134?131072:0);
    for(u32 i=0;i<length;i++){u32 code=data[i];const bool lead=charset==134?(code>=0x81&&code<=0xfe):((code>=0x81&&code<=0x9f)||(code>=0xe0&&code<=0xfc));if(lead&&i+1<length&&data[i+1])code=(code<<8)|data[++i];const auto c=u32(map[code*2])|(u32(map[code*2+1])<<8);utf8(result,c);}
    return result;
}
u32 blend_channel_4444(u32 before,u32 target,u32 coverage){
    const u32 source=(target*15+127)/255;
    return (before*(255-coverage)+source*coverage+127)/255;
}
}
extern "C" {
u32 fonts_bitmap(const BitmapDescription* d,u8** out){const auto* bytes=reinterpret_cast<const u8*>(d);i32 width,height;u16 bpp;std::memcpy(&width,bytes+4,4);std::memcpy(&height,bytes+8,4);std::memcpy(&bpp,bytes+14,2);height=std::abs(height);if(width<=0||height<=0||bpp!=16||uint64_t(width)*height>16777216)return 0;
    const auto id=add(Object::Bitmap);auto& b=get(id);b.width=width;b.height=height;b.bpp=bpp;b.pitch=((width*bpp+31)>>5)*4;b.pixels.resize(size_t(b.pitch)*(height+4));*out=b.pixels.data();return id;
}
u32 fonts_context(){return add(Object::Context);}
u32 fonts_select(u32 context,u32 id){auto& dc=get(context);auto it=objects.find(id);if(it==objects.end())return 0;u32& target=it->second->kind==Object::Font?dc.font:dc.bitmap;const auto old=target;target=id;return old;}
void fonts_delete_context(u32 id){objects.erase(id);}
void fonts_delete_object(u32 id){objects.erase(id);}
u32 fonts_font(i32 height,const char*,u32 charset){initialize();const auto id=add(Object::Font);auto& f=get(id);f.charset=charset;f.height=height;f.face=TTF_OpenFont(sdl_resource_path(charset==134?"fonts/simhei.ttf":"fonts/msgothic.ttc"),float(height));if(!f.face){std::fprintf(stderr,"SDL_ttf: %s\n",SDL_GetError());std::abort();}TTF_SetFontKerning(f.face,false);TTF_SetFontHinting(f.face,TTF_HINTING_NORMAL);return id;}
void fonts_background(u32 id,u32 mode){get(id).mode=mode;}
void fonts_color(u32 id,u32 color){get(id).color=color;}
void fonts_text(u32 id,i32 x,i32 y,const char* bytes,u32 length){auto& dc=get(id);auto& b=get(dc.bitmap);auto& f=get(dc.font);const auto value=decode(bytes,length,f.charset);if(value.empty())return;
    // Shadow and foreground use identical coverage. Keep only the most recent
    // run per font, so those two original draws share one TTF rasterization.
    if(!f.raster||f.text!=value){const SDL_Color white{255,255,255,255};auto* original=TTF_RenderText_Blended(f.face,value.c_str(),value.size(),white);if(!original){failures++;return;}auto* raster=SDL_ConvertSurface(original,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(original);if(!raster){failures++;return;}if(f.raster)SDL_DestroySurface(f.raster);f.raster=raster;f.text=value;}
    const auto* raster=f.raster;
    const auto r=dc.color&255,g=(dc.color>>8)&255,blue=(dc.color>>16)&255;const auto* pixels=static_cast<const u8*>(raster->pixels);
    for(int j=0;j<raster->h;j++){const int row=y+j;if(row<0||row>=b.height)continue;for(int i=0;i<raster->w;i++){const int column=x+i;if(column<0||column>=b.width)continue;const u32 coverage=pixels[j*raster->pitch+i*4+3];if(!coverage)continue;
        auto* target=b.pixels.data()+row*b.pitch+column*2;const u32 before=target[0]|(u32(target[1])<<8);
        // The original GDI target is A4R4G4B4. TextOut writes antialiased RGB
        // into the low 12 bits and clears the alpha nibble on touched pixels;
        // TextRaster::draw() flips that nibble afterwards. Keep the channels
        // separate here instead of treating the DIB as RGB555, which creates
        // the visible red/green/blue fringe around otherwise white text.
        const u16 result=u16((blend_channel_4444((before>>8)&15,r,coverage)<<8)|(blend_channel_4444((before>>4)&15,g,coverage)<<4)|blend_channel_4444(before&15,blue,coverage));target[0]=u8(result);target[1]=u8(result>>8);
    }}
}
TH_SDL_EXPORT("sdl_fonts_errors") u32 sdl_fonts_errors(){return failures;}
TH_SDL_EXPORT("sdl_fonts_shutdown") void sdl_fonts_shutdown(){objects.clear();blend.clear();codepages.clear();if(initialized)TTF_Quit();initialized=false;}
}
