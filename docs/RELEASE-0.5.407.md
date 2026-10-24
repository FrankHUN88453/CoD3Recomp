# CoD3 PC Port 0.5.407 (source only)

Performance release for Marseille / big 32-player maps: a second drawing thread for heavy frames, pooled script fibers, a native memcpy, faster box tree walks and register writes, plus guards for the effects crash and a heap freeze watch.

On Marseille's city views the same camera spots went from about 63 fps (0.5.396) to about 69 fps (71+ with profilers off), and the main thread from 15.4 to 14.2 ms a frame. Rimling 32 players: median 45 fps (0.5.369) to about 65.

**This release contains source code only.** No game files, executables, recompiled code, textures or shaders are included. Build it with your own legally obtained copy of Call of Duty 3 (Xbox 360). Based on [FrankHUN88453/CoD3Recomp](https://github.com/FrankHUN88453/CoD3Recomp).

Requires a CPU with SSE4.1 (x86-64-v2); no AVX2 needed.

## Changes 0.5.391 – 0.5.407

### 0.5.407: a watch on a heap freeze

- 0.5.406's Rimling run froze 30 s into the match (a first in all of
  today's runs): the main thread in sub_82185D18, a cache that asks the
  game's heap (sub_820DE188, through sub_820DE7B8) for a block and, when
  refused, lets go of its own oldest entry (sub_82185B40) and asks again,
  for ever. Its own list was already empty, so nothing was let go and the
  heap stayed full: the game spun at full speed with no frame drawn.
- The first time this happens the request and the cache are now logged
  ("heapwait:"), and the loop sleeps a millisecond a turn from then on, so
  the threads that hold the heap's memory get to give it back (and the
  game does not burn a core while it waits). Why the heap is full is the
  next question; the log line says which cache and how much it wanted.

### 0.5.406: the depth copies four texels at a time

- 0.5.405: clean. The drawing thread used from 3000 draws instead of
  4000 (A/B in one run): lighter command thread at 3700 draws but 56.1
  against 58.1 fps, so it stays at 4000. Rimling at 32 players, against
  0.5.369's run: median 65 fps (was 45), slowest tenth 56 (was 41), the
  server frame 6.1 ms (was 9.1). No crash.
- On Rimling the command thread's biggest piece of its own work is the
  depth copies written back into the title's memory (ConvertDepthRows,
  4.9% there, and all the helper threads with it). The float depth
  (20e4) is now encoded four texels at a time; any four with a texel at
  or below zero or below 20e4's normal range go one by one as before.

### 0.5.405: the written-page check in one read

- 0.5.404 measured: no change from the box tree walk's early reads (the
  same views as 0.5.402, within a fraction of a frame), and no crash. Run
  with no profiling at all (BENCH-MARSEILLE-CITY-PLAIN.bat, the frame rate
  as played): 71.5 / 70.4 / 75.8 / 65.5 fps on the views 0.5.403 measured
  at 68.8 / 69.1 / 68.4 / 62.7 with the profilers on; the main thread
  13.4 ms a frame.
- The game is still built for any x86-64 processor with SSE4.2 (2008 on):
  Justin's friends may have older PCs, so no AVX2 build.
- Whether a buffer's or texture's pages were written since it was last
  looked at (WriteWatch::Clean, several times a draw on the command
  thread) took two reads a page from two tables; it is now one word a page.

### 0.5.404: the box tree walk asks for its nodes early

- 0.5.403's run: clean (no crash, no bad call, the effects guard never
  needed). The same four views: 68.8 / 69.1 / 68.4 / 62.7 fps, the main
  thread 14.2 ms a frame.
- The box tree walk (NativeCull::Walk, still the main thread's busiest
  function at 3-5%) waits on memory for each node it pops; it now asks
  for a node's four children (64 bytes) as soon as it pushes them.

### 0.5.403: a guard for the effects crash

- The job worker's bad call that 0.5.397 and 0.5.398 crashed on is not
  the fiber pool's: Codex's 0.5.384 "index off" crash, before the pool
  existed, is the same chain (sub_824472C0 -> sub_820FC850, an effect's
  emitters), and it is the long unexplained "effects crash". 0.5.402 ran
  clean (no crash, no bad call); the pool stays, with its stacks cleared.
- Where the bad pointer comes from is not known yet. Until it is, the
  effect update checks its emitters first: an emitter that is not a heap
  address is left out, and an emitter's attached object (+200) that is
  not a plausible object (an aligned heap address whose first word is a
  vtable in the image) is treated as none, before the update calls
  through it. The first dozen are logged ("fxguard:") with what surrounds
  them, to find the cause. COD3_FXGUARD=0: off.

### 0.5.402: a fiber used again starts as a fresh one

