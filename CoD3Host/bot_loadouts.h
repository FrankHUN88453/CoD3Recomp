#pragma once
#include <array>
#include <cstdint>
namespace BotLoadouts {
constexpr unsigned Count=11;
// Seven native kits plus the user's four already-loaded custom primaries.
constexpr unsigned Order[Count]={10,2,9,0,8,1,7,3,4,5,6};
constexpr uint16_t Base[Count]={0,1,2,3,4,5,6,2,4,1,0};
constexpr const char* Primary[Count]={"","","","","","","","lee_enfield","bren","sten","fg42"};
inline unsigned Select(const std::array<unsigned,Count>& team,const std::array<unsigned,Count>& total) {
    unsigned pick=Order[0];
    for(unsigned candidate:Order)
        if(team[candidate]<team[pick] || (team[candidate]==team[pick] && total[candidate]<total[pick]))pick=candidate;
    return pick;
}
template<class Sidearm,class Rounds>
uint32_t ArmedPrimary(uint32_t first,uint32_t second,Sidearm sidearm,Rounds rounds) {
    for(uint32_t weapon:{first,second})
        if(weapon>0 && weapon<128 && !sidearm(weapon) && rounds(weapon)>0)return weapon;
    return 0;
}
template<class Rounds>
uint32_t PreferPrimary(uint32_t held,uint32_t primary,Rounds rounds) {
    // Respect ownership/definition/ammo validation supplied by the caller.
    // Empty primaries still allow the existing sidearm fallback.
    return primary>0 && primary<128 && rounds(primary)>0 ? primary:held;
}
}
const char* BotsPrimaryName(uint8_t* base,uint32_t entity);
int BotsBaseClass(uint8_t* base,uint32_t entity);
// 0.5.198: a bot's kit grenades (bot_kits.txt; -1 the class's own) and his side.
bool BotsKitGrenades(uint8_t* base,uint32_t entity,int& frags,int& smokes,bool& american);
// 0.5.198: a scoped rifle, by its weapon definition's name.
bool BotsScopedWeapon(uint8_t* base,uint32_t def);
