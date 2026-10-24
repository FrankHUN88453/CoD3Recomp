#pragma once

#include <cstdint>

namespace SessionPlayerStorage
{
    inline constexpr uint32_t ObjectOffset = 0x1010;
    inline constexpr uint32_t RecordBytes = 784;
    inline constexpr uint32_t ActiveCount = 24;
    // 0.5.240: 36, four past the 32 in play: the game's six-at-a-time
    // walks over the roster (sub_823DC678, sub_823DC918) read 36 entries
    // when 32 are in play; the last four stay empty.
    inline constexpr uint32_t StorageCount = 36;
    inline constexpr uint32_t GuardBytes = 64;

    // Explicit roster accesses are supplied only by the catalogued roster
    // instructions or SessionPlayerAt. Support all reserved records here;
    // direct accesses to adjacent local-place fields never call this helper.
    constexpr uint32_t RedirectRosterAddress(uint32_t address, uint32_t object, uint32_t storage, uint32_t count = StorageCount)
    {
        if (object == 0 || storage == 0 || object > 0xFFFFFFFFu - ObjectOffset) return address;
        const uint32_t first = object + ObjectOffset;
        if (count == 0 || count > StorageCount) return address;
        const uint32_t bytes = count * RecordBytes;
        if (first > 0xFFFFFFFFu - bytes || storage > 0xFFFFFFFFu - bytes) return address;
        if (address < first || address - first >= bytes) return address;
        return storage + (address - first);
    }

    // Generic address classification remains limited to the original roster.
    // Redirect only addresses inside the original 24-entry inline roster.
    // All unrelated session-object fields keep their original addresses.
    constexpr uint32_t RedirectAddress(uint32_t address, uint32_t object, uint32_t storage)
    {
        if (object == 0 || storage == 0 || object > 0xFFFFFFFFu - ObjectOffset) return address;
        const uint32_t first = object + ObjectOffset;
        const uint32_t end = first + ActiveCount * RecordBytes;
        if (end < first || address < first || address >= end) return address;
        return storage + (address - first);
    }
}
