#pragma once
#include <cstdint>
#include <initializer_list>
namespace BotWeaponSelection {
// Only for empty hands with no pending switch. The remembered gun can be
// from another life/class/map; request one still owned by the current kit.
template<class Owned>
inline uint32_t Recover(uint32_t remembered,uint32_t primary,uint32_t secondary,Owned owned) {
    for(uint32_t weapon : {remembered,primary,secondary})
        if(weapon>0 && weapon<128 && owned(weapon)) return weapon;
    return 0;
}
}
