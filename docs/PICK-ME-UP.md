# Pick me up (read this first)

Last updated by Claude, 2026-10-23 19:55. Next build 0.5.408. Source and install: 0.5.407. One freeze seen (0.5.406, Rimling: a game heap full, a cache retrying for ever) - watched from 0.5.407 ("heapwait:" in the log).

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

**GitHub:** GitHub release NOT done yet: public repo hovg-N8/CoD3Recomp is still at 0.5.183. Claude's session had no GitHub account linked (add_repo refused). Release text ready: docs/RELEASE-0.5.407.md (source only, no binaries, credit FrankHUN88453/CoD3Recomp). Upload handwritten source/docs/scripts only.

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

**Details:** docs/NEXT-SESSION.md (top lines), docs/HANDOFF-MARSEILLE-60FPS.md,
docs/CODEX-HANDOFF-0.5.384.md (history), docs/RELEASE-0.5.407.md.
