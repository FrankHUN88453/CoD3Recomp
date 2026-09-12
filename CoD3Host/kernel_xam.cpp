// Xbox 360 kernel imports: the Xam layer, which on the console is the
// dashboard. Profiles, controllers, notifications, storage devices, and the
// system dialogs all come through here.
//
// None of it is backed by anything real. A single local player is reported so
// the title has a profile to work with, no controller is connected, no
// notification ever arrives, and every system dialog answers as if the user
// cancelled it immediately. Those answers are chosen to be the ones a title
// handles as a normal case rather than an error.

#include "kernel.h"
#include "sampler.h"
#include <atomic>
#include "input.h"

#include <cstdio>
#include <cstring>

#include <Windows.h>

namespace
{
    constexpr uint32_t X_ERROR_SUCCESS            = 0x00000000;
    constexpr uint32_t X_ERROR_NO_SUCH_USER       = 0x00000525;
    constexpr uint32_t X_ERROR_FUNCTION_FAILED    = 0x0000045B;
    constexpr uint32_t X_ERROR_DEVICE_NOT_CONNECTED = 0x0000048F;
    constexpr uint32_t X_ERROR_NOT_FOUND          = 0x00000490;
    constexpr uint32_t X_ERROR_IO_PENDING         = 0x000003E5;

    constexpr uint32_t SignedInUser = 0;
    constexpr uint64_t PlayerXuid = 0xE000000000000001ull;
    constexpr char PlayerName[] = "Player";
}

// --- The signed in user ----------------------------------------------------

// ULONG XamUserGetSigninState(ULONG userIndex)
PPC_FUNC(__imp__XamUserGetSigninState)
{
    Kernel::CountImport("XamUserGetSigninState");
    // 0 not signed in, 1 signed in locally, 2 signed in to Live.
    ctx.r3.u32 = (ctx.r3.u32 == SignedInUser) ? 1 : 0;
}

