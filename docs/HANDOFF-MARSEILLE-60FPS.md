# Handoff log: Marseille (ordnance) toward a true 60 fps at 32 players

Written by Claude, 2026-10-22 23:25 (Toronto). Source and install are at 0.5.379.
Read PICK-ME-UP.md first for the short version; this is the full story.

## Ground rules (Justin's)

- NEVER upload game data to GitHub: ISO, .cod/.xex, generated ppc/ppc_mp, textures, shaders, exes.
- Patch notes for every build (docs/PATCH-NOTES.md, newest on top, "Current source version" line).
- Keep HANDOFF.md / NEXT-SESSION.md / this log current so anyone can pick up.
- Test where possible; give Justin a short status after every test.
- Use Justin's PC only when needed; be efficient.

## Paths and the build loop

- Source: D:\Call of Duty 3 (USA, Europe)\CoD3Recomp-main\CoD3Recomp-main
- Game folder: D:\Call of Duty 3 (USA, Europe)\CoD3-PC-Port-0.4.0
- Clips: C:\Users\hovgt\Videos\NVIDIA\CoD3-PC-Port-0.4.0
- Build: write build-request.txt (one line, CRLF) in the source root. The watcher
  (AUTO-BUILD.ps1) builds (~2.8 min), copies CoD3MP.exe/CoD3.exe to the game
  folder and writes build-status.txt ("built: 0.5.N OK ..." or "failed: ...").
- The version comes from the FIRST "## 0.5.N:" heading in docs/PATCH-NOTES.md
  (BUILD-MULTIPLAYER.ps1 writes version_number.h from it). Commit the source
  and docs FIRST, and only then build-request.txt in a separate step, or the
  build picks up the old notes (happened with 0.5.372: built, labelled 0.5.371).
- Files are mostly CRLF; gpu.cpp is LF. Edit byte-safe.

## The test

- Game folder BIG-PROFILE-32.bat [map] [ENV_SWITCH]: 32 players, 28 bots, 150 s
  of match, closes itself, log saved as big-profile-32-<map>.log. No map = ordnance
  (Marseille). It profiles the main thread and the drawing thread
  (COD3_HOSTPROFILE=main,cp, COD3_RENDER_PROFILE=1).
- PROFILE-MARSEILLE-AUDIT.bat = the same + COD3_FPAUDIT=1 (every 5 s: which
  buffers the drawing thread hashes, by size).
