#include "../CoD3Host/session_player_storage.h"

#include <cassert>
#include <initializer_list>

int main()
{
    using namespace SessionPlayerStorage;
    constexpr uint32_t object = 0x66000000;
    constexpr uint32_t storage = 0x50000000;
    constexpr uint32_t first = object + ObjectOffset;
    constexpr uint32_t bytes = ActiveCount * RecordBytes;

    assert(RedirectAddress(first, object, storage) == storage);
    assert(RedirectAddress(first + 4, object, storage) == storage + 4);
    assert(RedirectAddress(first + 23 * RecordBytes + 12, object, storage) ==
           storage + 23 * RecordBytes + 12);
    assert(RedirectAddress(first + bytes - 1, object, storage) == storage + bytes - 1);
    assert(RedirectAddress(first - 1, object, storage) == first - 1);
    assert(RedirectAddress(first + bytes, object, storage) == first + bytes);
    assert(RedirectAddress(first, 0, storage) == first);
    assert(RedirectAddress(first, object, 0) == first);
    assert(RedirectAddress(0x60000000, 0xFFFFFFF0, storage) == 0x60000000);
    for (uint32_t slot : {0u, 23u, 24u, 31u}) {
        assert(RedirectRosterAddress(first + slot * RecordBytes, object, storage) == storage + slot * RecordBytes);
        assert(RedirectRosterAddress(first + slot * RecordBytes + 12, object, storage) == storage + slot * RecordBytes + 12);
    }
    assert(RedirectRosterAddress(first + StorageCount * RecordBytes - 1, object, storage) == storage + StorageCount * RecordBytes - 1);
    assert(RedirectRosterAddress(first + StorageCount * RecordBytes, object, storage) == first + StorageCount * RecordBytes);
    assert(RedirectRosterAddress(first - 1, object, storage) == first - 1);
    assert(RedirectRosterAddress(first, object, 0) == first);
    assert(RedirectRosterAddress(first, 0, storage) == first);
    assert(RedirectRosterAddress(0xFFFFFFF0, 0xFFFFD000, storage) == 0xFFFFFFF0);
    assert(RedirectRosterAddress(first, object, 0xFFFFF000) == first);
    assert(RedirectRosterAddress(first + 24 * RecordBytes, object, storage, ActiveCount) == first + 24 * RecordBytes);
    assert(RedirectRosterAddress(first + 23 * RecordBytes, object, storage, ActiveCount) == storage + 23 * RecordBytes);
    assert(RedirectRosterAddress(first, object, storage, 0) == first);
    assert(RedirectRosterAddress(first, object, storage, 33) == first);
}
