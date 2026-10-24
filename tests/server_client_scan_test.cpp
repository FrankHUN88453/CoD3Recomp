#include "../CoD3Host/server_client_scan.h"
#include <array>
#include <cassert>
#include <cstdint>
int main()
{
    for (uint32_t count : {24u, 32u}) {
        for (uint32_t freeSlot = 0; freeSlot <= count; ++freeSlot) {
            std::array<uint32_t, 32> states{}; states.fill(1);
            if (freeSlot < count) states[freeSlot] = 0;
            uint32_t reads = 0;
            const auto result = ServerClientScan::FindFree(count, [&](uint32_t slot) {
                assert(slot < count); ++reads; return states[slot];
            });
            assert(result == freeSlot);
            assert(reads == (freeSlot < count ? freeSlot + 1 : count));
        }
    }
    assert(ServerClientScan::FindFree(0, [](uint32_t) { assert(false); return 0; }) == 0);
}
