#pragma once
#include <cmath>
namespace BotAim {
inline float EyeHeight(float value,float fallback=60.0f) {
    return std::isfinite(value)&&value>=4.0f&&value<=80.0f?value:fallback;
}
// Native8254E120 returns forward.z=-sin(pitch): negative pitch looks UP.
inline float Pitch(float dx,float dy,float dz) {
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(dz))return 0;
    return -std::atan2(dz,std::hypot(dx,dy))*57.295779513f;
}
// Short-angle commands can produce357 degrees for-3. Learn offsets using
// the signed value, rather than treating a wrap as a360-degree correction.
inline float SignedPitch(float value) {
    if(!std::isfinite(value))return 0;
    float result=std::fmod(value,360.0f);
    if(result>180)result-=360;
    if(result<-180)result+=360;
    return result;
}
}
