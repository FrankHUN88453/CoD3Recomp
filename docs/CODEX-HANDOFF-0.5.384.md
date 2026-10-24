# CoD3 Codex handoff: final source/install 0.5.384

Finished October 23, 2026, Toronto, after the legacy-control test completed at 00:36:40.
Justin asked to stop new work because account usage was low and prepare a complete handoff.
NO 0.5.385 build or source change exists. Next build number is 0.5.385.

## Read this first

- Source and installed game are BOTH 0.5.384, built OK. No build request pending.
- Normal tests of 380,381,382,383,384 all reached `big test: done` and passed native checks.
- One additional 384 run with ONLY index lookup cache disabled CRASHED. Crash unresolved.
- A final 384 control restoring pre380 performance behavior completed without allocation failures/crash.
- Do NOT declare 60 fps achieved or stability established. Busy views still saturate the CP.
- Do NOT claim the failed cache-off comparison proves the cache is good/bad. Different matches.
- Justin authorized the agent to LAUNCH batch tests while he watches/plays; no need to ask again.
  The batch runs bots but DOES NOT tour the camera. Similar views/routes improve comparisons.
- No remote operations/uploads/commits. Build root has no .git; backups made locally instead.
- NEVER upload game data, generated PPC, binaries, shaders/textures, or the local log archive.

## Paths and preserved evidence

Source: `D:\Call of Duty 3 (USA, Europe)\CoD3Recomp-main\CoD3Recomp-main`
Game: `D:\Call of Duty 3 (USA, Europe)\CoD3-PC-Port-0.4.0`
Local-only archive (outside source/Git trees):
`D:\Call of Duty 3 (USA, Europe)\CoD3-local-handoff-2026-10-23-Codex-384`

Archive contains nine complete/partial logs, before/final handwritten source,
patch diffs, both analysis scripts, three reproduction batch launchers, and SHA256 manifest.
No ISO/XEX/COD/generated PPC/shaders/textures/EXE/PDB copied to archive.
Keep archive LOCAL: runtime logs include game memory diagnostics.
Original work copies also remain at:
`C:\Users\hovgt\Documents\Codex\2026-10-22\you-re-picking-up-my-call\work\cod3-380`

Logs in archive/logs:
- baseline-0.5.378.log: actual baseline header is 378, though install was 379 at session start.
- test-0.5.380.log: normal; audit-0.5.380.log: fingerprint audit (extra overhead).
- test-0.5.381.log, test-0.5.382.log, test-0.5.383.log: normal completed runs.
- test-0.5.384.log: normal completed, first true full-thread CPU baseline.
- test-0.5.384-index-off.log: INCOMPLETE/CRASH; only COD3_INDEXLOOKUPCACHE=0 changed.
- test-0.5.384-legacy-control.log: completed, no effect pool failures or access violation.

## Actual source changes and switches

| Build | File/function | Final behavior | Restore prior behavior |
|---|---|---|---|
| 380 | CoD3Host/gpu.cpp ExecuteBuffer | Skip diagnostic Remember for type0 register/type2 padding packets | COD3_REMEMBERALLPACKETS=1 |
| 381 | CoD3Host/render_resources.cpp VertexBufferFor | Stable-model watch threshold 4 KB ->1 KB; stable/midFrame/falseWrites safeguards unchanged | COD3_WATCHSTILL_KB=4 |
| 382 | CoD3Host/gpu.cpp ExecuteBuffer | Also skip diagnostic copies for DRAW_INDX/DRAW_INDX2/SET_CONSTANT/SET_CONSTANT2/SET_SHADER_CONSTANTS/NOP; other type3 history retained | COD3_REMEMBERDRAWS=1; ALLPACKETS=1 restores pre380 entirely |
| 383 | CoD3Host/render_resources.cpp IndexBufferFor | 2048-slot direct mapped HANDLE front cache; full live four-word key validated; page/content checks unchanged | COD3_INDEXLOOKUPCACHE=0 |
| 384 | CoD3Host/render_stats.cpp EndFrame | GetThreadTimes kernel+user delta once/reporting second, ONLY in render-profile runs; cp-thread log lines | COD3_THREADCPUPROFILE=0 |

All five versions built OK, notes/source saved BEFORE a separate build-request write.
Build durations ~2.6-3 minutes. Current source version/first patch heading: 0.5.384.
The source-after snapshots and unified diffs in archive/analysis show exact changes.
No other host source was modified. No generated PPC was manually edited.

