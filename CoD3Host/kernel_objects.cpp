// Xbox 360 kernel imports: handles, synchronisation objects, and networking.
//
// Two different kinds of object live here. Nt* calls work on handles from a
// table this file owns. Ke* calls work on dispatcher objects the title keeps
// in its own memory, so those read and write a guest structure directly.
//
// Guest threads are real host threads, so all of this is shared state. One
// lock covers the handle table and every signal state; waiters block on one
// condition variable and re-check their own condition when anything changes.

#include "kernel.h"
#include "scheduler.h"
#include <atomic>
#include "gpu.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <Windows.h>

namespace
{
    constexpr uint32_t X_STATUS_SUCCESS            = 0x00000000;
    constexpr uint32_t X_STATUS_TIMEOUT            = 0x00000102;
    constexpr uint32_t X_STATUS_INVALID_HANDLE     = 0xC0000008;
    constexpr uint32_t X_STATUS_INVALID_PARAMETER  = 0xC000000D;

    // Offsets inside the dispatcher header the title embeds in its own events,
    // semaphores and mutants. The type is the high byte of the first word,
    // because the guest is big endian.
    constexpr uint32_t DISPATCH_TYPE         = 0x00;
    constexpr uint32_t DISPATCH_SIGNAL_STATE = 0x04;

    constexpr uint32_t DispatchNotificationEvent   = 0;
    constexpr uint32_t DispatchSynchronizationEvent = 1;
    constexpr uint32_t DispatchMutant              = 2;
    constexpr uint32_t DispatchSemaphore           = 5;

    uint32_t DispatchType(const uint8_t* base, uint32_t object)
    {
        return (Guest::Read32(base, object + DISPATCH_TYPE) >> 24) & 0xFF;
    }

    // A wait consumes the signal of a synchronisation event, a mutant or a
    // semaphore. A notification event stays set until it is explicitly reset,
    // which is the whole point of it: every thread waiting on one is released,
    // not just the first. Clearing it here would strand the rest forever.
    bool WaitConsumes(uint32_t type)
    {
        return type == DispatchSynchronizationEvent ||
               type == DispatchMutant ||
               type == DispatchSemaphore;
    }

    enum class ObjectType { Event, Semaphore, Mutant, File, Thread, Other };

    struct Object
    {
        ObjectType type = ObjectType::Other;
        bool manualReset = false;
        int32_t signalled = 0;
        int32_t limit = 0;         // semaphore maximum
        std::string name;

        // A mutant is recursive: the thread that holds it can acquire it again
        // without blocking, and only the matching number of releases frees it.
        // Treating one as a plain count deadlocks the first thread that takes
        // it twice, which is what happened here.
        uint32_t owner = 0;
        int32_t recursion = 0;

        // How many times this object has been pulsed.
        //
        // A pulse releases every thread waiting at that moment and then leaves
        // the object clear. A signal state cannot say that: setting it to one
        // releases a single waiter, and setting it and clearing it releases
        // none. This runtime did the second, which is why four worker threads
        // sat on their job queue with no timeout while the two pulses meant to
        // start them went nowhere. A waiter records this counter when it starts
        // and is released as soon as it changes.
        uint32_t pulses = 0;

        // Where this object lives in guest memory.
        //
        // The title can turn a handle into a pointer with
        // ObReferenceObjectByHandle and then wait on the pointer with the Ke
        // family, which reads the signal state straight out of guest memory.
        // Handing back the handle value as if it were a pointer, which is what
        // this used to do, sent those waits to read address 0x104 and similar:
        // always zero, never signalled, and the thread waiting for its data to
        // load never woke up.
        uint32_t guestAddress = 0;
    };

    // The dispatcher header is sixteen bytes: a type and size, then the signal
    // state, then a list head this runtime does not model.
    constexpr uint32_t ObjectBodySize = 32;

    int32_t SignalState(Object& object)
    {
        if (object.guestAddress != 0)
            object.signalled = int32_t(
                Guest::Read32(Guest::Base, object.guestAddress + DISPATCH_SIGNAL_STATE));
        return object.signalled;
    }

    void SetSignalState(Object& object, int32_t value)
    {
        object.signalled = value;
        if (object.guestAddress != 0)
            Guest::Write32(Guest::Base, object.guestAddress + DISPATCH_SIGNAL_STATE,
                uint32_t(value));
    }

    void GiveBody(Object& object)
    {
        object.guestAddress = Guest::AllocateKernelObject(ObjectBodySize);
        if (object.guestAddress == 0) return;
        // Type in the low byte, so guest code that inspects it sees something
        // consistent. Event, mutant and semaphore use the values the console
        // does.
        const uint32_t type = object.type == ObjectType::Mutant ? 2u
                            : object.type == ObjectType::Semaphore ? 5u
                            : (object.manualReset ? 0u : 1u);
        Guest::Write32(Guest::Base, object.guestAddress, type << 24);
        Guest::Write32(Guest::Base, object.guestAddress + DISPATCH_SIGNAL_STATE,
            uint32_t(object.signalled));
    }

