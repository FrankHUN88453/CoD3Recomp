#pragma once
#include <cstdint>
namespace TacticalPointInput {
struct Hold { uint32_t began=0; bool timing=false, fired=false; };
inline bool Save(Hold& hold,bool eligible,bool down,uint32_t now,uint32_t holdMs=1000) {
    if(!eligible||!down) {hold={};return false;}
    if(!hold.timing) {hold.began=now;hold.timing=true;}
    if(!hold.fired && uint32_t(now-hold.began)>=holdMs) {hold.fired=true;return true;}
    return false;
}
}