## Critical measurement correction

The original handoff and early Codex updates MISLABELLED `render cpu_ms*1000/draws`
as total command-thread cost. Source shows cpuMilliseconds equals ONLY elapsed
Draw + Resolve + Present scope times (render_stats.cpp, render_d3d11.cpp).
It EXCLUDES ordinary packet-loop work. These old ratios are renderer-scope ratios.
The separate `profile: us a draw` excludes resolve/present too, divides draws+skips.
The new `cp-thread:` OS CPU time includes the ENTIRE calling thread, including
packet processing/driver CPU, and excludes sleeping/descheduling. Do not subtract
elapsed render scopes from CPU time. First report primes baseline; OS time is quantized.
Full-thread measurements exist ONLY from build384 onward.

`analysis/analyze_profile.py` keeps historical JSON key `cp_us`; that NAME IS MISLEADING:
it calculates render-scope cpu_ms*1000/draws, NOT full CP. `analyze_thread_cpu.py`
parses the actual cp-thread lines. Both take one log path argument.
Use >4000 draws and comparable bins, not just whole-match fps; matches/camera differ.

## Results, with limitations

| Normal run | All gameplay median fps | >4000 fps | >4000 render-scope us/draw | busy median draws |
|---|---:|---:|---:|---:|
| 378 baseline | 53.25 |45.6|3.382|4835|
| 380 |53.25|49.3|3.218|4676|
| 381 |54.15|48.7|3.224|4691|
| 382 |54.4|46.0|3.132|5284|
| 383 |56.8|47.6|3.080|5062|
| 384 |56.7|42.9|3.091|5534|

These are different workloads, not a fixed-workload causal proof or regression ranking.
380 ExecuteBuffer active sampled median self13.2% ->10.6% vs378. 382 vs381 self
share11.35 vs11.3%, so no clear further share reduction despite per-draw changes.
383 IndexBufferFor sampled self3.45% ->2.9%, but 5000-5499 fps46.3 ->46.4,
render-scope us3.087 ->3.111: gain is PROVISIONAL, no clear matched-bin speedup.

381 audit-driven threshold: dense index hashes median3.5MB/5s vs vertex472.1MB/5s
(after first6 windows). 337 printed top-list vertex entries were1056..4080 bytes,
median252 looks/window. IndexBufferFor ALREADY watches pages after8 stable looks!
Lower threshold cut gameplay median hashes4304 ->974.5KB/frame, but did NOT clearly
improve busiest-view fps. Keep that lack of improvement documented.

384 NORMAL true CP baseline (>4000 draws, n57): 21.88ms CPU/frame,3.925us/draw,
95.6% one core,42.9fps,median5534 draws. 5000-5499 n5:20.83ms,3.792us,45.7fps.
Legacy control (>4000,n115):21.06ms,4.191us,95% core,45fps,median5022 draws.
Control5000-5499 n91:21.4ms,4.207us,44.8fps. Different workload; no causal proof.
The CP is saturated in heavy views; true cost is closer to4us, NOT the old3.1 estimate.
Native inverse2000/cull3000/slerp5000 checks completed and matched in successful runs.

## UNRESOLVED crash: next priority

384 index-cache-off run launched by agent00:28:03; user says he was running around playing.
At ~110s game time / ~90s bot match it escalated into corruption and ended early.
First error `apsGroup : alloc failed` requests0x53C62AD0 bytes (~1.4GB).
Then `apsMemory : pool(size=128,count=8192) is empty` and `apsEffect : pool is empty`.
350 group-allocation failures;368 pool-empty messages. Bad indirect calls follow in
sub_82110FD0 <- sub_820FC850 <- sub_824472C0. Final Guest fault read0x84C28277,
unhandled C0000005, demand handler had committed2048MB while following garbage.
Last periodic counts before escalation were196/8192 effects,297/4096 nodes
(peaks395/815). Simply increasing pools is NOT justified by this evidence.

