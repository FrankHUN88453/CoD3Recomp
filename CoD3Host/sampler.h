#pragma once

// Periodically reports which guest function each guest thread is executing.
// Useful when the title stops making progress without asking the runtime for
// anything, which from the outside looks identical to a hang.

namespace Sampler
{
    // The guest function a host address belongs to, or zero. Used by the fault
    // handler to name which translated function went wrong.
    uint32_t FunctionAt(unsigned long long hostAddress);

    // Names the guest functions on a stack that is already stopped, which is
    // what a fault handler has. Returns how many were written.
    int WalkGuestStack(void* winContext, uint32_t* functions, int limit);

    // The same, for code that is running now rather than stopped in a handler.
    int FunctionsOnStack(uint32_t* functions, int limit);

    void Start(int intervalSeconds);
    void Stop();
}