- 0.5.401 (calls through pointers inline, constant words four at a time),
  the same four views again: 73.4 / 67.1 / 69.6 / 61.4 fps, the main
  thread 14.0 ms a frame. No crash, but the job worker's bad call that
  0.5.397 and 0.5.398 crashed on came back twice (a vtable read where an
  object was expected), so the fiber pool still differs from fresh fibers
  in some way the title notices.
- The one difference left: a new fiber's stack is all zeros, a used one
  holds the last script thread's frames. Something reading stack memory it
  never wrote reads zero on a fresh stack and stale values on a used one.
  The used part of a pooled fiber's stack is now cleared before it runs the
  next script thread. COD3_FIBERCLEAR=0: not cleared (to compare);
  COD3_FIBERPOOL=0: no pool at all.

### 0.5.401: calls through pointers inline, constant words four at a time

- 0.5.400 (native memcpy), the same four views as 0.5.397's clean run:
  69.2 / 67.5 / 68.0 / 62.5 fps against 64.3 / 64.0 / 66.5 / 59.9, the
  main thread 14.2 ms a frame against 14.9 (15.8 with neither the fiber
  pool nor the memcpy). No crash, none of the bad calls.
- Every call the title makes through a function pointer (virtual calls,
  10865 places in its code) went out of line through Mmio::CallIndirect
  and its trace checks: 1.7% of the main thread. A call to a function of
  the title's that is there now goes straight to it, inline; anything
  else (a level's DLL, a bad pointer, or any of the call traces on) still
  goes through CallIndirect as before.
- The command thread's constant packets and register runs (its busiest
  lines, about 7% together) are turned round and stored four words at a
  time with SSE2 instead of one at a time.

### 0.5.400: the title's memcpy native

- 0.5.399 ran the city benchmark twice with no crash and none of the bad
  calls that led up to 0.5.397's and 0.5.398's (the fiber pool's fix
  holds). Heavy views (5000-5700 drawn) 47-54 fps, held by the command
  thread (17-19 ms); lighter ones 56-69.
