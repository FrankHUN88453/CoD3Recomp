#pragma once
#include <cstdint>

namespace ServerClientScan
{
    // Return count on a full list. Unlike the retail six-wide loop, this
    // never reads a slot >= count, including for counts not divisible by six.
    template <class ReadState>
    uint32_t FindFree(uint32_t count, ReadState readState)
    {
        for (uint32_t slot = 0; slot < count; ++slot)
            if (readState(slot) == 0) return slot;
        return count;
    }
}