// X_RESULT XamUserGetName(ULONG userIndex, char* buffer, ULONG length)
PPC_FUNC(__imp__XamUserGetName)
{
    Kernel::CountImport("XamUserGetName");
    const uint32_t userIndex = ctx.r3.u32;
    const uint32_t buffer = ctx.r4.u32;
    const uint32_t length = ctx.r5.u32;

    if (userIndex != SignedInUser || buffer == 0 || length == 0)
    {
        ctx.r3.u32 = X_ERROR_NO_SUCH_USER;
        return;
    }

    const uint32_t copy = length < sizeof(PlayerName) ? length : uint32_t(sizeof(PlayerName));
    memcpy(Guest::Ptr(buffer), PlayerName, copy);
    *(Guest::Ptr(buffer) + copy - 1) = '\0';
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XamUserGetXUID(ULONG userIndex, ULONG type, XUID* out)
PPC_FUNC(__imp__XamUserGetXUID)
{
    Kernel::CountImport("XamUserGetXUID");
    if (ctx.r3.u32 != SignedInUser || ctx.r5.u32 == 0)
    {
        ctx.r3.u32 = X_ERROR_NO_SUCH_USER;
        return;
    }
    Guest::Write64(base, ctx.r5.u32, PlayerXuid);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XamUserReadProfileSettings(titleId, userIndex, unk, unk,
//                                     settingCount, settingIds*, sizePtr*,
//                                     buffer*, overlapped)
PPC_FUNC(__imp__XamUserReadProfileSettings)
{
    Kernel::CountImport("XamUserReadProfileSettings");
    const uint32_t settingCount = ctx.r7.u32;
    const uint32_t sizePtr = ctx.r9.u32;
    const uint32_t buffer = ctx.r10.u32;

    // The reply is a header followed by one entry per requested setting. Each
    // entry is madeup of an id, a type, and a value union; 24 bytes covers it.
    constexpr uint32_t HeaderSize = 8;
    constexpr uint32_t EntrySize = 24;
    const uint32_t required = HeaderSize + settingCount * EntrySize;

    if (buffer == 0)
    {
        // The title is asking how much room it needs.
        if (sizePtr != 0)
            Guest::Write32(base, sizePtr, required);
        ctx.r3.u32 = 0x0000007A;   // ERROR_INSUFFICIENT_BUFFER
        return;
    }

    if (sizePtr != 0 && Guest::Read32(base, sizePtr) < required)
    {
        Guest::Write32(base, sizePtr, required);
        ctx.r3.u32 = 0x0000007A;
        return;
    }

    // Report every requested setting as present but unset. There is no profile
    // to read, and a title that finds an empty setting falls back to its own
    // default, which is the behaviour wanted here.
    memset(Guest::Ptr(buffer), 0, required);
    Guest::Write32(base, buffer + 0, settingCount);
    Guest::Write32(base, buffer + 4, buffer + HeaderSize);

    const uint32_t settingIds = ctx.r8.u32;
    for (uint32_t i = 0; i < settingCount; i++)
    {
        const uint32_t entry = buffer + HeaderSize + i * EntrySize;
        if (settingIds != 0)
            Guest::Write32(base, entry + 0, Guest::Read32(base, settingIds + i * 4));
        Guest::Write32(base, entry + 4, 0);   // type: unset
    }

    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XamUserWriteProfileSettings(...)
PPC_FUNC(__imp__XamUserWriteProfileSettings)
{
    Kernel::CountImport("XamUserWriteProfileSettings");
    // Accepted and discarded. Nothing persists a profile yet.
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// --- Controllers -----------------------------------------------------------

// The controller.
//
// Reporting no pad attached is not a neutral answer. The title polls for one
// about five hundred thousand times a second while it waits, which is what made
// it sit on its loading screen and never move: it was not loading, it was
// waiting for a controller that this runtime kept saying was not there. A pad
// on the first port with nothing pressed is the honest stand in until real
// input is wired up.

// X_RESULT XamInputGetState(ULONG userIndex, ULONG flags, XINPUT_STATE* state)
PPC_FUNC(__imp__XamInputGetState)
{
    Kernel::CountImport("XamInputGetState");
    {
        // One look at the thread pointer. Guest code reads thread state
        // through r13, and if this runtime never sets it up, everything it
        // reads through it is whatever happens to be at address zero.
        static std::atomic<int> looked{ 0 };
        if (looked.fetch_add(1) < 3)
        {
            const uint32_t pcr = ctx.r13.u32;
            printf("thread pointer r13 = 0x%08X", pcr);
            if (pcr != 0)
            {
                const uint32_t thread = Guest::Read32(base, pcr + 0x100);
                printf(", *(r13+0x100) = 0x%08X", thread);
                if (thread != 0)
                    printf(", id = %u, +0x58 = %u",
                        Guest::Read32(base, thread + 0x14C),
                        Guest::Read32(base, thread + 0x58));
            }
            printf("\n");
            fflush(stdout);
        }
    }

    if (ctx.r3.u32 != SignedInUser)
    {
        if (ctx.r5.u32 != 0) memset(Guest::Ptr(ctx.r5.u32), 0, 16);
        ctx.r3.u32 = X_ERROR_DEVICE_NOT_CONNECTED;
        return;
    }

    if (ctx.r5.u32 != 0)
    {
        // A packet number, then the pad itself: buttons, two triggers and four
        // thumb stick axes, in the order the console lays them out. The sticks
        // and triggers used to be written as zero whatever the player did,
        // which left the keyboard able to press buttons and nothing able to
        // move or aim.
        const Input::Pad pad = Input::State();

        memset(Guest::Ptr(ctx.r5.u32), 0, 16);
        Guest::Write32(base, ctx.r5.u32 + 0, Input::PacketNumber());
        Guest::Write16(base, ctx.r5.u32 + 4, pad.buttons);
        Guest::Write8(base, ctx.r5.u32 + 6, pad.leftTrigger);
        Guest::Write8(base, ctx.r5.u32 + 7, pad.rightTrigger);
        Guest::Write16(base, ctx.r5.u32 + 8, uint16_t(pad.thumbLX));
        Guest::Write16(base, ctx.r5.u32 + 10, uint16_t(pad.thumbLY));
        Guest::Write16(base, ctx.r5.u32 + 12, uint16_t(pad.thumbRX));
        Guest::Write16(base, ctx.r5.u32 + 14, uint16_t(pad.thumbRY));
    }
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XamInputGetCapabilities(userIndex, flags, XINPUT_CAPABILITIES*)
PPC_FUNC(__imp__XamInputGetCapabilities)
{
    Kernel::CountImport("XamInputGetCapabilities");

    if (ctx.r3.u32 != SignedInUser)
    {
        if (ctx.r5.u32 != 0) memset(Guest::Ptr(ctx.r5.u32), 0, 20);
        ctx.r3.u32 = X_ERROR_DEVICE_NOT_CONNECTED;
        return;
    }

    if (ctx.r5.u32 != 0)
    {
        memset(Guest::Ptr(ctx.r5.u32), 0, 20);
        // Type and subtype both one: an ordinary gamepad. The masks that
        // follow say which controls exist, and a gamepad has all of them.
        Guest::Write32(base, ctx.r5.u32, 0x01010000);
        Guest::Write16(base, ctx.r5.u32 + 4, 0xFFFF);
        Guest::Write16(base, ctx.r5.u32 + 6, 0xFFFF);
        Guest::Write32(base, ctx.r5.u32 + 8, 0xFFFFFFFF);
        Guest::Write32(base, ctx.r5.u32 + 12, 0xFFFFFFFF);
    }
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// X_RESULT XamInputSetState(userIndex, flags, XINPUT_VIBRATION*)
PPC_FUNC(__imp__XamInputSetState)
{
    Kernel::CountImport("XamInputSetState");
    ctx.r3.u32 = (ctx.r3.u32 == SignedInUser)
        ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
}

// --- Notifications ---------------------------------------------------------

// HANDLE XamNotifyCreateListener(ULONGLONG areas)
PPC_FUNC(__imp__XamNotifyCreateListener)
{
    Kernel::CountImport("XamNotifyCreateListener");
    // A handle the title can hold but that never produces anything.
    ctx.r3.u32 = 0x000F0001;
}

// BOOL XNotifyGetNext(HANDLE listener, ULONG id, ULONG* outId, ULONG* outParam)
PPC_FUNC(__imp__XNotifyGetNext)
{
    Kernel::CountImport("XNotifyGetNext");
    if (ctx.r5.u32 != 0) Guest::Write32(base, ctx.r5.u32, 0);
    if (ctx.r6.u32 != 0) Guest::Write32(base, ctx.r6.u32, 0);
    ctx.r3.u32 = 0;   // no notification waiting
}

// --- Memory the dashboard lends the title ----------------------------------

// void* XamAlloc(ULONG flags, ULONG size, void** out)
PPC_FUNC(__imp__XamAlloc)
{
    Kernel::CountImport("XamAlloc");
    // Routed through the guest allocator so these blocks live in guest memory
    // like every other pointer the title holds.
    const uint32_t size = ctx.r4.u32;
    const uint32_t out = ctx.r5.u32;

    ctx.r3.u32 = 0;                 // base address in, zero means anywhere
    ctx.r4.u32 = 0;
    // Reuse NtAllocateVirtualMemory by building its argument block on the
    // guest stack would be circular here, so allocate directly instead.
    extern void __imp__MmAllocatePhysicalMemoryEx(PPCContext&, uint8_t*);
    ctx.r4.u32 = size;
    ctx.r8.u32 = 0;
    __imp__MmAllocatePhysicalMemoryEx(ctx, base);

    const uint32_t address = ctx.r3.u32;
    if (out != 0)
        Guest::Write32(base, out, address);
    ctx.r3.u32 = address != 0 ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED;
}

// void XamFree(void* pointer)
PPC_FUNC(__imp__XamFree)
{
    Kernel::CountImport("XamFree");
    ctx.r4.u32 = ctx.r3.u32;
    extern void __imp__MmFreePhysicalMemory(PPCContext&, uint8_t*);
    __imp__MmFreePhysicalMemory(ctx, base);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

// --- Content and storage ---------------------------------------------------

PPC_FUNC(__imp__XamContentCreateEnumerator)
{
    Kernel::CountImport("XamContentCreateEnumerator");
    if (ctx.r8.u32 != 0) Guest::Write32(base, ctx.r8.u32, 0);
    ctx.r3.u32 = X_ERROR_NOT_FOUND;
}

PPC_FUNC(__imp__XamContentCreateEx)   { ctx.r3.u32 = X_ERROR_NOT_FOUND; }
PPC_FUNC(__imp__XamContentClose)      { ctx.r3.u32 = X_ERROR_SUCCESS; }
PPC_FUNC(__imp__XamContentDelete)     { ctx.r3.u32 = X_ERROR_NOT_FOUND; }
PPC_FUNC(__imp__XamContentGetCreator) { ctx.r3.u32 = X_ERROR_NOT_FOUND; }

// X_RESULT XamEnumerate(HANDLE, flags, buffer, size, itemsReturned*, overlapped)
PPC_FUNC(__imp__XamEnumerate)
{
    Kernel::CountImport("XamEnumerate");
    if (ctx.r7.u32 != 0) Guest::Write32(base, ctx.r7.u32, 0);
    ctx.r3.u32 = X_ERROR_NOT_FOUND;
}

// --- System dialogs --------------------------------------------------------
// Every one answers as if the user dismissed it. A title normally treats that
// as a valid outcome and carries on.

PPC_FUNC(__imp__XamShowSigninUI)          { ctx.r3.u32 = X_ERROR_SUCCESS; }
PPC_FUNC(__imp__XamShowDeviceSelectorUI)  { ctx.r3.u32 = X_ERROR_NOT_FOUND; }
PPC_FUNC(__imp__XamShowMessageBoxUI)      { ctx.r3.u32 = X_ERROR_SUCCESS; }
PPC_FUNC(__imp__XamShowMessageBoxUIEx)    { ctx.r3.u32 = X_ERROR_SUCCESS; }
PPC_FUNC(__imp__XamShowMarketplaceUI)     { ctx.r3.u32 = X_ERROR_SUCCESS; }

PPC_FUNC(__imp__XamShowDirtyDiscErrorUI)
{
    Kernel::CountImport("XamShowDirtyDiscErrorUI");
    printf("\nThe game reported a dirty or unreadable disc. The installed copy\n");
    printf("is missing something it expected to find.\n");
    fflush(stdout);
    Guest::Shutdown();
    Kernel::Exit(1);
}

// --- Gamer tiles -----------------------------------------------------------

PPC_FUNC(__imp__XamParseGamerTileKey)   { ctx.r3.u32 = X_ERROR_FUNCTION_FAILED; }
PPC_FUNC(__imp__XamReadTileToTexture)   { ctx.r3.u32 = X_ERROR_FUNCTION_FAILED; }
PPC_FUNC(__imp__XamWriteGamerTile)      { ctx.r3.u32 = X_ERROR_FUNCTION_FAILED; }

// --- Title lifetime --------------------------------------------------------

PPC_FUNC(__imp__XamGetExecutionId)
{
    Kernel::CountImport("XamGetExecutionId");
    // The title's own execution id, which it uses to identify itself.
    if (ctx.r3.u32 != 0)
        Guest::Write32(base, ctx.r3.u32, 0);
    ctx.r3.u32 = X_ERROR_SUCCESS;
}

PPC_FUNC(__imp__XamLoaderGetLaunchDataSize)
{
    Kernel::CountImport("XamLoaderGetLaunchDataSize");
    if (ctx.r3.u32 != 0) Guest::Write32(base, ctx.r3.u32, 0);
    ctx.r3.u32 = X_ERROR_NOT_FOUND;   // nothing launched us with data
}

PPC_FUNC(__imp__XamLoaderGetLaunchData)  { ctx.r3.u32 = X_ERROR_NOT_FOUND; }
PPC_FUNC(__imp__XamLoaderSetLaunchData)  { ctx.r3.u32 = X_ERROR_SUCCESS; }

PPC_FUNC(__imp__XamLoaderLaunchTitle)
{
    Kernel::CountImport("XamLoaderLaunchTitle");

    // Which title, and from where. A path of nothing is the dashboard, and a
    // title that asks for the dashboard has usually just decided that
    // something is fatally wrong: the call chain says what.
    printf("\nThe game asked to launch another title");
    if (ctx.r3.u32 != 0)
    {
        char path[256] = {};
        for (size_t i = 0; i + 1 < sizeof(path); i++)
        {
            path[i] = char(Guest::Base[ctx.r3.u32 + i]);
            if (path[i] == 0) break;
        }
        printf(": \"%s\"", path);
    }
    else
    {
        printf(": the dashboard");
    }
    printf(", flags 0x%08X, from 0x%08X.\n", ctx.r4.u32, uint32_t(ctx.lr));
    {
        uint32_t functions[12] = {};
        const int count = Sampler::FunctionsOnStack(functions, 12);
        if (count > 0)
        {
            printf("  guest call chain, innermost first:");
            for (int i = 0; i < count; i++)
                printf("%ssub_%08X", i == 0 ? " " : " <- ", functions[i]);
            printf("\n");
        }
    }
    Kernel::ReportRecentCalls(GetCurrentThreadId());
    printf("Shutting down.\n");
    fflush(stdout);
    Guest::Shutdown();
    Kernel::Exit(0);
}

PPC_FUNC(__imp__XamLoaderTerminateTitle)
{
    Kernel::CountImport("XamLoaderTerminateTitle");
    printf("\nThe game asked to terminate. Shutting down.\n");
    fflush(stdout);
    Guest::Shutdown();
    Kernel::Exit(0);
}

// --- Cross process messages ------------------------------------------------

PPC_FUNC(__imp__XMsgInProcessCall)   { ctx.r3.u32 = X_ERROR_FUNCTION_FAILED; }
PPC_FUNC(__imp__XMsgStartIORequest)  { ctx.r3.u32 = X_ERROR_FUNCTION_FAILED; }
PPC_FUNC(__imp__XMsgStartIORequestEx){ ctx.r3.u32 = X_ERROR_FUNCTION_FAILED; }
