#pragma once

// 0.5.391: the drawing thread. The command thread works out each draw
// (render_commands.cpp's PrepareDraw) and, instead of making the Direct3D
// calls itself, puts the draw in a queue that a second thread carries out
// on the device's context, so working out the next draw overlaps with
// sending the last one. Everything the queue holds was read from the
// title's memory when it was put in (the constants, the indices and the
// vertex data are copied in), so the fences and the order the title sees
// are as before. Anything else that uses the context first waits for the
// drawing thread to finish what it has (Sync), then uses it directly.
// COD3_DRAWTHREAD=0: off, everything on the command thread as before.

#include <cstdint>

struct ID3D11Buffer;
struct ID3D11Resource;
struct D3D11_BOX;

namespace RenderQueue
{
    // This thread is putting the context's work in the queue (the command
    // thread, inside a draw): uploads go in the queue, anything else waits.
    bool Deferring();

    // The drawing thread finished everything given to it so far: the context
    // and the drawing thread's bindings are this thread's to use directly
    // until it gives the queue more. Cheap when there is nothing queued.
    void Sync(int site = 0);   // site: which caller, for the profile's "queue:" line

    // A ring buffer's bytes at offset, written on the drawing thread with a
    // map (discarding the buffer first when `discard`): where to put them now.
    // The biggest ring write the queue takes (bigger ones are mapped by the
    // caller after a Sync).
    constexpr uint32_t MaxRingWrite = 512u << 10;
    uint8_t* RingWrite(ID3D11Buffer* buffer, bool discard, uint32_t offset, uint32_t bytes);

    // UpdateSubresource, in the queue with a copy of the data when deferring,
    // else at once. A buffer with no box: bytes 0 means the whole buffer.
    void Upload(ID3D11Resource* resource, unsigned subresource, const D3D11_BOX* box, const void* data, uint32_t bytes);
}