    std::map<uint32_t, Object>& Handles()
    {
        static std::map<uint32_t, Object> handles;
        return handles;
    }

    uint32_t g_nextHandle = 0x00000100;

    // Which handle is which matters when one of them is never signalled: the
    // kind of object and where it was created point straight at the code that
    // was supposed to signal it.
    uint32_t CreateHandle(Object object, uint32_t from)
    {
        const uint32_t handle = g_nextHandle;
        g_nextHandle += 4;

        const char* kind = "object";
        switch (object.type)
        {
        case ObjectType::Event:     kind = object.manualReset ? "notification event"
                                                             : "synchronisation event"; break;
        case ObjectType::Semaphore: kind = "semaphore"; break;
        case ObjectType::Mutant:    kind = "mutant"; break;
        default: break;
        }

        printf("object: handle 0x%08X is a %s, initially %d, created from 0x%08X\n",
            handle, kind, object.signalled, from);
        fflush(stdout);

        Handles()[handle] = std::move(object);
        return handle;
    }

    Object* Find(uint32_t handle)
    {
        auto found = Handles().find(handle);
        return found != Handles().end() ? &found->second : nullptr;
    }

    // An object that is waited on and never signalled is what a stalled boot
    // looks like from here, so both sides are counted and compared.
    std::mutex g_trafficMutex;
    std::map<uint32_t, uint64_t> g_waits;
    std::map<uint32_t, uint64_t> g_signals;

    // Totals say what is never signalled; the order says why. The first few
    // dozen events are kept so the sequence that leads into the deadlock can be
    // read back rather than guessed at.
    struct Event
    {
        uint32_t thread;
        const char* action;
        uint32_t object;
        uint32_t from;
    };
    std::vector<Event> g_events;
    constexpr size_t EventLimit = 64;

    // A ring, not a prefix: the interesting events are the last ones before
    // everything stopped, not the first ones after it started.
    size_t g_eventCursor = 0;

    void Record(const char* action, uint32_t object, uint32_t from)
    {
        std::lock_guard<std::mutex> lock(g_trafficMutex);
        if (g_events.size() < EventLimit)
        {
            g_events.push_back({ GetCurrentThreadId(), action, object, from });
        }
        else
        {
            g_events[g_eventCursor] = { GetCurrentThreadId(), action, object, from };
            g_eventCursor = (g_eventCursor + 1) % EventLimit;
        }
    }

    // What each thread is blocked on right now, as opposed to what it has ever
    // waited on. This is the state that matters when everything has stopped.
    struct Blocked { uint32_t object; uint32_t from; bool infinite; };
    std::map<uint32_t, Blocked> g_blocked;

    void EnterWait(uint32_t object, uint32_t from, bool infinite)
    {
        std::lock_guard<std::mutex> lock(g_trafficMutex);
        g_blocked[GetCurrentThreadId()] = { object, from, infinite };
    }

    void LeaveWait()
    {
        std::lock_guard<std::mutex> lock(g_trafficMutex);
        g_blocked.erase(GetCurrentThreadId());
    }

    void CountWait(uint32_t object, uint32_t from)
    {
        {
            std::lock_guard<std::mutex> lock(g_trafficMutex);
            g_waits[object]++;
        }
        Record("wait  ", object, from);
    }

    void CountSignal(uint32_t object, uint32_t from)
    {
        {
            std::lock_guard<std::mutex> lock(g_trafficMutex);
            g_signals[object]++;
        }
        Record("signal", object, from);
    }

    // A wait that never completes is the usual way a boot stalls, and it is
    // invisible from outside. Reporting one after a few seconds names what the
    // title is blocked on.
    void ReportStalledWait(const char* what, uint32_t object, uint32_t from)
    {
        static std::mutex reported;
        static std::map<uint32_t, bool> seen;
        std::lock_guard<std::mutex> lock(reported);
        if (seen[from]) return;
        seen[from] = true;
        printf("stalled: waiting on %s 0x%08X, called from 0x%08X\n", what, object, from);
        fflush(stdout);
    }

    // Guest timeouts are 100 nanosecond units; negative means relative.
    // Returns false when the caller asked not to wait at all.
    bool TimeoutToDeadline(const uint8_t* base, uint32_t timeoutPtr,
                           std::chrono::steady_clock::time_point& deadline,
                           bool& infinite)
    {
        infinite = (timeoutPtr == 0);
        if (infinite) return true;

        const int64_t timeout = static_cast<int64_t>(Guest::Read64(base, timeoutPtr));
        if (timeout == 0) return false;                  // poll

        const int64_t hundredNanoseconds = timeout < 0 ? -timeout : timeout;
        deadline = std::chrono::steady_clock::now() +
                   std::chrono::nanoseconds(hundredNanoseconds * 100);
        return true;
    }
}

