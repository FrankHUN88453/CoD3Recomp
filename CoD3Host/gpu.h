#pragma once

// The GPU register file and command processor.

#include <cstdint>

namespace Gpu
{
    // Registers are addressed by their byte address in the aperture, which is
    // what the driver actually writes. These were found by watching what it
    // touched, not assumed: see the discovery reporting in gpu.cpp.
    inline constexpr uint32_t ApertureBase = 0x7FC80000;
    inline constexpr uint32_t RegisterWritePointer = 0x7FC80714;

    // The vertical blank handler reads this and does nothing unless bit 0 is
    // set. Found by disassembling the callback the title installs: on the
    // source 0 path it loads 0x7FC86544, keeps the low bit, and returns
    // immediately when it is clear.
    inline constexpr uint32_t RegisterInterruptStatus = 0x7FC86544;

    uint32_t ReadRegister(uint32_t address);
    void WriteRegister(uint32_t address, uint32_t value);

    // Where the driver says it has filled the ring, in dwords.
    uint32_t WritePointer();

    // A copy of a range of registers, by index, taken under the lock once.
    void SnapshotRegisters(uint32_t firstIndex, uint32_t count, uint32_t* out);

    // How many interrupts the command stream has asked for since this was last
    // called, and clears the count. The command thread raises them.
    uint32_t TakePendingInterrupts();

    // Installs the routine that raises an interrupt where the packet stands,
    // on the command thread. Until one is installed they are only counted.
    void SetInterruptRaiser(void (*raiser)(uint32_t cpuMask));

    // Every address a fence has been written to, with a count.
    void ReportFences();

    struct Statistics
    {
        uint64_t packets = 0;
        uint64_t registerWrites = 0;
        uint64_t draws = 0;
        uint64_t swaps = 0;
        uint64_t unknown = 0;
        uint32_t readPointer = 0;
    };

    // Consumes what the driver has queued, up to its write pointer or until
    // the budget runs out, whichever comes first. Returns the new read
    // pointer. The budget matters because the work behind a packet is done in
    // software here: without it, one busy buffer stops the vertical blank from
    // happening at all and the whole title slows to the rate this can draw.
    uint32_t ProcessRing(uint32_t ringBase, uint32_t ringSizeDwords,
                         uint32_t readPointer, uint32_t writePointer,
                         uint32_t budgetMicroseconds = 0);

    Statistics Stats();

    // The context the title passed to VdSetGraphicsInterruptCallback. Its own
    // ring bookkeeping hangs off this, which is how the fields it spins on can
    // be read from outside.
    uint32_t InterruptContext();

    // Reports a register the driver is reading in a loop, which is how a wait
    // for the GPU shows up from this side.
    void ReportPolling();

    // What kinds of packet the title is actually sending.
    void ReportPacketMix();
}
