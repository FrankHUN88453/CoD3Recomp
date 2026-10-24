#pragma once
#include <cstdint>
namespace BotNetworkClock {
// Native82403840: object[(8884/8888/8892 + currentClient)*4].
constexpr uint32_t Offset(unsigned kind,uint32_t client) {
    return (8884u+kind*4u+client)*4u;
}
struct State {
    uint32_t session=0,player=0,sent[3]={};
    void Bind(uint32_t nextSession,uint32_t nextPlayer) {
        if(session==nextSession && player==nextPlayer)return;
        session=nextSession;player=nextPlayer;
        for(auto& clock:sent)clock=0;
    }
};
}
