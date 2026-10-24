# Pick me up (read this first)

Last updated by Claude, 2026-10-24 01:25. Install/source 0.5.424: pshufb byte swaps (WriteRegisterRunFrom 6.5% -> 4.4% of cp) + native rotate sub_82147008. SMALL NATIVE PIECES ARE AT DIMINISHING RETURNS: read 'Where it stands' at the end of docs/CLAUDE-SESSION-2026-10-23.md. 0.5.423: native table search sub_820B64D0 (3000/3000; 1.1% -> 0.02% of main). 0.5.422: native packed-vector unpack sub_82181520 (3000/3000; 1.65% -> 0.60% of main). 0.5.421: native box sweep sub_8248BB48 (5000/5000; 1.1% -> 0.09% of main). 0.5.420: native segment-box test sub_82490A30 (5000/5000; 0.9% -> under 0.1% of main). 0.5.419: native trace tree walk (record-and-replay check, 3000/3000), walk 2.3% -> 1.6%. 0.5.418 fixed the freeze when a killcam ends (both host and friend hit it in 416); not yet proven in a 2-player game: look for "killcam guard: ... killcam not playing". Friend's 14 fps in 416 was their GPU slowing 7x for ~55 s (game's CPU work steady 3-5 ms); cause outside the log. Next main-thread targets (0.5.421 profile): sub_82164160 1.7% (a loop of virtual calls: little to gain), sub_8241B2A8 0.9%, sub_82167E28 0.8%, sub_82147008 0.75%, leaf tests sub_82499F58 / sub_8249DDC8, sub_8248C708 (0.6%, VMX flush switches); Killcam::Record memcpy 0.8%. Table: docs/CLAUDE-SESSION-2026-10-23.md. Read newest NEXT-SESSION STATE.

**Goal:** a steady 60 fps on Marseille (map "ordnance") at 32 players.

**Where we are (0.5.407):** Marseille city views (BENCH-MARSEILLE-CITY.bat)
68-69 fps with profilers on, 70-76 without (were 63-64 at 0.5.396); main
thread 14.2 ms a frame (13.4 without profilers). Heavy views (5000+ draws)
~47-57 fps. Rimling 32p median 45 (0.5.369) -> 65 (0.5.405).

**What changed 0.5.391-0.5.407:** a second drawing thread for heavy frames
(4000+ draws; from 3000 was worse); script-thread fibers pooled (SSE control
word carried across switches, stacks cleared on reuse); the title's memcpy
native; calls through function pointers inline; constant packets copied four
words at a time; profiler names callers; box tree walk SSE2; written-page check
one read; depth copy SSE4.1; guards below.

**Open problems:** (1) the "effects crash" (job worker, sub_824472C0 ->
sub_820FC850, also in Codex 0.5.384) is guarded from 0.5.403 ("fxguard:" in
the log), cause unknown. (2) One freeze (0.5.406 Rimling): game heap full, a
cache (sub_82185D18) retrying for ever; 0.5.407 logs "heapwait:" and sleeps
1 ms a retry; cause unknown. Grep test logs for both words first.

**What limits it now:** heavy views: the command thread (17-19 ms); elsewhere
the main thread (~14 ms, fully busy). CPU target stays x86-64-v2 (no AVX2:
friends' older PCs). Switches: COD3_DRAWTHREAD=0, COD3_FIBERPOOL=0,
COD3_FIBERCLEAR=0, COD3_NATIVE_MEMCPY=0, COD3_FXGUARD=0, COD3_HOSTPROFILE_SELF=1.

**GitHub:** source-only v0.5.407 published by Codex: https://github.com/hovg-N8/CoD3Recomp/releases/tag/v0.5.407 (commit40db64a6856fbf7dce494f715b1c9284e587a4fe). No game data/binaries/logs. Release407 excludes408. Local nested Git checkout still atA3; fetch before pushing.

**How to test (no clicking needed):** put the bat's name in test-request.txt
(source root), then a build-request.txt whose text starts "test only". The
auto-builder starts the bat (BENCH-*.bat or BIG-PROFILE-32.bat only). Or
request a real build and the test starts after it. BENCH-MARSEILLE.bat = 32
players, camera held at 4 places (35 s each), bench-ordnance*.log;
BENCH-MARSEILLE-AB-*.bat flip a setting every 6 s at the same place.

**How to build:** commit source and docs first (new "## 0.5.N:" at the top of
docs/PATCH-NOTES.md), then build-request.txt in a separate step. Use a UNIQUE
local file name for every file you send (the bridge can resend a stale copy).

**Never upload game data to GitHub** (ISO, .cod/.xex, generated ppc/ppc_mp,
textures, shaders, exes).

**408:** exact heap-wait request/overflow and safe heap capacity/usage audit; off COD3_HEAPWAIT_AUDIT=0. Rimling test completed clean; freeze not reproduced. See newest STATE.

**Details:** docs/NEXT-SESSION.md (top lines), docs/HANDOFF-MARSEILLE-60FPS.md,
docs/CODEX-HANDOFF-0.5.384.md (history), docs/RELEASE-0.5.407.md.