- The profile by source line: the main thread's 3% "sub_8231E8C0" is the
  title's own memcpy, used by every thread. It is now the host's (same
  bytes, same order; a copy whose destination starts inside its source,
  or one near the device registers, is still the title's own).
  COD3_NATIVE_MEMCPY=0: the title's own always.

### 0.5.399: the fiber pool's crash found

- 0.5.398's runs: with the fiber pool, two of three benchmark runs since
  0.5.397 crashed near the end in a job worker (a call through a bad
  pointer, sub_824472C0 -> sub_820FC850 -> sub_82110FD0), never seen in
  any run before; with the pool off (COD3_FIBERPOOL=0) the run was clean
  and that bad call never happened at all. Same views as 0.5.397's clean
  run: pool off 61.2 / 61.7 / 61.0 / 56.5 fps, main thread 15.8 ms;
  pool on 64.3 / 64.0 / 66.5 / 59.9, 14.9 ms.
- The cause: each fiber has its own SSE control word (rounding, flush to
  zero), but the title's code keeps one copy of it in the shared context
  and only sets the processor's when that copy says it changed. A fiber
  used again started with whatever its last script thread left, so a
  script thread could run with the wrong rounding or flush mode and
  compute wrong numbers (indices, handles) that a job later used.
- The control word now goes with every switch into and out of a script
  thread's fiber, both ways.

### 0.5.398: the main thread measured, the box tree walk quicker

- 0.5.397 measured: the same four views as 0.5.396 (same draw counts) at
  64.3 / 64.0 / 66.5 / 59.9 fps against 62.9 / 63.7 / 63.8 / 57.5, the
  main thread 14.9 ms a frame against 15.4: the fiber pool took the
  NtFreeVirtualMemory share off it entirely. One of 0.5.397's two runs
  crashed after 130 s on a call through a bad pointer in a job worker
  (never seen before); the second ran clean. Watched.
- The "perf: a game frame" line also gives the main thread's CPU time a
  frame, so its waiting can be told from its work.
- The box tree walk (NativeCull, 5.6% of the main thread) tests a node's
  six bounds with one SSE2 compare instead of six reads and compares
  (still checked against the game's own walk for its first 3000 queries).

### 0.5.397: script thread fibers kept and used again

- 0.5.396's profiler, now able to say who called: the main thread's 3.7%
  in NtFreeVirtualMemory (and 0.6% in ZwAllocateVirtualMemory) is
  Coroutines::Destroy and Begin: every one of the level's script threads
  got a new host fiber (a 4 MB stack reserved from Windows) and handed it
  back when it ended, many times a frame with 32 players.
- A fiber whose script thread ran to its end now goes into a pool (up to
  64) and runs the next script thread; one whose thread is still inside
  a yield is deleted as before. COD3_FIBERPOOL=0: the old way. The
  COD3_TRACECOROUTINES line says how many fibers were made and reused.

### 0.5.396: the profiler says who called

- With the drawing thread done, the game's main thread is the other half
  of Marseille's limit (about 19 ms a frame in heavy views). Its sampled
  profile in those views has 4.3% in NtFreeVirtualMemory (memory handed
  back to Windows every frame) and 0.7% in ZwAllocateVirtualMemory, but
  could not say from where: since 0.5.371 it records only the function a
  sample fell in, because walking a running thread's stack froze the game.
- The profiler now walks the stack while the thread is stopped, by the
  modules' own unwind tables read once (no system lock the stopped thread
  could hold) and never outside the stopped thread's stack, and names, for
  a system function, the first caller back in the game's executable.
  COD3_HOSTPROFILE_SELF=1 is the old "where it was" only, =0 the old walk.
- BENCH-MARSEILLE-CITY.bat: the benchmark from four points high over the
  middle of the city (3000-5200 drawn whatever way the camera faces).

### 0.5.395: the drawing thread only for heavy frames

- Across the A/B runs of 0.5.392-0.5.394 the drawing thread made the
  command thread lighter (7-13% a draw at 4000-5200 drawn, less in
  lighter views), but in views where the game's main thread held the
  frame rate the frame rate was often a little lower with it on (about
  1.5 fps on average): its core is one the game's own threads could use.
- It is now used only in heavy frames, where the drawing is what holds
  the frame: from 4000 draws a frame, off again under 3000
  (COD3_DRAWTHREAD_MIN=draws; 0 = every frame; COD3_DRAWTHREAD=0 = never).
  The A/B benchmark compares that against off.

### 0.5.394: two small costs off the command thread

- 0.5.393's A/B: in those views (2400-4300 drawn) the game's main
  thread, at about 17 ms a frame, held the frame rate as much as the
  drawing; the command thread was 2-4% lighter with the drawing thread.
  Its sampled profile showed two costs that are not drawing work: the
  system clock read twice a draw for the counters (2.7%), and system
  calls waking the drawing thread when it had fallen asleep between
  batches (about 2-4%).
- The counters' timing now uses the processor's time stamp counter
  (measured against the clock once a frame), and the drawing thread
  waits about a third of a millisecond, by that counter, before it
  sleeps, whatever a pause instruction costs on the processor.

### 0.5.393: a cheaper hand over to the drawing thread

- 0.5.392's A/B over the middle of Marseille (4000-5200 things drawn):
  with the drawing thread 51.4 / 53.7 / 50.6 fps, without 47.4 / 53.6 /
  54.7, the command thread 7-13% lighter a draw. The drawing thread is
  busy only about 30% of the time and keeps a draw or two behind, yet the
  command thread saved less than half of what it handed over: writing
  the queue cost it nearly as much as the calls it gave away.
- The queue is now 4 MB instead of 64 MB, so it stays in the
  processor's cache, and it is handed over every 8 draws instead of
  every draw (or at once when anything has to wait for it).
- A ring write too big for the queue (over 512 KB) is mapped on the
  command thread after the drawing thread catches up.

### 0.5.392: measuring the drawing thread

- 0.5.391 ran the whole Marseille benchmark with no crash and no
  graphical fault, but the command thread's time per draw did not drop
  (3.38 us a draw against 3.31 before). This build measures why: a
  "queue:" line every second (with the render profile) says how often
  and how long the command thread waited for the drawing thread, from
  which place, and how busy the drawing thread really was.
- COD3_DRAWTHREAD_TOGGLE=seconds turns the drawing thread on and off in
  turns in one run (logged as the benchmark's settings A and B), and
  BENCH-MARSEILLE-AB-DRAWTHREAD.bat runs the benchmark that way.

### 0.5.391: a second drawing thread

- The heaviest views on Marseille (6000+ things drawn a frame) were held
  to about 40 fps by one thread doing everything for the graphics card:
  reading the game's draw commands, working each draw out, and making the
  Direct3D calls. The Direct3D calls (binding, drawing, filling the
  constant and index buffers, small buffer uploads) now run on a thread
  of their own, fed by a queue, while the first thread works out the next
  draw. Everything the queue carries is copied from the game's memory
  when it is queued, so the game sees its draws finish in the same order
  as before.
- Anything else that touches the graphics card (resolves, the present,
  texture uploads, a cache dropping something) first waits for the
  drawing thread to finish what it has, then runs as before.
- COD3_DRAWTHREAD=0 turns it off. With the render profile on, the log has
  a "draw-thread:" line beside "cp-thread:", and the render report says
  how many draws were queued and how often the command thread waited.

