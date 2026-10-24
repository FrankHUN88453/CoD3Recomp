# Codex Marseille validation, 0.5.380 and 0.5.381

## Logs retained locally

`C:\Users\hovgt\Documents\Codex\2026-10-22\you-re-picking-up-my-call\work\cod3-380\`

- baseline-0.5.378.log: copied before the first test; header really says 378.
- test-0.5.380.log: Justin's first normal BIG-PROFILE-32 run.
- audit-0.5.380.log: Justin's PROFILE-MARSEILLE-AUDIT run; extra audit overhead.
- analyze_profile.py: reproduces render medians / draw bins, one log argument.
- gpu.before.cpp and render_resources.before381.cpp: local source backups.

These are local only. Do not upload runtime/game data or these log files.
Source build root has no Git metadata. No remote operations were performed.

## Normal test comparison

Each sample is one `render:` summary with >1000 draws. Busy filter is >4000
(the 4500-5500 subset is inclusive). Each per-draw CPU sample is
`render cpu ms * 1000 / draws`; then take the median. Do not divide medians.
The separate `profile: us a draw` excludes packet processing.

| Metric | 378 baseline | 380 normal |
|---|---:|---:|
| All gameplay render fps median | 53.25 | 53.25 |
| >4000 draws sample count | 49 | 43 |
| >4000 draws fps median | 45.6 | 49.3 |
| >4000 draws median draw count | 4835 | 4676 |
| >4000 draws cpu/draw median (us) | 3.382 | 3.218 |
| >4000 draw-only profile median (us) | 3.05 | 2.91 |
| 4500-5500 sample count | 25 | 20 |
| 4500-5500 fps median | 45.3 | 48.95 |
| 4500-5500 cpu/draw median (us) | 3.357 | 3.170 |
| 5000-5499 fps median | 43.5 | 45.8 |
| 5000-5499 cpu/draw median (us) | 3.316 | 3.158 |

Different matches, no deterministic replay or same-build off-switch A/B yet.
This is promising evidence, not a claim of fixed-workload causality.
ExecuteBuffer median active sampling self share: 13.2% -> 10.6%, including
only CP sampling windows with ExecuteBuffer self >=5% (32 vs 30 windows).
Native validation completed: inverse 2000, cull 3000, slerp 5000, all matched.
No fatal/native-failure markers found. The spare-slots diagnostic warning is
present 32 times in BOTH logs; not introduced by 380. No visual feedback yet.

## Audit findings and next change

380 audit: >4000 draws 64 samples, 46.3 fps, 3.433 us cpu/draw.
Do not compare this directly to normal runs: FPAUDIT collects a map entry and
reads the clock for every dense fingerprint, then sorts/logs every five seconds.
After discarding first six audit windows (loading/menu/start), median of next
30 five-second totals: index small 3.5 MB, vertex small 472.1 MB.
337 printed top-list vertex entries were <4096 bytes (1056..4080 bytes), median
252 looks per window. The totals include buffers not printed in the top 12.

IndexBufferFor ALREADY arms stable index pages (StableLooks=8) and calls Clean
on each look. Simply adding page watching repeats existing behavior. No index
source changes made. A synthetic aligned-key hash distribution check did not
suggest a severe bucket-distribution issue; no key-hash rewrite attempted.

381: RenderResources::VertexBufferFor watchStillBytes default 4 KB ->1 KB.
Rollback: COD3_WATCHSTILL_KB=4. Keeps stable>=8, !midFrame, falseWrites<4.
Build requested only after source/patch notes/other handoff files were saved.
Built OK and installed 23:55:45 (2.7 min). Normal Justin test PENDING; compare against 380 NORMAL, not audit.

## Changes deliberately not made

No whole-buffer pre-swap / packet payload cache: guest command buffers can be
written by commands/indirect execution; immutable snapshots need stronger proof.
No lower-quality sampled index fingerprint or skipping within-frame checks.
No runtime optimization tried and reverted so far; 381 outcome still pending.

## 0.5.381 normal test result (2026-10-23)

Justin TESTED 381: >4000 draws 48.7 fps / 3.224 us cpu per draw vs 380 49.3 / 3.218; no clear busy-view speedup. 4500-5500: 48.6 fps / 3.228 us vs 48.95 / 3.170. Overall gameplay render median 54.15 vs 53.25 fps. All profile summaries median fingerprint KB 943 vs 4155 (includes startup/menu summaries). All native checks matched; no fatal/native-failure markers. Keep lower threshold for less scanning, but do NOT claim busy-view performance improved.

Gameplay-only hashing refinement: pair profile summary with following render
summary and require >1000 draws. 380: median 3833 fingerprints / 4304 KB /
4583 page-watch hits per frame. 381: 2098.5 fingerprints / 974.5 KB / 5033.5
page-watch hits. Earlier 4155 -> 943 KB numbers included menu/startup summaries.

## 0.5.382 normal test result (2026-10-23)

Justin TESTED 382: >4000 draws 46.0 fps / 3.132 us cpu per draw vs 381 48.7 / 3.224, BUT median busy draws rose 4691 -> 5284. In 5000-5499 bin: 46.3 fps both, cpu/draw 3.190 -> 3.087 (7 vs 10 samples). 4500-5500: 46.75 fps / 3.122 us (16 samples, med 5144.5 draws) vs 381 48.6 / 3.228 (28 samples, med 4770.5). Overall render fps 54.4. Different bot matches, no fixed-workload causal claim. All native checks matched; no fatal/native-failure markers. ExecuteBuffer active self-share ~11.35% vs 381 11.3%; no clear share reduction.

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