mp_fixes.cpp around11107 has a 0.5.205 comment describing the SAME drawing call
chain after effect exhaustion. 205 enlarged groups/effects/nodes;242..244 enlarged
general pools. All active here:4MB groups,8192 effects,4096 nodes,general1024 blocks.
This is a clue, NOT proof the current cause is identical or unrelated to our changes.
All older logs already have53-68 non-function warnings (ordinals/nulls etc), including378;
none has a Guest fault/C0000005. Crash run has112 and corrupt object data.
Do not treat all preexisting warnings as newly introduced failures.

Static trace (READ generated code locally; DO NOT upload/hand-edit it):
- sub_82109310 (ppc_recomp.10.cpp ~2101): constructor copies40 bytes from r6 to
  group+116, stores r4 atgroup+156, multipliesgroup+120 bygroup+156 into requested
  bytes at821093D8-ish; allocator failure logged from82109424. Need actual count,
  stride, config pointer and source definition for first bad request.
- sub_821106B8 (~19720): allocates224-byte group node, calls constructor; on failure
  destroys/frees node and returns0. DO NOT assume this failure path lacks cleanup.
- sub_820FC850 (ppc_recomp.9.cpp ~8843): group entries are72 bytes at
  effect->definition(+72)->group-table(+80). Entry+0=count,+24=parameter,
  +28=particle callback-list descriptor,+32=config. Calls group constructor and
  handles null. Later passesentry+28 asr3 to sub_82110FD0, group instance asr4.
- sub_82110FD0 (ppc_recomp.11.cpp ~852) walks descriptor->array at+0 and signed
  16-bit count at+6, calls through objects. A corrupted count/table can walk far
  out of bounds. Do not add arbitrary caps or silently skip before understanding.

Final control:384 with COD3_REMEMBERALLPACKETS=1,COD3_WATCHSTILL_KB=4,
COD3_INDEXLOOKUPCACHE=0 launched00:33:34, completed00:36:40. Reached explicit
big-test done;0 group failures,0 pool-empty errors,0 Guest faults/C0000005.
ONE passing control cannot rule out a regression in380..383; behavior is timing/
workload dependent. Off variables were CHILD PROCESS ONLY, not persisted settings.
Installed384 still has new defaults unless caller sets switches. No rollback build made.

## What to do next, and what was NOT implemented

1. Reproduce/trace particle crash FIRST. Add narrowly scoped bounded diagnostics
   for impossible group count*stride/config source before considering a guard.
   Suggested hook:sub_82109310 or821106B8. Off switch required. Log only anomalous
   inputs, limit output; use safe reads for extra diagnostic dereferences. Check
   upstream effect definition lifetimes/list counts. Do not just enlarge pools.
2. Isolate one switch at a time / repeat controls. Index-off test was incomplete,
   so cache benefit remains unproven. Local reproduce/*.bat files configure runs
   explicitly; keep logs separately after EACH run before next overwrite.
3. Once stable, investigate unnecessary g_sentFile copies in render_commands.cpp
   Constants: used by RecordChanged/native-record path, copied even ordinary draws.
   NOT IMPLEMENTED. A safe optimization must retain/invalidate per-stage shadow
   validity and force an upload on transition to recorded draws; native mode can
   mix recorded/fallback draws. g_packedShadow/Verify::Constants is separate.
4. NativeCull::Walk sampled ~4.5% main; possible signed16 SIMD node rejection,
   but NOT IMPLEMENTED/benchmarked. Preserve game's child order and self-check.
5. Whole-buffer packet preswap/caching NOT attempted: command buffers may be
   written during execution; immutable snapshots need proof.

No optimization was reverted this session. Some gains are uncertain as noted.
No particle diagnostic/fix or constant-copy optimization has been committed to source.
No further test/build/automation is scheduled; game is closed. Resume from384, next385.

## Operational rules carried forward

Every build: save source/docs/new FIRST `## 0.5.N:` patch heading and Current source
version FIRST; only afterward write one CRLF line to build-request.txt in a separate
step. AUTO-BUILD.ps1 builds/copies both EXEs, ~3min; inspect build-status.txt.
Update NEXT-SESSION.md with top `# 0.5.N:` for every build, HANDOFF.md source/install,
SHARED-LOG.md and main Marseille handoff. Normal test BIG-PROFILE-32.bat runs32-player
cap,28 bots,150s match, auto closes. User may move; test does not camera-tour.
Agent can launch via hidden cmd helper with Start-Process -WindowStyle Hidden,
working directory game folder; game creates its own visible window. Do not launch
another game/build while one runs. Report short status after every test.
