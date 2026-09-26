#include "../TextFormat.hpp"
#include <cfenv>
#include <clocale>
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace th10;
static bool formatted(const char* pattern,double value,const char* expected){
    u64 bits;std::memcpy(&bits,&value,sizeof(bits));
    const TextWord arguments[]={static_cast<u32>(bits),static_cast<u32>(bits>>32)};
    char output[512];
    return format_text(output,sizeof(output),pattern,arguments,2)==static_cast<i32>(std::strlen(expected)) && std::strcmp(output,expected)==0;
}
int main(){
    const int original_rounding=std::fegetround();
    const char* original_locale=std::setlocale(LC_NUMERIC,nullptr);
    char locale_copy[128];std::snprintf(locale_copy,sizeof(locale_copy),"%s",original_locale?original_locale:"C");
    // Exercise a locale with a decimal comma when installed on the host.
    for(const char* name:{"fr_FR.UTF-8","de_DE.UTF-8","French_France.1252"})if(std::setlocale(LC_NUMERIC,name))break;
    int failure=0;
    for(const int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        if(std::fesetround(rounding))continue;
        // Final decimal rounding intentionally follows the original CRT's
        // half-up rule after its initial 17 significant decimal digits.
        if(!formatted("%.2f",1.125,"1.13"))failure=1;
        if(!formatted("%.2f",-1.125,"-1.13"))failure=2;
        if(!formatted("%.1f",9.99,"10.0"))failure=3;
        if(!formatted("%.4g",1.2345678901234567,"1.235"))failure=4;
        if(!formatted("%.16e",1.2345678901234567,"1.2345678901234567e+000"))failure=5;
        if(!formatted("%.2e",std::numeric_limits<double>::denorm_min(),"4.94e-324"))failure=6;
        if(!formatted("%.2e",std::numeric_limits<double>::max(),"1.80e+308"))failure=7;
        if(!formatted("%g",0.0,"0"))failure=8;
        if(std::fegetround()!=rounding)failure=9;
    }
    const char name[]="native pointer";const TextWord arguments[]={reinterpret_cast<TextWord>(name),static_cast<u32>(-7)};
    char output[128];
    if(format_text(output,sizeof(output),"%s %d",arguments,2)!=17 || std::strcmp(output,"native pointer -7"))failure=10;
    std::setlocale(LC_NUMERIC,locale_copy);
    if(original_rounding!=-1)std::fesetround(original_rounding);
    if(failure)std::fprintf(stderr,"TextFormat smoke failure %d\n",failure);
    return failure;
}