- Justin double-clicks it (Claude's clicks stopped launching bats at 21:53).
- Read in the log:
  - "[ Ns] fps N" lines: one per second. The ~22 lines at 144 are the menu; ignore fps >= 140.
  - "perf: a game frame: bots X, server frame Y, the rest Z": the main thread's ms a frame.
  - "render: F fps, ... (C ms cpu, G ms gpu), D draws": drawing thread per frame.
    "gpu" is a span including idle; it is NOT the card's load (RTX 2070, never the limit).
  - "profile: X us a draw: state .. buffers .. constants ..": the drawing thread's per draw split.
  - "profile of the title's first thread / the command processor, N samples, in the function itself": hot functions.
  - Self-check lines: "inverse:/cull:/slerp: native matched the game's on N of N".
- Bots don't play the same match twice: compare medians AND "views with > 4000 draws".

## Where we started (0.5.371, this morning's state)

Marseille at 32 players: median 43 fps, worst 10% 28. Both the game's main thread
and the drawing thread (the "command processor", which turns the Xbox's GPU commands
into Direct3D 11) took about 23 ms a frame. The graphics card was idle much of
the time. For 60 both must get under 16.6 ms.

## What each build did (all tested unless said)

| Build | Change | Result |
|---|---|---|
| 0.5.370 | getenv cache + buffered log | rimling 32p 43 -> 53 fps (earlier today) |
| 0.5.371 | Profiler samples only the current function (no stack walk) | No more freeze; first Marseille profile |
| 0.5.372 | Native 4x4 matrix inverse (sub_820B6260, mp_fixes.cpp NativeInverse) | Matched 2000/2000; main -2 ms; worst 28 -> 36 |
| 0.5.373 | Type-0 register packets use the fast WriteRegisterRun (gpu.cpp ExecuteBuffer) | Drawing thread cheaper |
| 0.5.374 | Two 128 KB buffers rewritten mid-frame: each draw compares only its vertex range against a kept copy (render_resources.cpp rangeShadow, render_commands.cpp LookRanges) | Median 42 -> 50; data hashed 16.5 -> 4.6 MB/frame |
| 0.5.375 | Native world box-tree search (sub_824193E0, NativeCull) | Matched 3000/3000 |
| 0.5.376 | Native quaternion slerp (sub_820B5CD8, NativeSlerp) | Matched 5000/5000; median 59, main 17.5 ms |
| 0.5.377 | Still models from 4 KB up watched by their memory pages, not hashed (was 32 KB; COD3_WATCHSTILL_KB) | Hashing 10 -> 3.6 MB/frame |
| 0.5.378 | 12 MB dynamic geometry pool: only pieces whose pages were written are compared (COD3_BIGCOMPAREALL=1 to undo) | Busy views 39 -> 45 fps; 4.04 -> 3.39 us a draw; worst 43 |
| 0.5.379 | Steady decals pull capped (DepthBiasClamp 0.00002, COD3_DECALNUDGECLAMP) | Justin: Marseille tracks no longer drawn over crates/gun |

Every native rewrite checks itself against the game's own code for its first
few thousand calls and falls back to the game's own forever on any difference.
Each has an off switch: COD3_NATIVE_INVERSE=0, COD3_NATIVE_CULL=0, COD3_NATIVE_SLERP=0.

## Where it stands now

- Most of a Marseille match: 55-65 fps. Median 53-59 depending on the match.
- Busy views (4500-5400 things drawn): ~45 fps. The DRAWING THREAD is the limit
  there: about 3.4 us per draw. To hold 60 at 5000 draws it needs about 3 us.
- Main thread ~17.5 ms a frame (bots 0.7, server 5.5, client 11.3). It waits on
  the drawing thread in busy views.

## What to do next (in order)

1. Drawing thread, per draw (3.4 -> 3 us). 0.5.378 profile, self time:
   - ExecuteBuffer 16% (gpu.cpp): the PM4 packet loop. Per packet it calls
     Remember() (a debug ring of the last packets: 5 guest reads + 6 stores) and
     reads every word byte-swapped. Ideas: keep Remember only for type 3 or
     behind a switch; read runs with one bulk byte-swap.
   - memcpy 5.8%, Fingerprint 5%, IndexBufferFor 4.7%, VertexBufferFor 4.5%.
     Index buffers are still hashed per look: try the same page watch as
     vertex buffers (the audit showed index buffers ~3 MB per 5 s, small).
   - The D3DKMTOpenResource lines are the NVIDIA driver (unnamed), mostly
     UpdateSubresource/Map.
   - RenderStats::Enter and QueryPerformanceCounter (~4%) are the profiler
     itself; they are not there in normal play.
2. Main thread: next hot spots are the native cull itself (3.9%, could use a
   SIMD node test), sub_8231E8C0 (memcpy in guest code, 1.9%), sub_824A0940
   (1.8%), NtFreeVirtualMemory (2.9%, find who frees memory every frame).
3. Bullet holes still flicker sometimes (long-standing). They live in the 12 MB
   pool and the 128 KB mid-frame buffers. Verify mode (Verify::Buffers) and the
   late-check machinery in render_commands.cpp are the tools.
4. Crosshair: on Justin's class (Garand + pistol) it was missing in one match
   (clip 2026.10.22 22.55.58), then showed again. Cause unknown. Ask what
   happened before it vanished.
5. Older items still open: the killcam respawn text and death tint, the T-pose
   watch (nothing seen lately), the two-PC 32-player test (mortar/UAV
   visibility), binocular sides, clean-up of debug code.

## Things learned the hard way today

- Run the version/docs edit to completion BEFORE committing (a commit sent in
  the same step as the edit sent the old file).
- The profiler's stack walk suspends the main thread while it may hold a lock:
  keep COD3_HOSTPROFILE_SELF on (default).
- Hooks: in mp_fixes.cpp, `extern "C" PPC_FUNC(__imp__sub_X); PPC_FUNC(sub_X) {...}`;
  call the original with __imp__sub_X(ctx, base). Guest indirect calls:
  Guest::Lookup(address)(ctx, base). Vector stores (stvx) align the address down to 16.
- A trick for checking a native function that calls back into the game: give the
  game's own version a stand-in object whose first virtual is the hooked function
  itself, and record the calls (see NativeCull).

## Codex continuation: 0.5.380 (2026-10-22)

- ExecuteBuffer type-0 and type-2 diagnostic Remember calls now conditional
  on COD3_REMEMBERALLPACKETS=1. Type-3 history preserved; no packet payload
  caching or decoding changes. Built OK and installed 23:38:54 (3 min); Justin tested; result below.
- Baseline log preserved before test overwrite: version header is 0.5.378.
- IndexBufferFor already watches stable index pages. Do not simply add a
  second page-watch cache; inspect why entries lose watching / hash instead.
- Build root has no .git, so local backup instead of a source commit. No
  remote operations performed. Detailed handback in NEXT-SESSION.md.

Baseline 0.5.378: 49 render samples >4000 draws; median 45.6 fps,
4835 draws, 16.42 ms cpu, median(cpu_ms*1000/draws) 3.382 us/draw.
4500-5500: 25 samples, 45.3 fps, 3.357 us/draw. Profile draw-only
median 3.05 us/draw excludes packet-loop work; compare both metrics.

### 0.5.380 test result

Justin TESTED 0.5.380: >4000 draws median 45.6 -> 49.3 fps, cp cpu/draw 3.382 -> 3.218 us (49 vs 43 samples); 4500-5500 draws 45.3 -> 48.95 fps, 3.357 -> 3.170 us (25 vs 20). Different matches. Overall render median unchanged 53.25 fps. Native checks all matched (inverse 2000, cull 3000, slerp 5000); no fatal/native-failure markers. Existing spare-slots diagnostic warning occurs 32 times in BOTH logs. CP ExecuteBuffer active profile median self 13.2% -> 10.6% (not 16%: handoff quoted one window).

Requested same-build PROFILE-MARSEILLE-AUDIT to locate remaining index hashes.
IndexBufferFor already watches after eight unchanged looks; no index source
change yet. Do not claim a new page-watch optimization without measuring.

### 0.5.381 preparation: audit redirects the next change

Justin ran PROFILE-MARSEILLE-AUDIT.bat on 0.5.380. Result: index only
~1.5-6 MB per five seconds in late windows; vertex ~330-621 MB. Repeated
model buffers of 3120-4080 bytes are below the current 4 KB threshold.
381 lowers VertexBufferFor watchStillBytes default to 1 KB; existing
COD3_WATCHSTILL_KB=4 restores 380. Other stable/midFrame/falseWrites checks
unchanged. No index-buffer code change, since it already watches pages.
Built OK, installed 23:55:45 (2.7 min); test pending; compare NORMAL test with 380 normal, not with audit.
Audit busy views >4000 draws: 46.3 fps, 3.433 us cpu/draw (64 samples).
Audit logging has extra overhead; do not call this a regression of 380.

## 0.5.381 normal test result (2026-10-23)

Justin TESTED 381: >4000 draws 48.7 fps / 3.224 us cpu per draw vs 380 49.3 / 3.218; no clear busy-view speedup. 4500-5500: 48.6 fps / 3.228 us vs 48.95 / 3.170. Overall gameplay render median 54.15 vs 53.25 fps. All profile summaries median fingerprint KB 943 vs 4155 (includes startup/menu summaries). All native checks matched; no fatal/native-failure markers. Keep lower threshold for less scanning, but do NOT claim busy-view performance improved.

### 0.5.382 (2026-10-23): remaining packet history

gpu.cpp ExecuteBuffer skips diagnostic Remember for type-3 DRAW_INDX,
DRAW_INDX2, SET_CONSTANT, SET_CONSTANT2, SET_SHADER_CONSTANTS, NOP. All
handlers execute as before. Waits/fences/interrupts/indirect and unknown/other
packets retained. COD3_REMEMBERDRAWS=1 restores 381 type-3 history;
COD3_REMEMBERALLPACKETS=1 restores full pre380 history. Built OK, installed 00:04:11 (2.6 min); test pending.

## 0.5.382 normal test result (2026-10-23)

Justin TESTED 382: >4000 draws 46.0 fps / 3.132 us cpu per draw vs 381 48.7 / 3.224, BUT median busy draws rose 4691 -> 5284. In 5000-5499 bin: 46.3 fps both, cpu/draw 3.190 -> 3.087 (7 vs 10 samples). 4500-5500: 46.75 fps / 3.122 us (16 samples, med 5144.5 draws) vs 381 48.6 / 3.228 (28 samples, med 4770.5). Overall render fps 54.4. Different bot matches, no fixed-workload causal claim. All native checks matched; no fatal/native-failure markers. ExecuteBuffer active self-share ~11.35% vs 381 11.3%; no clear share reduction.

### 0.5.383 (2026-10-23): index-key lookup front cache

render_resources.cpp IndexBufferFor: 2048 direct-mapped cached handles;
validate live and all four key words (physical/count/width/reset). Deque never
shrinks/clears; handle reuse remains safe via full-key/live check. Misses go
to existing g_indexKeys.Find. ALL page-watch/content checks unchanged.
Off: COD3_INDEXLOOKUPCACHE=0. Built OK, installed 00:15:55; test pending. Source backup local.
Previous profile: IndexBufferFor ~3.45% CP self + KeyTable<4>::Find ~0.8%;
this experiment targets lookup work, not index data hashes (small).

## Measurement correction (Codex, 2026-10-23)

Important: the inherited handoff and earlier Codex status messages called
render cpu_ms*1000/draws the whole drawing-thread cost. Source inspection
shows this is incorrect: render_stats.cpp computes cpuMilliseconds from
ONLY drawNanoseconds + resolveNanoseconds + presentNanoseconds, measured
inside Render::Draw/Resolve/Present (render_d3d11.cpp). It EXCLUDES normal
packet decoding/register writes outside those scopes. Thus the ~3.38 ->
3.13 us ratios remain valid RENDER-SCOPE comparisons, but cannot directly
measure ExecuteBuffer optimization or prove its causality. Sampling self
shares and matched draw-bin frame times must also be used. The separate
profile draw-only number excludes resolve/present too and uses draws+skips.
No full-thread CPU measurement yet. Do not propagate the older interpretation.
Justin explicitly extended the session beyond the original hour: keep working
on smoothness/stability, while documenting all changes for Claude.

## 0.5.383 normal test result (2026-10-23)

Justin TESTED 383: >4000 draws 47.6 fps, render-scope CPU/draw 3.080 us (35 samples, med 5062 draws); 382 46.0 fps /3.132 us (61 samples, med5284). 4500-5500: 47.6 fps/3.131us vs382 46.75/3.122 (mixed). 5000-5499 fps46.4 vs46.3, scope CPU/draw3.111 vs3.087 (no clear speedup). IndexBufferFor active sampled self median3.45%->2.9%; KeyTable<4>::Find0.8%->0.75%. Overall render fps56.8 vs54.4, different workload. All native checks matched; no fatal/native-failure markers. Keep as provisional; same-build off-switch comparison desirable. Do not call render-scope ratios full CP CPU.

### 0.5.384 (2026-10-23): whole-thread CPU measurement

render_stats.cpp EndFrame: GetThreadTimes once per reporting second, kernel+
user deltas / frame count and draw count. Print cp-thread lines only when
render profiling; off COD3_THREADCPUPROFILE=0. Excludes sleeping/descheduling;
first interval, changed thread and failed query prime baseline. Scope times
remain elapsed wall clocks; do NOT subtract them from OS thread CPU time.
No rendering behavior changed. Built OK, installed 00:24:04; test pending. Need true CP baseline.

## 0.5.384 first full-thread baseline (2026-10-23)

Justin TESTED 384 normal. New TRUE whole-CP baseline: >4000 draws n57, med5534 draws, 42.9fps,21.88ms CPU/frame,3.925us CPU/draw,95.6% core. 4500-4999 n19:19.23ms,4.132us,47.3fps (render line47.7 due rounding/boundary at4999.5); 5000-5499 n5:20.83ms,3.792us,45.7fps. Overall render median56.7fps. Renderer-scope busy CPU/draw3.091us is NOT full CP. Agent now authorized by Justin to launch batch tests while he watches. Same-build 384 COD3_INDEXLOOKUPCACHE=0 comparison launched automatically00:28:03, batch PID11764/game22984; IN PROGRESS.

## 0.5.384 same-build index-off comparison CRASH (2026-10-23)

Agent launched BIG-PROFILE-32.bat with ONLY COD3_INDEXLOOKUPCACHE=0 at
00:28:03. User was running around playing. It crashed at ~110 s game time
(~90 s into the bot match), NOT a completed 150 s run. Archive:
Codex chat work/cod3-380/test-0.5.384-index-off.log (absolute directory in
VALIDATION-0.5.380-381.md). Do not treat its partial fps as a successful test.

First failures: apsGroup allocation asks 0x53C62AD0 bytes (~1.4 GB), then
apsMemory effect pool size128/count8192 empty, then bad indirect calls in
sub_82110FD0 <- sub_820FC850 <- sub_824472C0, finally Guest fault read
0x84C28277 / C0000005. There are 350 apsGroup alloc failures and 368
pool-empty messages. Last periodic counts before escalation were only
196/8192 effects, 297/4096 nodes (peaks395/815), so don't blindly enlarge
pools. First bad bytes is product of group count(r4) and config+4 from
sub_82109310 (copied to group+120, count stored+156). No input values yet.
Its caller sub_821106B8 frees the group/returns0 on constructor failure;
parent sub_820FC850 handles null group. Why inputs corrupt is OPEN.

Existing mp_fixes.cpp 0.5.205 comment describes the same particle drawing
call chain after pool failures. 0.5.205 enlarged group/effect/node pools;
0.5.242..244 enlarged general memory pools. They are active in this log:
4MB group,8192 effects,4096 nodes,general pools1024 blocks each. No proof
that the current crash is identical to the old one or caused by new work.

Earlier broad 'no fatal markers' checks were too narrow to catch bad-call
warnings; all saved successful runs including 378 already have 53-68
'not a function' diagnostics (mostly ordinals/nulls), but NO Guest fault or
unhandled C0000005. The index-off run has 112, including corrupt objects.
The distinction matters: don't call every old warning a new regression.

Control run launched 00:33-ish (check process/log timestamp), batch27392:
same384 with COD3_REMEMBERALLPACKETS=1, COD3_WATCHSTILL_KB=4,
COD3_INDEXLOOKUPCACHE=0; restores pre380 performance behavior. IN PROGRESS.
No performance source edits made; proposed constant-shadow-copy reduction
NOT implemented. Pause performance changes until crash investigation.

## FINAL CODEX HANDBACK (2026-10-23, 0.5.384)

See docs/CODEX-HANDOFF-0.5.384.md FIRST. Source/install384, next385; no385 edits.
Final legacy-control test completed00:36:40, no particle failures/Guest fault;
index-off run before it DID crash. Root cause unresolved, cannot exclude regression.
All nine logs, three modified source snapshots, backups/diffs/parsers/reproduction
launchers preserved outside Git in D:\Call of Duty 3 (USA, Europe)\CoD3-local-handoff-2026-10-23-Codex-384.
No more tests/builds scheduled. Constant-shadow optimization and particle diagnostic
NOT implemented. User authorized agent-launched tests; stopped for low account usage.
