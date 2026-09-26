#pragma once
#include <algorithm>
#include <cmath>

namespace touhou::ios {
struct JoystickSample {
    float x=0,y=0;         // Game axes in [-1,1], with radial dead zone removed.
    float knobX=0,knobY=0; // Visual displacement, clamped to the travel radius.
};
inline JoystickSample joystickSample(float dx,float dy,float radius,float deadZone=.16f){
    JoystickSample sample;
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(radius)||radius<=0)return sample;
    const float distance=std::hypot(dx,dy);
    if(distance==0)return sample;
    const float magnitude=std::min(distance/radius,1.f);
    sample.knobX=dx/distance*magnitude;sample.knobY=dy/distance*magnitude;
    deadZone=std::clamp(deadZone,0.f,.95f);
    const float strength=std::max(0.f,(magnitude-deadZone)/(1-deadZone));
    sample.x=dx/distance*strength;sample.y=dy/distance*strength;
    return sample;
}
}