std::mutex& Kernel::DispatcherLock()
{
    static std::mutex lock;
    return lock;
}

std::condition_variable& Kernel::DispatcherChanged()
{
    static std::condition_variable changed;
    return changed;
}

// --- Handles ---------------------------------------------------------------

// NTSTATUS NtClose(HANDLE handle)
PPC_FUNC(__imp__NtClose)
{
    Kernel::CountImport("NtClose");
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    auto found = Handles().find(ctx.r3.u32);
    if (found != Handles().end())
    {
        Handles().erase(found);
        ctx.r3.u32 = X_STATUS_SUCCESS;
        return;
    }

    // File handles come from a separate table.
    if (Kernel::CloseFileHandle(ctx.r3.u32))
    {
        ctx.r3.u32 = X_STATUS_SUCCESS;
        return;
    }

    ctx.r3.u32 = X_STATUS_INVALID_HANDLE;
}

// NTSTATUS ObReferenceObjectByHandle(HANDLE handle, void* type, void** out)
PPC_FUNC(__imp__ObReferenceObjectByHandle)
{
    Kernel::CountImport("ObReferenceObjectByHandle");
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    const uint32_t handle = ctx.r3.u32;
    if (Find(handle) == nullptr)
    {
        // Thread handles live in their own table, and a title that wants to
        // resume or wait on a thread asks for its object this way.
        const uint32_t threadObject = Kernel::ThreadObjectFor(handle);
        if (threadObject != 0)
        {
            if (ctx.r5.u32 != 0) Guest::Write32(base, ctx.r5.u32, threadObject);
            ctx.r3.u32 = X_STATUS_SUCCESS;
            return;
        }

        printf("ObReferenceObjectByHandle: 0x%08X is not an object this runtime "
               "knows, from 0x%08X\n", handle, uint32_t(ctx.lr));
        fflush(stdout);
        ctx.r3.u32 = X_STATUS_INVALID_HANDLE;
        return;
    }
    Object* object = Find(handle);
    if (ctx.r5.u32 != 0)
        Guest::Write32(base, ctx.r5.u32,
            object->guestAddress != 0 ? object->guestAddress : handle);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// VOID ObDereferenceObject(void* object)
PPC_FUNC(__imp__ObDereferenceObject)
{
    Kernel::CountImport("ObDereferenceObject");
    // Reference counting is not modelled: handles live until NtClose.
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS ObCreateSymbolicLink(STRING* path, STRING* target)
PPC_FUNC(__imp__ObCreateSymbolicLink)
{
    Kernel::CountImport("ObCreateSymbolicLink");
    // Device links such as game: are resolved by the file layer instead.
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS ObDeleteSymbolicLink(STRING* path)
PPC_FUNC(__imp__ObDeleteSymbolicLink) { ctx.r3.u32 = X_STATUS_SUCCESS; }

// --- Events, by handle -----------------------------------------------------

// NTSTATUS NtCreateEvent(HANDLE* out, OBJECT_ATTRIBUTES* attr,
//                        ULONG type, BOOLEAN initialState)
PPC_FUNC(__imp__NtCreateEvent)
{
    Kernel::CountImport("NtCreateEvent");
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object object;
    object.type = ObjectType::Event;
    object.manualReset = (ctx.r5.u32 == 0);   // 0 notification, 1 synchronisation
    object.signalled = ctx.r6.u32 != 0 ? 1 : 0;
    GiveBody(object);

    const uint32_t handle = CreateHandle(std::move(object), uint32_t(ctx.lr));
    if (ctx.r3.u32 != 0)
        Guest::Write32(base, ctx.r3.u32, handle);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtSetEvent(HANDLE handle, LONG* previousState)
PPC_FUNC(__imp__NtSetEvent)
{
    Kernel::CountImportOn("NtSetEvent", ctx.r3.u32);
    CountSignal(ctx.r3.u32, uint32_t(ctx.lr));
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object* object = Find(ctx.r3.u32);
    if (object == nullptr) { ctx.r3.u32 = X_STATUS_INVALID_HANDLE; return; }

    if (ctx.r4.u32 != 0)
        Guest::Write32(base, ctx.r4.u32, uint32_t(SignalState(*object)));
    SetSignalState(*object, 1);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtClearEvent(HANDLE handle)
PPC_FUNC(__imp__NtClearEvent)
{
    Kernel::CountImportOn("NtClearEvent", ctx.r3.u32);
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object* object = Find(ctx.r3.u32);
    if (object == nullptr) { ctx.r3.u32 = X_STATUS_INVALID_HANDLE; return; }
    SetSignalState(*object, 0);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtPulseEvent(HANDLE handle, LONG* previousState)
PPC_FUNC(__imp__NtPulseEvent)
{
    Kernel::CountImportOn("NtPulseEvent", ctx.r3.u32);
    CountSignal(ctx.r3.u32, uint32_t(ctx.lr));
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object* object = Find(ctx.r3.u32);
    if (object == nullptr) { ctx.r3.u32 = X_STATUS_INVALID_HANDLE; return; }

    if (ctx.r4.u32 != 0)
        Guest::Write32(base, ctx.r4.u32, uint32_t(SignalState(*object)));

    // Everyone waiting right now goes; the event is left clear for anyone who
    // arrives afterwards.
    object->pulses++;
    SetSignalState(*object, 0);

    // How many that was. A pulse that finds nobody waiting is lost, on the
    // console as here, and a job system that relies on one being caught is
    // exactly where a run goes quiet with its workers asleep.
    {
        int waiting = 0;
        for (const auto& entry : g_blocked)
            if (entry.second.object == ctx.r3.u32) waiting++;
        static std::atomic<int> announced{ 0 };
        if (announced.fetch_add(1) < 120)
        {
            printf("pulse: 0x%08X from 0x%08X released %d waiter%s\n",
                ctx.r3.u32, uint32_t(ctx.lr), waiting, waiting == 1 ? "" : "s");
            fflush(stdout);
        }
    }
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// --- Semaphores and mutants, by handle -------------------------------------

// NTSTATUS NtCreateSemaphore(HANDLE* out, OBJECT_ATTRIBUTES*, LONG count, LONG limit)
PPC_FUNC(__imp__NtCreateSemaphore)
{
    Kernel::CountImport("NtCreateSemaphore");
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object object;
    object.type = ObjectType::Semaphore;
    object.signalled = int32_t(ctx.r5.u32);
    object.limit = int32_t(ctx.r6.u32);
    GiveBody(object);

    const uint32_t handle = CreateHandle(std::move(object), uint32_t(ctx.lr));
    if (ctx.r3.u32 != 0)
        Guest::Write32(base, ctx.r3.u32, handle);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtReleaseSemaphore(HANDLE handle, LONG count, LONG* previous)
PPC_FUNC(__imp__NtReleaseSemaphore)
{
    Kernel::CountImportOn("NtReleaseSemaphore", ctx.r3.u32);
    CountSignal(ctx.r3.u32, uint32_t(ctx.lr));
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object* object = Find(ctx.r3.u32);
    if (object == nullptr) { ctx.r3.u32 = X_STATUS_INVALID_HANDLE; return; }

    if (ctx.r5.u32 != 0)
        Guest::Write32(base, ctx.r5.u32, uint32_t(SignalState(*object)));
    int32_t released = SignalState(*object) + int32_t(ctx.r4.u32);
    if (object->limit > 0 && released > object->limit) released = object->limit;
    SetSignalState(*object, released);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtCreateMutant(HANDLE* out, OBJECT_ATTRIBUTES*, BOOLEAN owned)
PPC_FUNC(__imp__NtCreateMutant)
{
    Kernel::CountImport("NtCreateMutant");
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object object;
    object.type = ObjectType::Mutant;
    object.signalled = ctx.r5.u32 != 0 ? 0 : 1;   // owned means not available
    GiveBody(object);

    const uint32_t handle = CreateHandle(std::move(object), uint32_t(ctx.lr));
    if (ctx.r3.u32 != 0)
        Guest::Write32(base, ctx.r3.u32, handle);
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtReleaseMutant(HANDLE handle, LONG* previous)
PPC_FUNC(__imp__NtReleaseMutant)
{
    Kernel::CountImportOn("NtReleaseMutant", ctx.r3.u32);
    CountSignal(ctx.r3.u32, uint32_t(ctx.lr));
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object* object = Find(ctx.r3.u32);
    if (object == nullptr) { ctx.r3.u32 = X_STATUS_INVALID_HANDLE; return; }

    if (ctx.r4.u32 != 0)
        Guest::Write32(base, ctx.r4.u32, uint32_t(SignalState(*object)));

    // Only the matching release frees it, and only for the thread that holds it.
    if (object->recursion > 0) object->recursion--;
    if (object->recursion == 0)
    {
        object->owner = 0;
        SetSignalState(*object, 1);
    }
    ctx.r3.u32 = X_STATUS_SUCCESS;
}

// NTSTATUS NtWaitForSingleObjectEx(HANDLE handle, mode, alertable, timeout*)
PPC_FUNC(__imp__NtWaitForSingleObjectEx)
{
    Kernel::CountImportOn("NtWaitForSingleObjectEx", ctx.r3.u32);
    Scheduler::Release();
    struct Resume { ~Resume() { Scheduler::Acquire(); } } resume;
    Kernel::DeliverApcs(ctx, ctx.r5.u32 != 0);
    const uint32_t handle = ctx.r3.u32;
    CountWait(handle, uint32_t(ctx.lr));
    const uint32_t timeoutPtr = ctx.r6.u32;

    std::chrono::steady_clock::time_point deadline;
    bool infinite = false;
    const bool mayWait = TimeoutToDeadline(base, timeoutPtr, deadline, infinite);

    EnterWait(handle, uint32_t(ctx.lr), infinite);
    struct Leave { ~Leave() { LeaveWait(); } } leave;

    // A thread handle is not in the object table: threads keep their own. A
    // wait on one is a wait for that thread to end, and it completes when the
    // thread's object in guest memory is signalled.
    {
        std::unique_lock<std::mutex> lock(Kernel::DispatcherLock());
        if (Find(handle) == nullptr)
        {
            const uint32_t threadObject = Kernel::ThreadObjectFor(handle);
            if (threadObject != 0)
            {
                for (;;)
                {
                    if (Guest::Read32(base, threadObject + DISPATCH_SIGNAL_STATE) > 0)
                    {
                        ctx.r3.u32 = X_STATUS_SUCCESS;
                        return;
                    }
                    if (!mayWait) { ctx.r3.u32 = X_STATUS_TIMEOUT; return; }
                    if (infinite)
                    {
                        Kernel::DispatcherChanged().wait_for(
                            lock, std::chrono::milliseconds(20));
                    }
                    else if (Kernel::DispatcherChanged().wait_until(lock, deadline) ==
                             std::cv_status::timeout)
                    {
                        ctx.r3.u32 = X_STATUS_TIMEOUT;
                        return;
                    }
                }
            }
        }
    }

    std::unique_lock<std::mutex> lock(Kernel::DispatcherLock());

    // A pulse that happens while this wait is in progress releases it.
    uint32_t pulsesAtStart = 0;
    if (Object* start = Find(handle)) pulsesAtStart = start->pulses;

    for (;;)
    {
        Object* object = Find(handle);
        if (object == nullptr) { ctx.r3.u32 = X_STATUS_INVALID_HANDLE; return; }

        // The thread that already holds a mutant takes it again without
        // waiting. Anything else would deadlock a lock taken recursively,
        // which is normal and which the console allows.
        if (object->type == ObjectType::Mutant &&
            object->owner == GetCurrentThreadId())
        {
            object->recursion++;
            ctx.r3.u32 = X_STATUS_SUCCESS;
            return;
        }

        if (object->pulses != pulsesAtStart)
        {
            ctx.r3.u32 = X_STATUS_SUCCESS;
            return;
        }

        if (SignalState(*object) > 0)
        {
            if (object->type == ObjectType::Mutant)
            {
                SetSignalState(*object, 0);
                object->owner = GetCurrentThreadId();
                object->recursion = 1;
            }
            else if (object->type == ObjectType::Semaphore ||
                     (object->type == ObjectType::Event && !object->manualReset))
            {
                // A semaphore and a synchronisation event each consume the
                // signal they satisfy. A notification event does not.
                SetSignalState(*object, SignalState(*object) - 1);
            }
            ctx.r3.u32 = X_STATUS_SUCCESS;
            return;
        }

        if (!mayWait) { ctx.r3.u32 = X_STATUS_TIMEOUT; return; }

        if (infinite)
        {
            if (Kernel::DispatcherChanged().wait_for(lock, std::chrono::seconds(3)) ==
                std::cv_status::timeout)
                ReportStalledWait("handle", handle, uint32_t(ctx.lr));
        }
        else if (Kernel::DispatcherChanged().wait_until(lock, deadline) ==
                 std::cv_status::timeout)
        {
            ctx.r3.u32 = X_STATUS_TIMEOUT;
            return;
        }
    }
}

// --- Dispatcher objects in guest memory ------------------------------------

// LONG KeSetEvent(KEVENT* event, LONG increment, BOOLEAN wait)
PPC_FUNC(__imp__KeSetEvent)
{
    Kernel::CountImportOn("KeSetEvent", ctx.r3.u32);
    CountSignal(ctx.r3.u32, uint32_t(ctx.lr));
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    const uint32_t event = ctx.r3.u32;
    if (event == 0) { ctx.r3.u32 = 0; return; }

    const uint32_t previous = Guest::Read32(base, event + DISPATCH_SIGNAL_STATE);
    Guest::Write32(base, event + DISPATCH_SIGNAL_STATE, 1);
    ctx.r3.u32 = previous;
}

// LONG KeResetEvent(KEVENT* event)
PPC_FUNC(__imp__KeResetEvent)
{
    Kernel::CountImportOn("KeResetEvent", ctx.r3.u32);
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    const uint32_t event = ctx.r3.u32;
    if (event == 0) { ctx.r3.u32 = 0; return; }

    const uint32_t previous = Guest::Read32(base, event + DISPATCH_SIGNAL_STATE);
    Guest::Write32(base, event + DISPATCH_SIGNAL_STATE, 0);
    ctx.r3.u32 = previous;
}

// VOID KeInitializeSemaphore(KSEMAPHORE* semaphore, LONG count, LONG limit)
PPC_FUNC(__imp__KeInitializeSemaphore)
{
    Kernel::CountImport("KeInitializeSemaphore");
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    const uint32_t semaphore = ctx.r3.u32;
    if (semaphore == 0) return;

    memset(Guest::Ptr(semaphore), 0, 0x18);
    Guest::Write32(base, semaphore + DISPATCH_TYPE, 5u << 24);   // SemaphoreObject
    Guest::Write32(base, semaphore + DISPATCH_SIGNAL_STATE, ctx.r4.u32);
    Guest::Write32(base, semaphore + 0x14, ctx.r5.u32);          // limit
}

// LONG KeReleaseSemaphore(KSEMAPHORE* semaphore, LONG increment, LONG adjust, BOOLEAN wait)
PPC_FUNC(__imp__KeReleaseSemaphore)
{
    Kernel::CountImportOn("KeReleaseSemaphore", ctx.r3.u32);
    CountSignal(ctx.r3.u32, uint32_t(ctx.lr));
    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    const uint32_t semaphore = ctx.r3.u32;
    if (semaphore == 0) { ctx.r3.u32 = 0; return; }

    const uint32_t previous = Guest::Read32(base, semaphore + DISPATCH_SIGNAL_STATE);
    const uint32_t limit = Guest::Read32(base, semaphore + 0x14);
    uint32_t count = previous + ctx.r5.u32;
    if (limit > 0 && count > limit) count = limit;

    Guest::Write32(base, semaphore + DISPATCH_SIGNAL_STATE, count);
    ctx.r3.u32 = previous;
}

// NTSTATUS KeWaitForSingleObject(void* object, reason, mode, alertable, timeout*)
PPC_FUNC(__imp__KeWaitForSingleObject)
{
    Kernel::CountImportOn("KeWaitForSingleObject", ctx.r3.u32);
    Scheduler::Release();
    struct Resume { ~Resume() { Scheduler::Acquire(); } } resume;
    Kernel::DeliverApcs(ctx, ctx.r6.u32 != 0);
    const uint32_t object = ctx.r3.u32;
    CountWait(object, uint32_t(ctx.lr));
    const uint32_t timeoutPtr = ctx.r7.u32;
    if (object == 0) { ctx.r3.u32 = X_STATUS_INVALID_PARAMETER; return; }

    std::chrono::steady_clock::time_point deadline;
    bool infinite = false;
    const bool mayWait = TimeoutToDeadline(base, timeoutPtr, deadline, infinite);

    EnterWait(object, uint32_t(ctx.lr), infinite);
    struct Leave { ~Leave() { LeaveWait(); } } leave;

    std::unique_lock<std::mutex> lock(Kernel::DispatcherLock());
    for (;;)
    {
        const uint32_t state = Guest::Read32(base, object + DISPATCH_SIGNAL_STATE);
        if (state > 0)
        {
            if (WaitConsumes(DispatchType(base, object)))
                Guest::Write32(base, object + DISPATCH_SIGNAL_STATE, state - 1);
            ctx.r3.u32 = X_STATUS_SUCCESS;
            return;
        }

        if (!mayWait) { ctx.r3.u32 = X_STATUS_TIMEOUT; return; }

        if (infinite)
        {
            if (Kernel::DispatcherChanged().wait_for(lock, std::chrono::seconds(3)) ==
                std::cv_status::timeout)
                ReportStalledWait("dispatcher object", object, uint32_t(ctx.lr));
        }
        else if (Kernel::DispatcherChanged().wait_until(lock, deadline) ==
                 std::cv_status::timeout)
        {
            ctx.r3.u32 = X_STATUS_TIMEOUT;
            return;
        }
    }
}

// NTSTATUS KeWaitForMultipleObjects(count, objects[], type, ...)
PPC_FUNC(__imp__KeWaitForMultipleObjects)
{
    Kernel::CountImport("KeWaitForMultipleObjects");
    Scheduler::Release();
    struct Resume { ~Resume() { Scheduler::Acquire(); } } resume;
    const uint32_t count = ctx.r3.u32;
    const uint32_t objects = ctx.r4.u32;
    const uint32_t waitType = ctx.r5.u32;   // 0 all, 1 any
    const uint32_t timeoutPtr = ctx.r9.u32;

    if (count == 0 || objects == 0) { ctx.r3.u32 = X_STATUS_INVALID_PARAMETER; return; }

    std::chrono::steady_clock::time_point deadline;
    bool infinite = false;
    const bool mayWait = TimeoutToDeadline(base, timeoutPtr, deadline, infinite);

    std::unique_lock<std::mutex> lock(Kernel::DispatcherLock());
    for (;;)
    {
        uint32_t signalled = 0;
        uint32_t firstSignalled = 0xFFFFFFFF;
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t object = Guest::Read32(base, objects + i * 4);
            if (object == 0) continue;
            if (Guest::Read32(base, object + DISPATCH_SIGNAL_STATE) > 0)
            {
                signalled++;
                if (firstSignalled == 0xFFFFFFFF) firstSignalled = i;
            }
        }

        if ((waitType == 1 && signalled > 0) || (waitType == 0 && signalled == count))
        {
            // Consume only what a wait is supposed to consume.
            for (uint32_t i = 0; i < count; i++)
            {
                if (waitType == 1 && i != firstSignalled) continue;
                const uint32_t object = Guest::Read32(base, objects + i * 4);
                if (object == 0) continue;
                if (!WaitConsumes(DispatchType(base, object))) continue;
                const uint32_t state = Guest::Read32(base, object + DISPATCH_SIGNAL_STATE);
                if (state > 0)
                    Guest::Write32(base, object + DISPATCH_SIGNAL_STATE, state - 1);
            }
            ctx.r3.u32 = (waitType == 1) ? firstSignalled : 0;
            return;
        }

        if (!mayWait) { ctx.r3.u32 = X_STATUS_TIMEOUT; return; }

        if (infinite)
        {
            Kernel::DispatcherChanged().wait(lock);
        }
        else if (Kernel::DispatcherChanged().wait_until(lock, deadline) ==
                 std::cv_status::timeout)
        {
            ctx.r3.u32 = X_STATUS_TIMEOUT;
            return;
        }
    }
}

// --- Networking ------------------------------------------------------------

// INT NetDll_WSAStartup(ULONG caller, WORD version, WSADATA* data)
PPC_FUNC(__imp__NetDll_WSAStartup)
{
    Kernel::CountImport("NetDll_WSAStartup");
    // Reported as available so the title's network layer initialises. Nothing
    // below this does anything: no socket call is implemented, and there are
    // no socket imports in this executable beyond startup and cleanup.
    const uint32_t data = ctx.r5.u32;
    if (data != 0)
        memset(Guest::Ptr(data), 0, 0x190);
    ctx.r3.u32 = 0;
}

// INT NetDll_WSACleanup(ULONG caller)
PPC_FUNC(__imp__NetDll_WSACleanup) { ctx.r3.u32 = 0; }

void Kernel::ReportWaitTraffic()
{
    // Printed once. A thread that blocks calls its wait exactly once, so
    // counting repeats would miss precisely the case that matters.
    static bool reported = false;
    if (reported) return;

    std::lock_guard<std::mutex> lock(g_trafficMutex);
    if (g_waits.empty()) return;
    reported = true;

    printf("\n");
    printf("the last %zu synchronisation events, oldest first:\n", g_events.size());
    for (size_t i = 0; i < g_events.size(); i++)
    {
        const Event& event = g_events[(g_eventCursor + i) % g_events.size()];
        printf("  thread %-5u %s 0x%08X   from 0x%08X\n",
            event.thread, event.action, event.object, event.from);
    }

    // The worker pool's own globals. sub_82129778 only wakes the workers when
    // the word at 0x8290E928 is non zero, so its value decides whether the
    // event the workers wait on is ever set at all.
    printf("\n");
    printf("worker pool globals:\n");
    static const struct { uint32_t address; const char* what; } globals[] = {
        { 0x8290E920, "lock handle" },
        { 0x8290E928, "wake gate" },
        { 0x8290E930, "job array" },
        { 0x8290E934, "job count" },
        { 0x8290E938, "queued" },
        { 0x8290E98C, "worker wake event" },
        { 0x8290F900, "main thread lock" },
        { 0x8290F934, "lock depth" },
    };
    for (const auto& global : globals)
        printf("  0x%08X %-16s = 0x%08X\n",
            global.address, global.what,
            Guest::Read32(Guest::Base, global.address));

    // The title keeps its own copy of the ring pointers in a structure whose
    // address is not known from outside. It is findable: the structure holds
    // the address the GPU publishes the read pointer to, so searching guest
    // memory for that address locates the structure, and the fields around it
    // say what the title actually believes about the ring.
    printf("\n");
    printf("the title's own view of the ring:\n");
    {
        const uint32_t context = Gpu::InterruptContext();
        if (context == 0)
        {
            printf("  no interrupt context yet\n");
        }
        else
        {
            // The exact meaning of these fields is not documented, so the
            // block around the two the spin loop reads is dumped whole and the
            // pattern is read off it.
            printf("  context 0x%08X, fields 0x2A00 to 0x2A28:\n", context);
            for (uint32_t offset = 0x2A00; offset <= 0x2A28; offset += 4)
            {
                const uint32_t value = Guest::Read32(Guest::Base, context + offset);
                printf("    +0x%04X = 0x%08X (%u)%s\n", offset, value, value,
                    offset == 0x2A10 ? "   read pointer location" :
                    offset == 0x2A1C ? "   the value the spin compares against" : "");
            }

            const uint32_t location = Guest::Read32(Guest::Base, context + 0x2A10);
            if (location != 0)
                printf("  read pointer there = %u\n",
                    Guest::Read32(Guest::Base, location));
            printf("  write pointer register = %u\n", Gpu::WritePointer());

            // The ring address the structure holds is a physical address, and
            // physical memory is reachable through several windows. Which one
            // actually holds the command data has to be looked at rather than
            // assumed, because walking the wrong one turns noise into packets.
            const uint32_t ringAddress = Guest::Read32(Guest::Base, context + 0x2A04);
            if (ringAddress != 0)
            {
                printf("  ring address 0x%08X, first words at each alias:\n",
                    ringAddress);
                const uint32_t aliases[] = {
                    ringAddress & 0x1FFFFFFF,
                    0xA0000000u | (ringAddress & 0x1FFFFFFF),
                    0xC0000000u | (ringAddress & 0x1FFFFFFF),
                    0xE0000000u | (ringAddress & 0x1FFFFFFF),
                };
                for (uint32_t alias : aliases)
                {
                    printf("    0x%08X:", alias);
                    for (int i = 0; i < 6; i++)
                        printf(" %08X", Guest::Read32(Guest::Base, alias + i * 4));
                    printf("\n");
                }
            }
        }
    }

    // Who holds each mutant. A thread blocked on one is waiting for whoever
    // owns it, so this turns "blocked on 0x108" into a thread to go and look at.
    printf("\n");
    printf("mutant ownership:\n");
    {
        std::lock_guard<std::mutex> objects(Kernel::DispatcherLock());
        for (const auto& entry : Handles())
        {
            if (entry.second.type != ObjectType::Mutant) continue;
            if (entry.second.owner == 0)
                printf("  0x%08X free\n", entry.first);
            else
                printf("  0x%08X held by thread %u, depth %d\n",
                    entry.first, entry.second.owner, entry.second.recursion);
        }
    }

    printf("\n");
    printf("what each thread is blocked on right now:\n");
    if (g_blocked.empty()) printf("  nothing is blocked\n");
    for (const auto& entry : g_blocked)
    {
        printf("  thread %-5u on 0x%08X from 0x%08X, %s",
            entry.first, entry.second.object, entry.second.from,
            entry.second.infinite ? "no timeout" : "with a timeout");

        // A thread handle says nothing on its own. Which thread, and where it
        // started, is what makes a stalled wait readable.
        const std::string who = Kernel::DescribeThreadHandle(entry.second.object);
        if (!who.empty()) printf("   waiting for a thread: %s", who.c_str());
        printf("\n");
    }

    // And what each of them was doing, since a thread that appears to be
    // waiting on a lock it holds is either a report out of date or a wait
    // that took a path this runtime does not expect.
    Kernel::ReportRecentCalls();

    printf("\n");
    printf("synchronisation traffic so far:\n");
    for (const auto& entry : g_waits)
    {
        const auto signalled = g_signals.find(entry.first);
        printf("  object 0x%08X  waited %llu, signalled %llu%s\n",
            entry.first,
            (unsigned long long)entry.second,
            signalled == g_signals.end() ? 0ull : (unsigned long long)signalled->second,
            signalled == g_signals.end() ? "   <- nothing ever signals this" : "");
    }
    for (const auto& entry : g_signals)
    {
        if (g_waits.find(entry.first) != g_waits.end()) continue;
        printf("  object 0x%08X  signalled %llu, never waited on\n",
            entry.first, (unsigned long long)entry.second);
    }
    fflush(stdout);
}

bool Kernel::SignalHandle(uint32_t handle)
{
    if (handle == 0) return false;

    std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
    struct Wake { ~Wake() { Kernel::DispatcherChanged().notify_all(); } } wake;

    Object* object = Find(handle);
    if (object == nullptr) return false;
    SetSignalState(*object, 1);
    return true;
}

void Kernel::AbandonMutants(uint32_t osThreadId)
{
    // A thread that ends while holding a mutant.
    //
    // The console frees them: the kernel hands the next waiter the lock and
    // tells it the previous owner died. Leaving them held instead means every
    // thread behind that lock waits for a thread that no longer exists, which
    // is a deadlock that only shows up as the title going quiet. One run in
    // three stopped this way, always with a mutant still owned by a thread that
    // was no longer blocked on anything.
    if (osThreadId == 0) return;

    int released = 0;
    {
        std::lock_guard<std::mutex> lock(Kernel::DispatcherLock());
        for (auto& entry : Handles())
        {
            Object& object = entry.second;
            if (object.type != ObjectType::Mutant) continue;
            if (object.owner != osThreadId) continue;

            object.owner = 0;
            object.recursion = 0;
            SetSignalState(object, 1);
            released++;
        }
    }

    if (released > 0)
    {
        printf("thread: %d mutant%s released because the thread holding "
               "%s ended\n",
            released, released == 1 ? "" : "s", released == 1 ? "it" : "them");
        fflush(stdout);
        Kernel::DispatcherChanged().notify_all();
    }
}
