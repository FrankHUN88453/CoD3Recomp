#pragma once
#include <cstdint>
namespace BotListClear {
// sub_82436FC0 passes this exact temporary renderer list to820D3638.
constexpr uint32_t RendererList=0x82C89A60u;
struct Snapshot {
    bool checked=false,discarded=false;
    uint32_t buffer=0,capacity=0,count=0;
};
// A clear discards all elements. The native negative-capacity path allocates
// ZERO bytes yet copies count elements before finally clearing count. Make
// that copy empty before native allocation/free; preserve normal capacity
// and every other list. Only the verified count word may be written here.
template<class Read,class Write>
Snapshot Prepare(uint32_t list,Read read,Write write) {
    Snapshot s;
    if(list!=RendererList)return s;
    s.checked=true;s.buffer=read(list);s.capacity=read(list+4);s.count=read(list+8);
    if(int32_t(s.capacity)<0 && int32_t(s.count)>0) {
        write(list+8,0u);s.discarded=true;
    }
    return s;
}
}
