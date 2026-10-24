# CoD3 Recomp: patch notes since 0.4.0

**Current source version: 0.5.424.** See the newest entry below.

Historical versioning note: 0.5.0 was the build of October 11, 2026, evening.
From now on, every rebuild numbers itself one higher: 0.5.1, 0.5.2 and so on.
The number shows in the window's title, at the top of CoD3MP.log, in the F11
menu's title bar, and in VERSION.txt in the game folder.

The numbers between 0.4.0 and 0.5.0 below are given after the fact, grouping
the work by what it added. Only 0.4.0 and 0.5.0 were real builds with those
names.

---

## 0.5.424: byte order turned round in one shuffle; a rotate, native

- 0.5.423 tested (city benchmark): clean, no heap waits, effects-guard
  hits, access violations or freezes; "table search native:" 3000 of
  3000 and the other six native checks passed. sub_820B64D0's share of
  the main thread: 1.10% in 0.5.422's run, 0.02% now. The camera faced
  yaw 90 as in 0.5.421's run, at about the same draws: the main thread
  12.7 / 12.8 / 13.9 / 16.3 ms a frame against 13.8 / 13.6 / 13.5 /
  15.9; 58.2 / 56.3 / 63.3 / 75.0 fps against 57.6 / 55.6 / 61.7 / 71.1.
  (The benchmark's batch file did not copy its log this run; CoD3MP.log
  was read.)
- 0.5.423 without profilers (BENCH-MARSEILLE-CITY-PLAIN, yaw 0): 81.0 /
  81.2 / 81.7 / 68.5 fps at 2188 / 2159 / 1870 / 3928 draws. In the
  busiest views (5000 draws, yaw 90) the main thread is no longer what
  holds the frame (12.7 ms): the command thread is (15.6-16.7 ms a
  frame), with the drawing thread about as busy (about 16 ms).
- The constant and register runs read from guest memory, and the index
  copies, turned each word round with four shifts and two ors (two
  shifts, an or and two half-word shuffles for the indices'). One SSSE3
  byte shuffle each now (x86-64-v2 has it): Gpu's WriteRegisterRunFrom
  (6.5% of the command thread in 0.5.423's busy views) and
  RenderCommands' Swap16Into / Swap32Into. The same bytes.
- sub_82147008 (0.75% of the main thread; from sub_82147498, a merge)
  rotates a range of 8-byte elements, std::rotate's way round the cycles
  with a division a step for their greatest common divisor. The elements
  are moved whole, never read as values, so the result is the same bytes
  however they are moved: native, the first ones put aside, the rest
  moved down, those after them. Only when the original moves anything
  (the middle strictly inside the range); else the game's own. The first
  3000 calls of a match run the game's own first, then the range is put
  back, rotated natively and compared byte for byte ("rotate native:" in
  the log); one difference and the game's own from then on.
  COD3_NATIVE_ROTATE=0 turns it off.

## 0.5.423: a table searched by its 32-byte key, natively

- 0.5.422 tested (city benchmark): clean, no heap waits, effects-guard
  hits, access violations or freezes; "unpack native:" 3000 of 3000 (27
  stores in the last) and the other five native checks passed.
  sub_82181520's share of the main thread: 1.65% in 0.5.421's run, 0.60%
  now. The camera faced yaw 63.5 (pitch 7), a facing no earlier run had:
  58.4 / 57.8 / 63.6 / 66.1 fps at 4413 / 4450 / 2876 / 3269 draws, the
  main thread 14.2 / 14.7 / 15.4 / 17.1 ms a frame; not compared.
- sub_820B64D0 (1.1% of the main thread in 0.5.422's busy views; called
  from sub_8253C130, sub_8253B130, sub_82542DB8 and sub_8253CC70) looks a
  32-byte key up in a table of 48-byte entries, one by one, each by eight
  word loads byte-swapped and compared. Equal words are equal bytes: each
  entry is now two 16-byte compares. The answer is written as the
  original does, the pair (the table's owner, the index), or none (0 and
  the word already in the stack slot the original builds the pair in).
  The first 3000 calls of a match also run the game's own and compare
  the pair ("table search native:" in the log); one difference and the
  game's own from then on. COD3_NATIVE_TABLESEARCH=0 turns it off.

## 0.5.422: a stream of packed vectors unpacked natively

- 0.5.421 tested (city benchmark): clean, no heap waits, effects-guard
  hits, access violations or freezes; "box sweep native:" 5000 of 5000
  and the other four native checks passed. sub_8248BB48's share of the
  main thread: 1.1% in 0.5.420's run, 0.09% now. The camera faced yaw 90
  as in 0.5.418's run: the main thread's CPU a frame 13.8 / 13.6 / 13.5 /
  15.9 ms against 14.3 / 13.3 / 15.5 / 17.6 (0.5.419-0.5.421's trace
  pieces about 0.4 ms of it, the rest the match); 57.6 / 55.6 / 61.7 /
  71.1 fps at 4920 / 4791 / 3710 / 1873 draws.
- sub_82181520 (1.65% of the main thread in 0.5.421's busy views; called
  from sub_82189380) unpacks a stream of packed vectors: three stream
  positions rounded up, then for each entry four big-endian 16-bit values
  made floats as the recompiled vupkd3d128 does (the bits of 3.0 plus the
  value), scaled and offset by two constant vectors (denormals flushed as
  the vector unit does) and stored at the entry's address from a table.
  The generated code built each 8-byte read out of two partial vector
  loads and did the unpack lane by lane. Native, the same values.
- Checked: the first 3000 calls of a match first work out, writing
  nothing, what they would store; the game's own then runs and every
  store, the three positions and r3 must be the same ("unpack native:" in
  the log); one difference and the game's own from then on.
  COD3_NATIVE_UNPACK=0 turns it off.

## 0.5.421: a moving box against a box's faces, native

- 0.5.420 tested (city benchmark): clean, no heap waits, effects-guard
  hits, access violations or freezes. "box native:" 5000 of 5000; the
  walk 3000 of 3000 (180 calls in the last); the slab test and the piece
  sort 3000 of 3000. sub_82490A30's share of the main thread: 0.86% in
  0.5.419's run, under 0.1% now. The camera faced yaw -180 as in
  0.5.415's run: the main thread's CPU a frame in views 1-3 13.6 / 14.2 /
  14.2 ms against 14.3 / 14.0 / 13.9 (the fighting near the camera moves
  it more than these steps do); 63.4 / 69.4 / 58.2 / 68.4 fps.
- sub_8248BB48 (1.1% of the main thread in 0.5.420's busy views; called
  from the trace's pieces, sub_824972D0 and sub_82497958) sweeps the
  trace's box (its half sizes at +80) along the segment against two sets
  of a box's faces: per face the distances of the two ends less the half
  size, then the enter and exit fractions as in the slab test, 0 as soon
  as they cross. A hit writes the enter fraction (+288) and the face's
  normal (+272); none, +317 and, if no face had the end beyond it, a
  zero fraction and +316. Native, step for step as the recompiled code,
  single precision where it rounds. The first 5000 calls of a match also
  run the game's own from the same trace and compare r3 and the 48 bytes
  it writes ("box sweep native:" in the log); one difference and the
  game's own from then on. COD3_NATIVE_SWEEP=0 turns it off.

## 0.5.420: a segment against a box, native

- 0.5.419 tested (city benchmark): clean, no heap waits, effects-guard
  hits, access violations or freezes. "walk native:" the tree walk
  matched the game's on 3000 of 3000 walks (233 calls in the last);
  both "trace native:" checks 3000 of 3000. The walk's share of the main
  thread: 1.63% (native walk 1.59, the hook 0.04) against 2.31% in
  0.5.418's run. The camera faced yaw 0 this run, lighter scenes (1677
  to 3737 draws): 71.6 / 75.5 / 75.5 / 66.9 fps, not compared.
- sub_82490A30 (0.9% of the main thread in 0.5.419's busy views; called
  from the trace's pieces, sub_82497958 and sub_824972D0) tests a segment
  against a box: first with the vector unit, apart (0) if any of x, y, z
  of the boxes' gap reaches a limit; then the segment's enter and exit
  fractions against the box's faces, z, y, x, the lower corner's and then
  the upper's, 0 as soon as enter is not below exit, else 1. Native, step
  for step as the recompiled code, single precision where it rounds and
  denormals flushed in the vector part as there. It writes nothing, so
  the first 5000 calls of a match simply run both and compare ("box
  native:" in the log); one difference and the game's own from then on.
  COD3_NATIVE_BOX=0 turns it off.

## 0.5.419: the world trace's tree walk, native

- 0.5.418 tested (city benchmark): clean, no heap waits, effects-guard
  hits, access violations or freezes; both "trace native:" checks 3000
  of 3000. The camera faced yaw 90 (pitch 20): 56.2 / 56.6 / 61.4 / 69.2
  fps at 4528 / 4899 / 3753 / 1899 draws, near 0.5.413's 54.5 fps at
  5000+ draws at that facing. No killcam in the benchmark, so the new
  guard had nothing to catch: that wants a two-player game.
- sub_824A0940, the trace's walk down the world's tree (2.3-2.6% of the
  main thread in 0.5.418's busy views, the main thread's busiest after
  the world search), is native. At each node the plane splits the
  segment and one side or both are walked, the nearer first; then the
  node's two lists of pieces each go through the slab test and, where it
  does not miss, a leaf test (sub_82499F58 / sub_8249DDC8, still the
  game's own). The same steps as the recompiled code: the same guest
  stack frames (208 bytes a level) with the midpoints and boxes where the
  original keeps them, single precision where it rounds, the midpoint's
  vector part with denormals flushed as the vector unit does.
- Checked differently from the earlier native pieces, since the leaf
  tests write into the trace and must not run twice: the first 3000
  walks of a match are the game's own, writing down every call they make
  (arguments, results, the trace after). The native walk then runs from
  the same trace against that record: each call it would make must be
  the next one written down, whose result is put back instead of calling
  again; at the end the calls and the trace must be the same. One
  difference and the game's own walk from then on ("walk native:" in the
  log). COD3_NATIVE_WALK=0 turns it off.

## 0.5.418: the killcam freeze at the replay's end

- 0.5.417 tested (city benchmark): clean, no heap waits, effects-guard
  hits, access violations or freezes ("spare slots IN USE" 31 times, as
  before). "trace native:" the slab test and the piece sort each matched
  the game's on 3000 of 3000 calls. Their share of the main thread: the
  slab test 0.7-1.1% and the sort 0.8-1.0% against 1.8% and 1.6% in
  0.5.414's run. The camera looked down this run (pitch 38, 1800-3200
  draws against 2700-4300), so its frame rates (70.2 / 67.0 / 66.2 /
  60.5) are not compared with 0.5.416's.
- 0.5.416 played by two (the host and a friend over the network, 32
  players): froze on the frame a killcam ended. During the replay the
  guard refused the same model instance (5F586190, entity 82A59010
  type 2) a dozen times, as it was meant to. Then "killcam: over (played
  out)", and from the next second nothing was drawn (the watchdog: no
  swap for five seconds). The game's main thread was not waiting on
  anything: it was walking the scene's model list in sub_8243E198 /
  sub_82433B88 for ever, with that same instance in r4 and r31. The guard
  has run only while the replay plays since 0.5.369, so the same add
  came once more on the frame the replay ended, unguarded, and the
  instance followed itself in the list.
- The tail check (the instance is already the list's last one: the add
  that makes the loop) now runs on every add, killcam or not. It is one
  read. The walk of the whole list stays for the replay only, as in
  0.5.369. Adds refused outside the replay are logged with a count of
  their own ("killcam guard: ... killcam not playing"), so the replay's
  dozen messages do not hide them. COD3_SCENETAILGUARD=0: replay only,
  as before.
- Not found: why the game adds that instance twice at all (entity type
  2, around the replay). The guard stops the freeze; the cause is still
  open.
- The friend's log (0.5.416, 32 threads at 3.5 GHz, NVIDIA, over
  Tailscale) froze the same way on its own: the main thread in
  sub_8243E198 for ever with its own doubled instance (5EFD5C70, entity
  82A5A010 type 2, caller 8245C314) right after "killcam: over". Both
  machines need 0.5.418.
- The friend's 14-24 fps (52-105 s, as low as 6 at 96-100 s; 100-190
  either side) was the GPU: the same ~2000 draws, the CPU's part of a
  frame 3-5 ms throughout, but the GPU's 46-58 ms a frame against 7
  (the wait showing first in resolves, 70-95 ms, then in present, 40-48
  ms). Video memory steady (580-610 MB), scale 1.731 all along. It came
  back by itself at 106 s. Not found in the log: what slowed the GPU (a
  smoke cloud lasts about that long, but the log names no throw; or
  something else on that PC using the GPU, or its clocks).

## 0.5.417: two of the world trace's pieces, native; the sampler asked for in front of its table

- 0.5.416 tested (city benchmark): clean, no heap waits, effects-guard
  hits or access violations (the "spare slots IN USE" player-limit
  warning, 32 times, as in 0.5.415's run: 31). BotNadeAmmo and strstr,
  0.7-1.0% each in 0.5.415's profiles, are gone from them. The camera's
  facing changed in view 4 (yaw -57, pitch 27, 3938 draws against 2804),
  so the runs compare in views 1-3 only: 63.7 / 65.4 / 57.4 fps against
  60.9 / 68.0 / 60.3 at about the same draws (3670 / 2748 / 4154 against
  3883 / 2867 / 4311). Views 2 and 3 a little slower: the match (the
  main thread holds the frame there), not shown to be the change.
- Two changes in this one build (written as 0.5.417 and 0.5.418, built
  together).
- The world trace (movement, bullets, the bots' sight lines) is now the
  main thread's busiest work after the world search: two of its pieces,
  1.8% and 1.6% of the main thread in 0.5.414's run, are native.
  sub_824851F8 tests a segment against a box's two sets of faces;
  sub_824850D0 sorts the pieces of a list that match what the trace
  looks for into three lists of up to 128. Both are copied step for step
  from the recompiled code, every value rounded to single precision where
  the original rounds it. Checked off line against a transcription of
  the recompiled code: 6 million segments (1.35 million hits) and 1500
  lists, not one difference, writes to memory included.
- The first 3000 calls of each in a match also run the game's own and
  compare ("trace native:" in the log); one difference and the game's
  own is used from then on. COD3_NATIVE_SLAB=0 / COD3_NATIVE_SORT=0
  turn them off.
- Every texture of every draw looked its sampler up in the samplers'
  table (some 12000 a frame in Marseille's busy views, for a handful of
  different samplers), on the command thread. The handle of each key
  lately asked for is now kept in front of the table (512 entries),
  emptied with it when the filtering setting changes. The second drawing
  thread only replays Direct3D calls, so the front is the command
  thread's alone. COD3_SAMPLERFRONT=0: the table always.

## 0.5.416: the bots' grenade check remembers each weapon's kind

- 0.5.415 tested (city benchmark): clean, no heap waits, effects-guard
  hits, mismatches or access violations; 3000 world-search checks
  matched. The camera faced yaw -180, as in 0.5.412's and 0.5.414's runs,
  so the views compare one for one. Frame rate, 0.5.414 -> 0.5.415:
  view 1 59.3 -> 60.7, view 2 66.4 -> 68.0, view 3 (the busy one, 4155
  draws) 52.2 -> 60.0, view 4 62.2 -> 66.5. The main thread's CPU a
  frame in view 3: 18.7 ms -> 16.0 (0.5.412: 17.8); lower in every view.
  The walk and its push_back were 4.9% of the main thread, now 3.0%.
  Some of view 3's difference is the match (how much fighting near the
  camera), but every view moved the same way.
- Each time a bot's grenades were asked about, all 127 weapons' names
  were read, lower-cased and searched up to nine times for "grenade",
  "smoke" and the like (BotNadeAmmo and strstr together about 1.5% of
  the main thread on Marseille at 32 players). Each weapon's kind is now
  remembered with the name it was worked out from, and worked out again
  only when the name is different (a new map). COD3_NADEKINDCACHE=0:
  worked out every time, as before.

## 0.5.415: the world search's stack without push_back

- 0.5.414 tested (city benchmark): clean, no heap waits, effects-guard
  hits, mismatches or access violations; 3000 world-search checks
  matched. The camera faced yaw -180 all run, as in 0.5.412's, so the
  two compare view by view: the busy view (4206 draws) took the command
  thread 14.2 ms a frame against 15.3 (3.43 us a draw against 3.65, 8%
  less, 0.5.413 and 0.5.414 together); memcpy's share fell from 3.8% to
  2.3%. That view's frame rate went with the main thread, which had more
  to do in this run (18.7 ms of CPU a frame against 17.8: more fighting
  near it): 52.2 fps against 54.9. The main thread holds the frame.
- The game's world search (NativeCull::Walk, the main thread's busiest
  single function on Marseille, 3.3%) kept the boxes still to look at
  in a vector, and its push_back took another 1.1% of the main thread.
  The stack is now a plain array sized once for the deepest it can get
  (the roots and three a level). Checked against the vector on 3000
  random trees: the same pieces in the same order. The game's own
  3000-query comparison at the start of each match still runs.
  COD3_CULL_VECTORSTACK=1: the vector again.

## 0.5.414: the shader constants copied without memcpy calls

- 0.5.413 tested (city benchmark): clean, no heap waits, effects-guard
  hits, mismatches or access violations. The camera faced yaw 90 all run,
  as in 0.5.410's: the same four views, draw counts within 0.5%. Busy
  views (78 samples over 4000 draws): the command thread 16.2 ms a frame
  against 17.0 in 0.5.410 (3.19 us a draw against 3.35, 5% less); view 3
  8% less. The frame rate stayed (54.5 against 54.6): in these views the
  game's main thread (17.4 ms of CPU a frame: the server and bots 6.9,
  the client and its drawing 11.5) is now what holds the frame. 0.5.411
  to 0.5.413 together; 0.5.415 and on are for the main thread.
- Each draw's shader constants are copied into the constant ring, the
  ones each program reads. A run of consecutive constants went as one
  call to the library's memcpy, a short copy of a length known only at
  run time: 3.2% of the command thread in 0.5.410's busy views was
  memcpy called from there. They are now copied a constant (16 bytes) at
  a time in a plain loop, front to back as before. Checked against the
  old copy on 2000 random constant maps: the same bytes, both in the
  ring and in the copy kept of what was sent. COD3_CONSTLIBCOPY=1: the
  memcpy calls again.

## 0.5.413: the drawing tables' entries kept together

- 0.5.412 tested (city benchmark): clean, no heap waits, effects-guard
  hits, mismatches or access violations; the 3000 world-search checks
  matched. The profiler's D3D11CoreRegisterLayers (the GetDesc calls,
  named after the nearest export) went from 0.8-0.9% of the command
  thread to nothing. The camera faced yaw -180 the whole run, other
  scenes again, so its frame rates are not compared with 0.5.411's.
- The test ran with COD3_BENCH_CLIENTTURN (BENCH-MARSEILLE-CITY-TURN.bat):
  the local client's view yaw climbed to 5760 while the player kept
  facing -180. In the bot test neither setviewpos nor the client's
  angles turn him; runs are compared by draw counts and the profiler.
- The textures', vertex buffers' and index buffers' entries were kept
  in std::deques. Microsoft's library makes a deque of anything over
  16 bytes one entry a block, so each look up by handle (some 25000 a
  frame in Marseille's busy views, on the command thread) went through
  the deque's map to an entry of its own somewhere on the heap. They
  are now kept in blocks of 256 that never move: one small table of
  blocks, then the entry, with its neighbours beside it. As with the
  deque, an entry stays where it is while more are added. Nothing
  else changes.

## 0.5.412: the texture view check asks once a view

- 0.5.411 tested (city benchmark): clean, no heap waits, effects-guard
  hits, mismatches or access violations. The camera faced about where
  0.5.408's run did (pitch 18-21, yaw 157-164): between 4000 and 4500
  draws, 3.43 us a draw at 54.2 fps (42 samples) against 0.5.408's
  3.65 us at 53.2 fps (39 samples), some 6% less a draw. Not the very
  same scenes, so a good sign rather than proof. The texture lookup's
  share of the packet loop's time fell from 7.8% to 6.2% (profiler).
- Every texture of every draw had its view asked for its format
  (GetDesc, a call into Direct3D) by a check that only writes to the
  log when a view is of the wrong kind: some 12000 calls a frame in
  Marseille's busy views, on the command thread. Each view is now asked
  once and noted (the texture pack's replacements, always of another
  kind, included). Nothing drawn
  changes; the most a reused view could cost is one log line.
  COD3_VIEWCHECKALL=1 asks on every draw, as before.

## 0.5.411: the texture lookup's front cache

- 0.5.410 tested (Marseille city benchmark, 32 players): clean, with
  no heap waits, effects-guard hits, mismatches or access violations.
  Every view faced yaw 90, heavier than earlier runs: 72 samples between
  5000 and 5500 draws, median 54.75 fps, command thread 17.0 ms a frame,
  3.35 us a draw. That is the baseline for 0.5.411.
- 0.5.410's camera hold did not work: setviewpos was sent again every
  second, yet every view faced yaw 90 the whole run. Here setviewpos
  places the player but does not turn him. The facing stays one way for
  a run (it differs between runs), so runs are compared at the same draw
  counts. The resend is now off (COD3_BENCH_HOLDTURN=1 turns it on); the
  "bench: hold check" line stays, saying which way each view faced.
- Every texture of every draw asked two tables in turn: is this a
  surface a resolve made, and which texture is it. That is two hash
  probes per texture, some 24000 a frame in Marseille's busy views, on
  the command thread that limits those views. A small cache now sits in
  front of them. A hit needs the same address, format, size and kind of
  texture as before, no resolve made, remade or let go since, and the
  texture still in use under that same key; then it is exactly the
  answer the two tables would give. The checks for changed texels run
  as before. COD3_TEXTURELOOKUPCACHE=0 turns the cache off.

## 0.5.410: the benchmark camera holds its facing

- 0.5.409 tested (Rimling, 32 players): clean. All 3000 world-search
  checks matched; no heap waits, effects-guard hits, mismatches or access
  violations; median 55.8 fps at about 3760 draws.
- The Marseille benchmark moved the camera to the right places but did
  not keep it facing the way it was told. 0.5.408's city run asked for
  pitch 0 yaw 0 at every view; views 2-4 faced pitch 20.7 yaw 156.6.
  0.5.407's run faced -180 three times and -114.3 once. So per-view
  numbers from different runs were not the same scenes.
- The facing is now checked twice a second. When it is more than 1.5
  degrees off, setviewpos is sent again (at most once a second), logged
  "bench: turned back". Each view also logs "bench: hold check" near its
  end with where he faces. COD3_BENCH_HOLDTURN=0 restores once-a-view.
- Benchmark only; nothing changes when playing.

## 0.5.409: Restore the bounded world-search self-check scratch

- mp_fixes.cpp sub_824193E0 still placed its self-check object 24 KB below
  the guest stack, despite the 0.5.385 note saying this was fixed. Restore
  placement 128 bytes below the original walk's 2304-byte frame. The walk's
  only callback is intercepted before any guest-stack use; the 32-byte object
  fits within the Xbox 360 ABI red zone below that frame.
- COD3_CULL_NEARSTACK=0 restores the old placement for comparison. The native
  culling algorithm and its 3000-query comparison remain unchanged.
- This removes a possible small-stack corruption source; it is not a proven
  cause of heap exhaustion or the effects crash. Built OK; Rimling clean
  (3000/3000 checks matched).

## 0.5.408: Identify the heap request that cannot make progress

- The cache has two allocation loops. Its old wait report always used the
  entry's largest request, even if a smaller later level was failing.
  mp_fixes.cpp HeapWait now reports the current loop's exact units, stride,
  header, 64-bit size/overflow, allocator and eight game-heap capacity/usage
  records on the first and hundredth empty reclaim (12 reports maximum).
- Diagnostic reads cannot demand-commit guest memory. Normal successful
  allocation behavior and the existing 1 ms retry sleep are unchanged.
  COD3_HEAPWAIT_AUDIT=0 disables the additional diagnostic reports.
- Built OK; automated Rimling 32-player test completed with no heap
  waits, effects-guard hits, native mismatch or access violation. The
  freeze did not reproduce, so the new diagnostic path was not exercised.
  This is evidence collection, not a fix for the freeze.

## 0.5.407: a watch on a heap freeze

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

## 0.5.406: the depth copies four texels at a time

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

## 0.5.405: the written-page check in one read

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

## 0.5.404: the box tree walk asks for its nodes early

- 0.5.403's run: clean (no crash, no bad call, the effects guard never
  needed). The same four views: 68.8 / 69.1 / 68.4 / 62.7 fps, the main
  thread 14.2 ms a frame.
- The box tree walk (NativeCull::Walk, still the main thread's busiest
  function at 3-5%) waits on memory for each node it pops; it now asks
  for a node's four children (64 bytes) as soon as it pushes them.

## 0.5.403: a guard for the effects crash

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

## 0.5.402: a fiber used again starts as a fresh one

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

## 0.5.401: calls through pointers inline, constant words four at a time

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

## 0.5.400: the title's memcpy native

- 0.5.399 ran the city benchmark twice with no crash and none of the bad
  calls that led up to 0.5.397's and 0.5.398's (the fiber pool's fix
  holds). Heavy views (5000-5700 drawn) 47-54 fps, held by the command
  thread (17-19 ms); lighter ones 56-69.
- The profile by source line: the main thread's 3% "sub_8231E8C0" is the
  title's own memcpy, used by every thread. It is now the host's (same
  bytes, same order; a copy whose destination starts inside its source,
  or one near the device registers, is still the title's own).
  COD3_NATIVE_MEMCPY=0: the title's own always.

## 0.5.399: the fiber pool's crash found

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

## 0.5.398: the main thread measured, the box tree walk quicker

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

## 0.5.397: script thread fibers kept and used again

- 0.5.396's profiler, now able to say who called: the main thread's 3.7%
  in NtFreeVirtualMemory (and 0.6% in ZwAllocateVirtualMemory) is
  Coroutines::Destroy and Begin: every one of the level's script threads
  got a new host fiber (a 4 MB stack reserved from Windows) and handed it
  back when it ended, many times a frame with 32 players.
- A fiber whose script thread ran to its end now goes into a pool (up to
  64) and runs the next script thread; one whose thread is still inside
  a yield is deleted as before. COD3_FIBERPOOL=0: the old way. The
  COD3_TRACECOROUTINES line says how many fibers were made and reused.

## 0.5.396: the profiler says who called

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

## 0.5.395: the drawing thread only for heavy frames

- Across the A/B runs of 0.5.392-0.5.394 the drawing thread made the
  command thread lighter (7-13% a draw at 4000-5200 drawn, less in
  lighter views), but in views where the game's main thread held the
  frame rate the frame rate was often a little lower with it on (about
  1.5 fps on average): its core is one the game's own threads could use.
- It is now used only in heavy frames, where the drawing is what holds
  the frame: from 4000 draws a frame, off again under 3000
  (COD3_DRAWTHREAD_MIN=draws; 0 = every frame; COD3_DRAWTHREAD=0 = never).
  The A/B benchmark compares that against off.

## 0.5.394: two small costs off the command thread

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

## 0.5.393: a cheaper hand over to the drawing thread

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

## 0.5.392: measuring the drawing thread

- 0.5.391 ran the whole Marseille benchmark with no crash and no
  graphical fault, but the command thread's time per draw did not drop
  (3.38 us a draw against 3.31 before). This build measures why: a
  "queue:" line every second (with the render profile) says how often
  and how long the command thread waited for the drawing thread, from
  which place, and how busy the drawing thread really was.
- COD3_DRAWTHREAD_TOGGLE=seconds turns the drawing thread on and off in
  turns in one run (logged as the benchmark's settings A and B), and
  BENCH-MARSEILLE-AB-DRAWTHREAD.bat runs the benchmark that way.

## 0.5.391: a second drawing thread

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

## 0.5.390: the benchmark compares settings in place

- 0.5.389's benchmark ran (Claude started it through the auto-builder,
  which can now start a benchmark batch file on request). The camera
  goes to the four places but keeps the spawn's direction (the game
  ignores setviewpos), so each run sees different views. Across all
  runs the drawing thread takes about 23 ms a frame at 6000 things
  drawn (16.6 is 60 fps); the constant copy saved about 2% of that.
- The benchmark can now switch a setting back and forth every 6 seconds
  at the same place (COD3_BENCH_TOGGLE="A|B"), so a setting is compared
  on exactly the same view. BENCH-MARSEILLE-AB-DETAIL.bat compares model
  detail Maximum with the game's own; BENCH-MARSEILLE-AB-FOV.bat compares
  field of view 90 with the game's 65.

## 0.5.389: the benchmark camera turns (second try)

- 0.5.388 still faced one way: in the bot test the host's view comes
  from the bots' own commands, not the client's angles. Each view is
  now set with the game's own "setviewpos" console command, which turns
  the player on the server's side as a spawn does.

## 0.5.388: the benchmark camera turns

- 0.5.387's pictures showed the camera at the right places but always
  facing the same way, and one of the high views out past the edge of
  the map. The turn is now corrected a few times a second instead of
  every frame (each correction was added again before the last had
  reached the server), and that view is replaced by another high one
  over the city.

## 0.5.387: the benchmark takes a picture of each view

- The first benchmark run (0.5.386) finished cleanly but drew far fewer
  things than the same places did in the photo tour, so the camera may
  not be where it should be. The benchmark now saves a picture of each
  view (screenshots folder) and logs where the player really is.

## 0.5.386: A fixed Marseille benchmark; one copy less a draw

- Tested 0.5.385 (Claude ran it): the bot match finished cleanly, the
  native routines matched the game's again, and the effects watch found
  nothing odd. That match never reached the busy views (the camera stays
  where you spawn), which is why a fixed benchmark was needed.
- New BENCH-MARSEILLE.bat: the same 32-player bot match, but the camera
  is held at four fixed views of Marseille, 35 seconds each: two from
  high over the city (the heaviest scenes, 6000-7000 things drawn) and
  two down long streets at eye height. Every build is measured on the
  same scenes. Log: bench-ordnance.log.
- The drawing thread copied every draw's shader settings twice; the
  second copy is only needed by a mode that is off (the native command
  lists), so it is only made when that mode is in use.
  COD3_ALWAYSSENTFILE=1 makes the copy always, as before.

## 0.5.385: Watching for the effects crash

- One test of 0.5.384 (Codex's, with the index lookup cache turned off)
  crashed about 90 seconds into the match: an effect asked for 1.4 GB
  for its particles, the effect pools ran dry, and the game called into
  garbage. It hasn't happened in the other runs and its cause is unknown.
- Now every particle group that asks for an impossible amount (over
  16 MB) or has an unreadable setting is written to the log with the
  numbers it was given and who asked, so the next time it happens it
  tells us which effect and from where. The game does exactly what it
  did before. COD3_FXGROUPWATCH=0 turns the watching off.
- The native world search's self-check (first 3000 searches) now puts
  its stand-in object just under the game's own working space instead
  of 24 KB down the stack, which a thread with a small stack might not
  have.

## 0.5.384: Measure the whole command processor

- Profiling-only measurement in render_stats.cpp EndFrame: once per
  reporting second, read the command thread's kernel+user CPU time using
  GetThreadTimes. New cp-thread lines include packet processing and driver
  CPU work and exclude sleeping/descheduling. There are no new per-draw
  clock calls. COD3_THREADCPUPROFILE=0 disables the measurement.
- This corrects a gap in the existing render CPU figure, which only times
  Draw/Resolve/Present. Those are elapsed scope times; do not subtract
  them from thread CPU time or call either one the other's remainder.
- Justin tested: busiest-view command-thread median 21.88 ms CPU per
  frame at 5534 draws, using 95.6% of one core. It is saturated there.
  This is the first full-thread baseline; earlier render CPU numbers
  excluded packet processing. All native self-checks matched.

- Follow-up stability: same-build index-cache-off comparison crashed in
  particle effects; a final pre380-behavior control completed with no
  allocation errors/access violation. Cause unresolved; do not call
  stability established. Final evidence: CODEX-HANDOFF-0.5.384.md.

## 0.5.383: Faster lookup of repeated index buffers

- render_resources.cpp IndexBufferFor: a small front cache remembers
  recently used index-buffer handles, avoiding the full key-table hash
  and probe on hits. The complete live key (address/count/width/reset) is
  checked; page-watch and content checks still execute as before.
- COD3_INDEXLOOKUPCACHE=0 restores the previous lookup path. This targets
  cache lookup overhead; it does not skip index-content validation.
- Justin tested: overall median 56.8 fps, busy views 47.6 fps. Matched
  5000-5499-draw views stayed about 46.4 fps (previously 46.3); no clear
  speedup there. Sampled index lookup self share fell 3.45% to 2.9%.
  Different matches; all native checks passed. Same-build off comparison
  still desirable before calling this a confirmed gain.

## 0.5.382: Less packet history around every draw

- gpu.cpp ExecuteBuffer: skip the recent-packet diagnostic copy for type-3
  draws, inline constant writes and NOPs. Keep synchronization, fences,
  interrupts, indirect calls and other type-3 packets in the history.
  Command execution and payload reads used by command handlers are unchanged.
- COD3_REMEMBERDRAWS=1 restores the 0.5.380/381 history; existing
  COD3_REMEMBERALLPACKETS=1 restores the entire pre-380 history.
- Justin tested: busy-view drawing CPU fell from 3.224 to 3.132
  microseconds per draw. Busy-view fps was 46.0 vs 48.7, but median
  busy draw count rose 4691 to 5284. In 5000-5499-draw views fps stayed
  46.3, while CPU per draw fell 3.190 to 3.087 microseconds. Different
  matches; native self-checks passed.

## 0.5.381: Watch the remaining small models

- The 0.5.380 fingerprint audit found index buffers were only a few MB
  per five seconds, while vertex buffers were hundreds of MB. Many of
  the repeatedly checked models were just below the 4 KB watch cutoff
  (roughly 3 to 4 KB).
- render_resources.cpp VertexBufferFor: lower the default stable-model
  page-watch threshold from 4 KB to 1 KB. Existing safeguards for buffers
  that change within a frame or share frequently written pages remain.
  COD3_WATCHSTILL_KB=4 restores the 0.5.380 threshold.
- Justin tested: median hashing fell from about 4.1 MB to 0.94 MB per
  frame, but busy-view performance was essentially unchanged (49.3 to
  48.7 fps, 3.218 to 3.224 microseconds per draw). Overall render median
  53.25 to 54.15 fps. No clear busy-view speedup; native checks passed.

## 0.5.380: Less diagnostic work in the drawing thread

- gpu.cpp ExecuteBuffer: register (type 0) and padding (type 2) packets no
  longer copy their payload into the recent-packet diagnostic ring. Type 3
  packets still keep their history. Packet decoding, register writes, waits,
  fences and draw execution are unchanged. COD3_REMEMBERALLPACKETS=1 restores
  the complete old diagnostic history for comparison or troubleshooting.
- Justin tested: views over 4000 draws rose from 45.6 to 49.3 fps;
  drawing-thread CPU time per draw fell from 3.382 to 3.218 microseconds.
  Different bot matches; overall median stayed 53.25 fps. All native
  self-checks passed. Baseline log was 0.5.378; visual feedback not yet given.

## 0.5.379: Marseille's train tracks drawn over things

- Tested 0.5.378 (Justin ran it): the busiest views on Marseille went
  from 39 to 45 fps (each thing drawn 4.04 -> 3.39 microseconds on the
  drawing thread), the slowest moments the best yet (43), no crashes.
- Fixed: the rails and sleepers on Marseille drew over the crates, posts
  and your own gun when you were close to them, lying down or under a
  wagon (Justin's video; turning Steady decals off cured it). Steady
  decals pulls the layers laid over a surface (tracks, marks, bullet
  holes) a hair towards you so they stop flickering; part of that pull
  grows with how edge-on the surface is, and with no limit it grew past
  whole objects. It is now limited to a tiny amount. Steady decals can
  be left on. COD3_DECALNUDGECLAMP=0 removes the limit.

## 0.5.378: Marseille's busy views, part 2

- Tested 0.5.377 (Justin ran it): the drawing thread now re-reads 3.6 MB
  a frame instead of 10, no crashes, but the frame rate was about the
  same (55 average this run, 58 last; the bots don't play the same match
  twice). The log shows why: the frame rate follows the number of things
  drawn. Up to about 3000 it holds 55 to 61; the views with 4500 to 5400
  fall to 38 to 43, because the drawing thread spends about 3.6
  microseconds on each one.
- One of those costs: the game keeps its moving geometry (marks, effects)
  in one 12 MB pool, and the frame's first draw compared all 12 MB with
  the copy on the graphics card to find the few pieces that changed. The
  memory already says which pages were written, so only those pieces are
  compared now. COD3_BIGCOMPAREALL=1 compares it all as before.

## 0.5.377: Marseille's busy views

- Tested 0.5.376 (Justin ran it, Marseille, 32 players): 59 fps average
  (43 this morning), slowest moments 41, no crashes; the three native
  routines matched the game's own on every check (5000 blends, 3000
  searches, 2000 inverses). Most of the match held 57 to 65; the dips to
  27 to 50 came in the views with the most on screen.
- In those views the drawing thread was re-reading 7500 small models
  (the soldiers' and the props', 23 to 32 KB each) every frame, 300 to
  400 MB a second, to see whether they had changed, which they hadn't.
  Models of 32 KB and up were already watched instead (the memory itself
  says when it's written); now everything from 4 KB up is.
  COD3_WATCHSTILL_KB=32 puts the old size back.

## 0.5.376: the soldiers' animation blend

- The blend between two bone rotations, done for every bone of every
  soldier every frame, now runs as native code (3.7% of the main thread
  on Marseille at 32 players). Same arithmetic as the game's; the first
  5000 blends also run the game's own and must match it bit for bit, or
  the game's own is used from then on. COD3_NATIVE_SLERP=0 turns it off.

## 0.5.375: Marseille, back to the main thread

- Tested 0.5.374 (Justin ran it, Marseille, 32 players): 50 fps on
  average (was 42), slowest moments 39 (was 36), no crashes. The drawing
  thread now waits a third of the time, so the game's main thread is the
  limit again, at 19.4 ms a frame (16.6 is 60 fps).
- The game's search of the world for what is near a box (what to draw
  and what to test against) now runs as native code. It was the biggest
  single thing on the main thread, 6.5%. For its first 3000 searches the
  game's own search runs alongside and the two lists of what they found
  are compared; if they ever differ, the log says so and the game's own
  is used from then on. COD3_NATIVE_CULL=0 turns it off.

## 0.5.374: Marseille, the drawing thread, part 2

- Two small vertex buffers the game rewrites during every frame (effects
  and tank treads) were checked byte by byte, all 128 KB of each, by
  every draw that used them: most of the 16 MB the drawing thread checked
  each frame on Marseille. Now a copy of what the graphics card has is
  kept, and each draw compares only the vertices it actually uses,
  sending just the parts that changed. A draw whose vertices can't be
  worked out compares the whole buffer, as safe as before.
  COD3_MIDFRAMEHASH=1 puts the old way back.

## 0.5.373: Marseille, the drawing thread

- Tested 0.5.372 on Marseille at 32 players: the new matrix inverse gave
  the same answer as the game's own on all 2000 checks, and the main
  thread got about 2 ms a frame quicker. The slowest moments went from
  28 to 36 fps; the average stayed at 42 because the drawing thread is
  now what holds it back.
- The drawing thread's most common job, writing a draw's settings into
  the graphics registers, now goes through the quick path the shader
  constants already used, instead of one register at a time with every
  debugging check. That was about a tenth of the drawing thread.
  COD3_SLOWREGISTERS=1 puts the old way back.

## 0.5.372: Marseille, first cut at the main thread

- The 0.5.371 profile of Marseille at 32 players worked (no freeze). It
  showed the graphics card isn't the limit: the game's main thread is,
  at about 23 ms a frame (server 8 ms, the rest 14 ms), with the drawing
  thread close behind it.
- The game's 4x4 matrix inverse now runs as native code. It took 5% of
  the main thread, used 24 places in the server's code, and did every
  number through memory. Same steps and the same rounding as before; the
  first 2000 uses also run the old one and the log says if they ever
  differ. COD3_NATIVE_INVERSE=0 turns it off.

## 0.5.371: the profiler made safe, ready for Marseille

- The profiler froze the game twice. It walked back through a running
  thread's calls to see who called what; now by default it only notes
  where the thread is at each sample, which is what the fixes need and
  can't upset the game (COD3_HOSTPROFILE_SELF=0 for the old way).
- BIG-PROFILE-32.bat now takes a map and profiles both the game's main
  thread and the drawing thread; with no map it does Marseille
  (ordnance), the map that drops lowest.
- No change to play.

## 0.5.370: frame rate, step 2: two big wastes on the main thread removed

- I ran the profiler on your PC during a 32-player match (Rimling, 28
  bots, BIG-PROFILE-32.bat in the game folder). Two things that have
  nothing to do with playing the game were taking almost a fifth of the
  game's main thread:
  - About 11%: reading settings. Several of our checks looked up their
    on/off switch in the environment every time they ran, and one of them
    (the 32-player roster) runs for every player, every frame. Each lookup
    locks and searches the whole environment. Now each setting is read
    once and remembered.
  - About 7%: writing the log. Every line went straight out to the log
    file the moment it was printed, about 300 lines a second with bots.
    The log is now collected and written out every tenth of a second
    instead. If the game crashes, what was waiting is still written first.
- Measured (Rimling, 32-player test, 28 bots): 43 fps before, 53 fps now
  (median; the slowest moments 39 -> 49). That's faster than before all
  the recent updates (47 on October 17). Bots now take about 1 ms a frame
  (was 2.6), the match about 7 ms (was 9.8).
- COD3_LOG_UNBUFFERED=1 puts the log back to line-by-line, if ever needed.
- The profiler itself froze the game after about 95 s of play (the game
  stopped; the log kept going). Known from before (0.5.236); it gave 20
  good reports first. Not a problem in normal play (it's off).

## 0.5.369: frame rate, step 1: measure, and cut what we added that runs all the time

- You (01:06): frames have dropped by about half on every map since we
  added so much, especially with 32 players.
- What the logs say, same map (Merville) and the same 28-31 bots:
  0.5.247 (Oct 17) ran at a median 41 fps, the 0.5.367 log at 39. So the
  recent updates cost a few frames there, not half. What halves the frame
  rate is the 32 players themselves: each frame the game runs the match
  (about 9-10 ms with 32 players), the bots (about 2.5 ms) and then draws
  (about 12 ms), one after the other: about 25 ms, so about 40 fps.
  With fewer players, all three are smaller.
- Cut now: two safety checks added for the killcam (on the game's list
  of things to draw) ran on every frame, walking that whole list each
  time something was added to it. They only ever caught anything during
  the killcam, so now they only run then.
- Measured now: the killcam's recording, which runs all the time
  ("perf: killcam recorder ... ms" in the log every 10 s).
- Next steps: see where the match's 9-10 ms and the drawing's 12 ms go
  (fix the profiler run that cut the log off last time), then cut there.

## 0.5.368: the soldier-pose watch, sharpened

- 0.5.367 tested (you played a while, saw no odd poses). Its log: 9
  killcams, all played out, guns put back in hands every time, and the game
  never rebuilt a body mid-killcam (so 0.5.366's guess wasn't the cause in
  that session). The watch itself over-reported: a value I took for "this
  body is in its dead mode" is set on every living body, guns have
  different bone counts (93, 94, 96, 102 all normal), and a body that isn't
  on screen isn't posed at all, so it looks "frozen".
- Now the watch only flags what can't be normal: a body on screen in the
  last second, not in a vehicle, moving, whose pose hasn't changed for 6
  seconds (the T pose), one still using the killcam's pose memory, and
  impossible bone counts. When you next see a T pose or a "dead" soldier
  walking around, the log should name him.

## 0.5.367: a watch on soldiers in the wrong pose

- You (00:40): this has been happening now and then for a while: some
  soldiers in the T pose, or lying on the ground looking dead while they're
  clearly alive and moving. So it isn't only the killcam.
- Every 2 seconds the game now checks every living soldier's body and
  writes to the log when one is in its "dead" mode while alive, uses the
  killcam's own pose memory, has a bone count that doesn't match what he's
  carrying, or hasn't changed pose for 6 seconds while he moves (the
  T pose). Each line says how long ago the last killcam ended, so it shows
  whether the killcam is to blame or something else is.
- Includes 0.5.366's fix (the killcam keeps the game's rebuilds).

## 0.5.366: no more T-pose soldiers after a killcam

- You, on 0.5.365: vehicles work with 32 players and no crash, the
  killcam is almost what it should be, but some soldiers stand in the
  T pose (arms out, no gun), one right next to your body after the
  killcam in clip 00.33.12, and they keep running around like that.
- What I think happens: for each frame of the killcam the replay swaps in
  each body's recorded pose and gun, then puts the body back after. If the
  game rebuilds a body during that frame (a replayed weapon switch makes it
  lay the body out again), putting back the old layout mixes the old and
  the new: a body whose pose can't be worked out, which the game draws in
  the T pose until it rebuilds that body again.
- Now the replay only puts back what it changed: if the game rebuilt a body
  during the frame, the game's new layout is kept. The log says each time
  this happens ("rebuilt by the game during the replayed frame"), so the
  next test confirms it.

## 0.5.365: crash in the killcam after a vehicle death fixed

- You, on 0.5.364: vehicles work with 32 players (confirmed: you got in),
  but the game crashed shortly after you were killed.
- The crash: during the killcam, the game looked up a point on a body (a
  "tag", like where the hand or a seat is) and followed a model that
  wasn't real any more, into empty memory. The killcam puts each body's
  gun back in its hands (0.5.359) from what it recorded; a recording made
  around the vehicle could hold a model that was no good by then.
- Now the killcam checks every recorded model the same way that lookup
  reads it before putting it on a body, and only when the body itself is
  the same one. And the lookup itself skips any model it can't read
  instead of crashing (the log says which body, once).
- The vehicle test logging from 0.5.361 to 0.5.363 is off now
  (COD3_VEHICLE_PROBE=1 turns it back on).

## 0.5.364: vehicles work again with 32 players

- FOUND (0.5.363's log, 32 players): your "let me in" request got through
  every check except the seat: the game said the seat was taken, then
  looked for whoever was in it and found no one.
- Why: each vehicle keeps one byte per seat saying which player sits there,
  and a special number meaning "empty". In the original game that number
  is 24 (one past the last of 24 players). When we raised the limit to 32,
  one of the places that empties the seats was changed to write 32, but the
  places that check "is this seat empty?" still looked for 24. So every
  seat looked taken. Bots hit the same wall.
- Now every place that empties a seat writes the same "empty" number
  (24 with the 24-player limit, 32 with 32), and every check looks for that
  same number: 11 places in the game's code, all in its vehicle seat
  handling. With the 24-player limit nothing changes.
- The vehicle logging from 0.5.361 to 0.5.363 is still in for this test.

## 0.5.363: vehicles: following the "let me in" message

- 0.5.362 caught it: getting into a vehicle is a message inside the game's
  session ("player n wants seat s of vehicle v"). The game handles it in
  five checks (who sent it, the vehicle, the player, the player against
  the vehicle, the seat) and then seats you.
- At 32 players one of those says no, or the message never arrives. This
  build writes each check's answer to the log, so one try at the jeep with
  32 players shows which one, and that is the line to fix.

## 0.5.362: vehicles: catching the moment you get in

- 0.5.361's two runs, side by side: at 24 players, the moment you get in,
  your player gets linked to the vehicle; at 32, that link never happens,
  though you stand right there and the prompt shows. Your player, your
  state and the vehicle otherwise look the same in both.
- This build catches the game in the act: the first time you stand next to
  a vehicle, it watches the spot where that link is written, and when it is
  written, the log gets the exact chain of game code that did it. With
  that chain I can follow the same path at 32 and see where it stops.

## 0.5.361: vehicles: finding what the 32-player limit breaks

- What we know: with the 24-player limit (PLAY-24-PLAYERS.bat) you get in
  the jeep and the motorbike; with your usual 32 the "hold to enter" prompt
  shows but nothing happens, and the bots can't get in either. The killcam
  is not it (PLAY-NO-KILLCAM.bat changed nothing).
- I went through the parts of the 32-player work that touch vehicles (the
  vehicle seat checks, the per-player object pool, the slot the map's
  "level" entity used to share with player 25): each is handled, so this
  build measures instead of guessing.
- While you stand next to a vehicle, the game writes your player and the
  vehicle to the log once a second. One short run at 24 and one at 32
  side by side shows exactly what's different, and that is the fix.
- COD3_VEHICLE_PROBE=0 turns it off.

## 0.5.360: killcam timed like CoD 4: your death, then the kill, then you falling

- You, on 0.5.359: it still isn't smooth, you can't tell when he actually
  kills you, and it freezes slightly when you die.
- I went through 22.17.22 frame by frame. The killcam started on the very
  frame you died, with no moment to see your own death. It also ended
  exactly on the kill: the last frame then sat still for 0.4 s (23
  identical frames), which is the freeze you saw. The kill itself was the
  last frame, so it never "landed".
- Now it runs the way CoD 4 does:
  1. You die and see your own death as normal (1.5 s, recording keeps going).
  2. The killcam plays from 4 s before the kill until 1.2 s after it, so
     you see him line up, the shot, and yourself drop, and then it ends.
  3. Back to the game.
- The recording holds 6 s now (it held 4.8 s), to fit the extra second
  and a half.
- To tune: COD3_KILLCAM_DELAY (ms before it starts, default 1500),
  COD3_KILLCAM_PRE (default 4000), COD3_KILLCAM_POST (default 1200).
- Vehicles on Eder Dam: noted, waiting on which vehicle and whether the
  "enter" prompt showed.

## 0.5.359: killcam: your gun back in your hands

- 0.5.358's log showed exactly how a body holds its gun: the body is a
  list of models (the soldier, then the gun) with 92 bones for the soldier
  plus 4 for the gun. When you die the game shortens that list back to just
  the soldier (the gun's entry stays written, but is no longer counted).
  The killcam was putting back your 96-bone pose of then on a one-model
  body, so the gun was never drawn.
- Now each recorded moment also keeps the body's model list, and in the
  killcam every replayed body gets the list it had then, put back right
  after each frame. Your replayed self, and every bot, holds the gun he
  was holding at that moment (and switches when he switched).
- Also explains 0.5.353: the "fallen body" bytes it reset were really the
  gun's place in that list, which is why you vanished in that build.
- To turn it off: COD3_KILLCAM_GUNS=0. The end-of-killcam log line counts
  "guns put back in hands".

## 0.5.358: killcam: finding your gun on your body

- 0.5.357 tested by you (clips 22.17.22 and 22.18.03): the killer's hands
  and gun now show for the whole killcam, from the house out to the street.
  Still missing: the gun YOU were holding, on your body in the replay.
- What I found: a body's skeleton has 92 bones without a gun and 96 with
  one (the gun brings its own 4). Your body showed 92 even while alive in
  one check and 96 in another, so the game puts the gun on and takes it off.
  The killcam put back your recorded pose, but not your gun.
- This build writes to the log every time a body's gun goes on or comes off
  (yours and a few bots'), with what changed. That shows exactly what to put
  back so your replayed body holds its gun. Nothing else changes; the
  killcam works the same as 0.5.357.

## 0.5.357: killcam: the killer's gun in his hands the whole time

- 0.5.356 tested by you (clips 21.57.20 and 21.58.19): a big step. You are
  in the replay running at him with your rifle, his aim follows you (the
  log: 0 degrees off his aim at the moment he killed you), and the melee
  that killed you plays. Still off: his hands and gun only showed up for
  the last half second (the melee), the first half second shows the
  ground, and the "Waiting for respawn" text shows over the end.
- The gun: the game's "draw the gun in view" step checks the killer's
  state (all fine in the log: weapon, alive, normal movement) and then
  two switches of THIS machine's own view, plus whether a gun model is
  ready. Your player is dead, so your view's switches say "no gun". For
  the killcam's frames those two switches are now opened (put back right
  after each frame), and all of the checks are written to the log four
  times a second so the next test shows exactly which one was holding
  the gun back if it still does not show.
- To turn this off: COD3_KILLCAM_GUNGATE=0.

## 0.5.356: killcam: the killer's real aim, and his gun back

- 0.5.355 tested by you (video): the killer's view stared at a wall while
  he was shooting you, and his gun was not in view.
- The wall: for a bot, the game keeps two view directions. The one the
  killcam used is where his body faces; his shots go along the other one
  (the direction his "machine" sends with each move: the game fires along
  that one, which I found when building the bots). The killcam now looks
  along the direction he actually shot.
- The gun: missing since 0.5.352, when I stopped the game drawing the
  killer's body during his killcam. That step also gets his gun ready for
  the view, so it is back on. The double-drawing problem it was meant to
  stop is now refused where the game makes its drawing list (and now
  anywhere in the list, not just at the end).

## 0.5.355: killcam: you back in the picture (the "stand up" change undone)

- 0.5.354 tested by you: no loop (the game refused the double add every
  time, as planned: it was a crate-like object, type 3), but you still
  were not in the killcams.
- The log says the game drew you in every frame of every killcam, right
  in front of the killer. What changed when you vanished (0.5.353) was
  the "stand the fallen bodies up" change I made for the weapons: it sets
  three of your body's settings back to "alive", and with your body in the
  fallen state that makes it not show at all. It is turned off again.
- So this build should show you again (as in 0.5.351/352), still without
  your weapon in hand; the weapon gets its own fix next.

## 0.5.354: killcam: the drawing-list loop stopped at its source; you back in it

- 0.5.353 tested by you (3 killcams): all still off, and you were no longer
  in them at all.
- What the log showed: the drawing-list loop still happened in every
  frame of every killcam, and my safety cut from 0.5.350 then threw away
  everything after that point in the list for that frame: you were among
  the things thrown away. That is why you vanished (and likely why other
  things looked "off").
- Found exactly how the loop is made: the game adds a model to the end of
  its list; adding the model that is already at the end makes it point at
  itself. Now that second add is simply refused, so the list stays whole,
  nothing is thrown away, and the log names which object it was.
- Also undone: 0.5.353 kept too much of each player's current data during
  the replay; now only their current body and record are kept, the rest is
  as it was then.

## 0.5.353: killcam: bodies stand with their weapons; one more loop cause

- 0.5.352 tested by you: still no weapons seen, the moment you're killed
  looks off, and (from the log) the drawing-list loop still happened, now
  on a body that belongs to no current player.
- That body: a bot who died and respawned since gets a new body; the
  replay was handing him his OLD one, which by now is someone else's (a
  fallen body), so that body was drawn twice. Now every player keeps his
  current body; only his state, place, aim and pose of then are replayed.
- Weapons: your body's set-up logged alive and dead shows the game puts a
  dead body in a "fallen" mode (three settings change), and a fallen body
  is drawn without a weapon. In the replay, everyone who is dead now but
  was alive then is put back in the living mode for the length of the
  frame, so they should be drawn with their weapons, you included.

## 0.5.352: the killcam freeze's real cause fixed; your weapon looked into

- 0.5.351 tested by you (2-minute video, 3 killcams): no crash; "a lot
  better but still a bit off, and I don't see the weapon I'm holding".
- The freeze guard from 0.5.350 caught the cause in the act: one player's
  body was being added to the drawing list twice in the same frame. That
  is the killer: in the replay he is the camera (his gun and arms are drawn
  from his body), and his body was also drawn as a player. A spectator
  following someone never sees that person's body, so now it isn't drawn.
  That removes the loop at the root, and may also clean up what looked
  "off" around the view.
- Your weapon: when you die you drop it, and your body's set-up changes
  (it loses the weapon's parts). The replay restores your moving pose but
  not the weapon in your hands yet. This build writes your body's set-up
  to the log once alive and once dead, so the next build can put the
  weapon back exactly.

## 0.5.351: crash after a killcam fixed

- 0.5.350 tested by you: no freeze this time (and the killcam in your
  video looks right: the killer's view, his gun, you running at him), but
  the game crashed a little after a killcam, while you were shooting.
- Why (from the crash report): a part of the game that runs side by side
  with the drawing (it handles effects attached to objects) read some of
  an object's bookkeeping while the killcam had put the values of 4
  seconds ago there; those pointed at things that no longer exist.
- Now the killcam puts back only each object's state (what it is, where,
  how it moves, its angles and events): exactly what a Call of Duty
  snapshot carries and what CoD4's killcam replays. The game's own
  bookkeeping is never touched.
- Still to do: the red "hurt" tint of your own death shows over parts of
  the killcam, and the "Waiting for respawn" countdown over its end.

## 0.5.350: killcam freeze guarded (and caught in the act)

- 0.5.349 froze again in a killcam, in the same place as 0.5.347: the game
  walking, forever, its list of models to draw, because one model had been
  put in that list twice and the list became a loop.
- Now, before the game walks that list, it is checked; a loop is cut where
  it closes, so the game carries on instead of freezing. And the log says
  exactly which model it was and which object it belongs to
  ("killcam guard"), so the real cause can be fixed next.

## 0.5.349: killcam: you move again in it

- 0.5.348 tested by you: no freeze (fixed), "a little better but still
  needs work". The log showed your own recorded pose was not used at all
  in 348: the replay's new pose memory sits in a range a safety check of
  mine turned away, and a fallback then posed you frozen. Fixed: you are
  given your recorded, moving pose again, and the fallback never runs
  while the replay holds your body.
- Please tell me in a sentence or two what still looks wrong (for
  example: the camera's height or direction, who is where, a body doing
  the wrong thing, missing shots or effects), so the next fix goes
  straight at it.

## 0.5.348: killcam freeze fixed

- 0.5.347 tested by you: the game froze about 2.5 seconds into a killcam.
- Why (from the log): the freeze was the game going round and round a
  list of the models it draws, forever. The killcam had put back an
  object of then (a smoke grenade was being thrown) including the link
  to its model, but that model had been removed and its slot reused
  since; the game then put it into its list a second time, and the list
  became a loop.
- Now the objects of then are put back with their places, states and
  everything the drawing reads, but their links to models and the game's
  own records stay the current ones, so the game's lists stay sound.
- The log counts how often an object's model had changed since then.

## 0.5.347: killcam: every body in its real pose; no more standing corpse

- 0.5.346 tested by you: still off, and after dying your body stood stuck
  upright until you respawned.
- The standing corpse: since 0.5.341 the killcam's recorder posed your own
  body every frame (the game never does in first person) so it could be
  replayed, and it kept doing that after you died, over the game's own
  death pose. It now only does it while you are alive.
- Bodies in the killcam: the log of your test said only about 1 body in 4
  got its recorded pose; the rest kept their pose of now. The pose was put
  only when the body's skeleton matched the one of then exactly (a dead
  body has fewer bones than a living one; a bot who respawned has a new
  set-up). Now, for the length of the replay, each body is given the
  recorded pose with the skeleton set-up of then, and its own is put back
  after, so every body moves as it really did.
- Please record a video of the next test (a few killcams), so I can see
  exactly what still looks off.

## 0.5.346: the true killcam, step 2: everything of then, exactly

- 0.5.345's measuring worked (thanks for the test). It showed exactly what
  the game's drawing side reads when it draws a frame: the list of the
  match's objects (about 250: you and the bots, 25 others like flags and
  pickups, about 150 more like grenades, rockets, smoke and map objects),
  each object's own data, the players' records and their bodies. And what
  it writes itself (which shots and sounds it already played).
- The killcam now records ALL of that, every server frame (about 66 a
  second), for the last 4.8 seconds, and plays back exactly the frame of
  that moment: the same objects in the same places, the grenades and
  rockets that were flying, the smoke, everyone's exact pose and aim. No
  more blending between frames (that was the last piece of guessing).
- Still the same: the game's own drawing code draws it, from the killer's
  eyes, with his gun, his shots and their sounds.
- Not yet: the "Waiting for respawn" countdown over the end of the
  killcam; that is next once this is confirmed.
- If something looks broken: COD3_KILLCAM_OBJECTS=0 turns the new part
  off (players only, as 344), COD3_KILLCAM_BLEND=1 brings the blending
  back.

## 0.5.345: the true killcam, step 1: measuring (no play change)

- You asked for the real thing, as Call of Duty 4: an exact recording of
  what happened before you were killed, replayed by the game itself.
- How CoD4 does it: the server keeps the last seconds of the snapshots it
  sends (everything the player's game draws from); the killcam plays them
  back through the game's own drawing code. Nothing is filled in.
- What is different here: on the hosting PC the game's drawing side does
  not get snapshots at all; it reads the match's memory directly. So the
  recording has to hold exactly the memory the drawing side reads, every
  server frame, and the replay has to put exactly that back. To do that
  without guessing, this build MEASURES it: for a few frames it watches
  every read and write the drawing side makes to the players, the other
  objects (grenades, rockets, flags...), the players' records, bodies and
  network marks, and writes the list to the log.
- What you do: play one normal match with bots (at least 4) for a minute,
  get killed once so a killcam plays, then quit. You will feel a short
  hitch twice (30 s in, and 1 s into the first killcam): that is the
  measuring. Then send me the log (don't start the game again first).
- Next build: the recorder and the replay built on that list (every
  object, every server frame, no blending), replacing the current killcam.

## 0.5.344: killcam: you running, not just standing

- 0.5.343 tested by you: you stood up in the killcam, but did not move
  (no running).
- What the log showed: while you play, your body's pose IS recorded
  properly and moves (running, turning). In the killcam, the game writes
  your dead body's pose into place every frame, after the killcam had put
  your recorded pose there: that is why 341 showed you lying. 343 posed
  you itself right before drawing, which stood you up, but froze you.
- Now your recorded, moving pose is put in right before the game draws
  you, so the replay shows what you were really doing. One catch found in
  the log: your body has 96 bones alive and 92 once dead; the first 92 of
  the recorded ones are used (the log says so if it happens).

## 0.5.343: killcam: your body animated from how you moved (try 2)

- 0.5.342 tested by you: still lying on the ground, not running at him.
  The log shows the killcam itself working (you drawn, alive, health 100,
  the killer's shots and events played, the death text hidden): only your
  body's pose is wrong.
- Why (best reading): leaving your pose alone in 342 meant the game drew
  whatever your body's pose was right then, and right then you are dead,
  so: lying. Now, just before the game draws you in the replay, your body
  is posed from your recorded self (alive, moving), the same way the bots'
  bodies are posed every frame.
- The log now prints your body's pose numbers while you play (every 10 s)
  and before and after this step in the replay, so if you still lie down
  I can see exactly what the game is doing with your body.
- Same as before: close the game after the test and tell me before you
  open it again.

## 0.5.342: killcam: you running, not lying on the floor

- 0.5.341 tested by you: you were in the killcam, but lying on the floor.
- Why: your own body is never animated by the game while you play (you
  see through its eyes, in first person), so its pose stays the last one
  it had: the fall from your last death. 341 recorded that pose and put it
  on the replayed you. Now your own pose is left out: in the replay you
  are drawn like any other player, and the game animates you from how you
  actually moved (running, crouching, turning). Everyone else keeps their
  recorded poses.
- Log: the killcam's end line also says "your own pose left to cgame N
  times". COD3_KILLCAM_OWN_POSE=1 brings back 341's way, to compare.
- Note: the 341 test's log was replaced by two later starts of the game,
  so this fix comes from your description and the code. Please send the
  CoD3MP.log right after a killcam test next time (before starting the
  game again).

## 0.5.341: killcam: poses, shots, no death text; depth copy off by default

- Poses: every body in the replay now has the pose it had at that moment.
  A body's pose is its bone matrices (rig +112, from the entity's rig at
  +492), filled in place once a server frame; the replay moved bodies but
  each kept its pose of now (you: wounded, lying). The recorder keeps each
  player's bones with every sample, and during the replay each body is
  given its recorded bones (blended between samples) for cgame's frame and
  the scene, put back after. Your own body (never drawn in first person,
  never evaluated) is now evaluated by the recorder. COD3_KILLCAM_POSES=0
  turns this off.
- Shots: cgame plays an entity's events (shots, flashes, sounds) when its
  event sequence (+184) passes what it has played (+1000). The replay wrote
  the recorded +1000 back every frame, so no event was ever new. The replay
  keeps its own count now, so the killer's shots play.
- "You are critically wounded" no longer shows over the killcam: the death
  panel (sub_823A3690) is updated as hidden while it plays.
- F11 > Graphics > Optimizations "Depth copy on its own thread" is OFF by
  default (it crashed 0.5.340 multiplayer when aiming right after loading
  in). Saved under a new name, so a 0.5.340 settings file does not turn it
  back on.
- Log: "killcam: client N's pose", "killcam: replayed event", "killcam:
  death text hidden", and at the end "killcam: over (...): N events played,
  poses put N times".

## 0.5.340: merged GitHub pull request #2 (legoliamneeson's PC work)

- Brought in the pull request merged on GitHub (from legoliamneeson, built on 0.5.183): PC performance, stutter, picture and sound work, all on top of our 0.5.339.
  - **HDR mode** (F11 > HDR), on the RenoDX shader library and the Prism tone mapper (both MIT, in thirdparty/): tone mappers, peak/game/UI brightness and RenoDX's sliders.
  - **Optimizations**, each with its own switch in F11 > Graphics > Optimizations, so if anything looks wrong it can be traced to one: depth on its own thread, faster constants, shader load cache, staging uploads, texture pool and more; sharp shadows, fast shadow maps, smooth foliage edges, steady decals, effects resolution.
  - Sharper picture on Windows display scaling above 100% (the game window is now DPI aware).
  - Sound: a limiter instead of clipping; audio channels setting.
  - Log counters that stop counting once they have said their lines (they were counted forever).
- Everything of ours is kept: killcam, bots, mortar, killstreaks, the console-less window (we keep WIN32_EXECUTABLE ON for multiplayer: no extra cmd window), max players 32, sprint hold.
- No game data in any of it (checked).
- If the picture or sound misbehaves after this, F11 > Graphics > Optimizations: turn them off one at a time and tell me which one.

## 0.5.339: killcam: you, at last (the "followed player" rule)

- 338 confirmed: the Panzerschreck's explosion is back (the map's sound bank loads now), and the other players are drawn in the killcam. The log shows the game was drawing you too, every frame, but you were not seen.
- Why: the killcam shows the killer's view as a spectator "following" him would see it, and the game hides the body of the player being followed (you do not see the body you look out of). The followed player is named by his number, and during the killcam the killer's number points at YOUR place in the player list (the two places are swapped so the game takes the killer for you). So the game hid you, thinking you were the one being followed. The killcam now names place 1, which holds the killer: he is hidden, you are shown.
- Still to do: other players' poses in the killcam ("one guy looked like he was laying down while running"): their positions are from the replay but their body animation is still the live one. That needs the animation state recorded too; next after this.

## 0.5.338: killcam: everyone back in it (my sum was wrong); map sounds

- FOUND IT, and it was my mistake: 337's counts showed that during the killcam not one player went to the game's player drawing, nobody at all, not only you. Since 333 the killcam has marked every replayed player as "in the picture" with the game's frame number, but I read that number from the wrong address (a sum done wrong: 0x82C281D4 instead of 0x82C28274). So every replayed player failed the game's own check and none was drawn: not you, not the guy in front of you. That also matches "I can't see anyone else". Fixed: you, the killer's targets and everyone else he saw should be drawn where they were.
- Panzerschreck explosion and other missing sounds: on Merville the map's own sound bank was not loading (its explosion, melee, grenade throw, enemy gunshot, dying and ambience sounds were all missing from the log). The campaign sound banks loaded for the campaign guns and the mortar left too little sound memory for the map's bank, loaded last. The sound memory is raised from 36 MB to 64 MB.
- Still to come in the killcam: explosions, smoke and other effects as they were (they show as they are now; only players are replayed so far).

## 0.5.337: killcam diagnostic (why you are skipped)

- 336's log: during the killcam you ARE in the list of things the game puts in the picture and you pass its test, yet the game's player drawing never gets you. This build counts, every 30 frames of a killcam, how many things go through each step (and whether you do), to find the exact step that drops you. Nothing else changes.
- Bazooka sound: you heard it this time (336), so it was not the build; it may depend on the map (it was missing on Merville). Next time it is silent, tell me which map.

## 0.5.336: killcam: you in the picture (closing in)

- 335: the killcam looked clean, but you were still not in it. The killcam switch-over now also covers the exact routine that puts the players' bodies into the scene (in case the step 335 covered is not the one that runs it), and the log says, once per killcam, whether you are in that routine's list and whether you pass its test, with the numbers. With that line the last piece is certain.
- The blue "double" around the other player: in your video there is a pale, blurred figure in the smoke during the killcam. Noted; if it is a player drawn twice (where he is now and where he was), the same line will show it.
- Bazooka with no sound: the log shows your reload sound being sent as yours (so not the bots' sound filter), but the bazooka's firing sound is not found in the sounds loaded on Merville (BAZOOKA_FIRE_3RD missing). Being looked into next, separately from the killcam.

## 0.5.335: killcam: you in the picture (found it)

- 334's log settled it: the killer did turn onto you (3 degrees off his aim, 360 away, at the moment you died), but the game was never asked to draw you. The game adds the players' bodies to the picture in a second, separate step of drawing a frame, and the killcam's switch-over only covered the first step (your view, the gun). In the second step everyone was where they are now and you were still "this machine's player", so left out. The killcam now covers both steps: you, and everyone else, where you were.
- The bot's view hugging the walls (334's video): the killer was a bot walking along the houses with his aim held on you through the walls (334's log: 24 to 63 degrees off, turning onto you as you came round). That is how the bots move and aim, the killcam is showing it faithfully; making bots look where they walk instead is a bot change for later if you want it.

## 0.5.334: killcam: you in the picture (try 2, with the log to prove it)

- 333's videos: the killer's gun, his hands and his view are right now, but you are still not seen. One more thing found in the game's player drawing: a body is left out when a certain flag is on it, which your own player may carry as this machine's player. It is now taken off the replayed you (only while you were alive in it).
- To settle it either way, the log now says, twice a second during the killcam, where you were from the killer's eyes (how far, and how many degrees off his aim), and whether the game was asked to draw you. So if you are still not seen, the next fix is exact.

## 0.5.333: you in the killcam, the moment you are shot; mortar on level ground

- Killcam (332: smoother, you saw them walk, but not yourself or the moment they shot you):
  - The game only draws the players that are in your own last update from the server, and your own player is never in it (you are the one looking). So the you of 4 seconds ago was there but never drawn. During the replay every recorded player is marked as in it, so you are drawn, and so is anyone the killer saw, even if you could not see them from where your body lies.
  - Players are now recorded after they die too, so the replay shows you hit and going down, not frozen standing at your last moment alive.
- Mortar (332: arms in view, standing to the left of it): the log shows the tube was put down 58 units above your feet, on a ledge in front of you (the ground search took anything up to 60 above), so your view sat low and off under it. Now the tube only goes on ground within 20 above or 60 below your feet, else at your feet' height. The log says where ("mortar: ground under the tube").
- Bots throw smoke 5% less often (every 47 to 68 s at most, was 45 to 65 s), every class that carries smoke.
- From 332's log: Rusher 2's artillery works (he called two strikes and they came down), and his kills took 10, 15, 20, 25 s off his wait. You played a scout but none of your kills took anything off: the log now says why each time ("nothing to take off": probably your artillery was already ready, unused).

## 0.5.332: the killcam as the killer (his gun, his shots, you in the picture)

- 331's killcam played its full 4 seconds but felt like "watching a ghost": no gun, no muzzle flash, and you could not see yourself being killed. Dug into how the game draws a frame on the host PC and found why:
  - The gun in your view is drawn from your own player on the server: his state and also his body's model and animations. 331 gave your player the killer's state but your body stayed your (dead) body, so the gun never showed and nothing flashed.
  - Every player's body is drawn except your own player's (you don't see yourself in first person). Your past self was put back where you stood, but as "you" it was never drawn.
- So now, only while each killcam frame is drawn, the game is told the killer is you: your player's place in the game's list holds the killer, and the killer's place holds you. The game then draws exactly what he saw: his view, his gun with its own animations and muzzle flashes, his body left out, and your body drawn like any other player, where you were, alive, until he shot you. Everyone else is where they were, as in 329. Put straight back after each frame; nothing in the real match changes.
- Bots switching to a special (Rusher 2's binoculars, rifle grenades, mines, packs): 331's log showed the switch never finished (no binocular fire, no barrage, the rounds unspent): the bot's orders kept asking for the gun in his hands while the switch was under way. They now ask for the weapon being switched to until it is up.
- COD3_KILLCAM_AS_KILLER=0 goes back to 331's way, if this misbehaves.

## 0.5.331: artillery earned faster with kills

- The class artillery (the scout's and Rusher 2's, about 130 s to come back) now comes faster with every kill: each kill takes time off that player's wait, more for each kill in a row this life (10 s, then 15, 20, 25, and 30 s at most per kill). Everyone's, people and bots. On screen for you: "ARTILLERY -15 s (60 s left)", or "ARTILLERY READY". The killstreak artillery is not changed.

## 0.5.330: the killcam plays all the way through

- 329's killcam stopped after half a second and went back to you lying dead (you saw it, and the log says "over (respawned)" each time). My mistake: while the replay draws, your player is shown alive as you were, and the check for "has he respawned?" read that replayed you instead of the real one. It now reads the real one, so the replay runs its full 4 seconds.
- From the same test's log: rifle grenades are now given (riflemen and Demolitionists had 3-4 each) and the bots took them out 26 times, but they were put away again at once: every bot's orders always asked for the first gun of his kit. Now a bot holding a special (rifle grenade, mine, pack, binoculars) keeps it until he is done with it.
- Rusher 2 showed up (one a side, Sten, Bren and Mills bombs all given), but the same thing kept the Bren in his hands; he now gets the Sten on the move and the Bren holding the objective.
- Rusher 2's artillery: he had no spotter's binoculars. His class's artillery is now started for him each life, and when it is ready he is given the spotter's binoculars with a round, uses them, and waits for it to recharge.

## 0.5.329: smoother killcam with everyone in it, Rusher 2, rifle grenades that work

- Killcam (you saw it in 328, a bit laggy, and only the killer's view was from the past):
  - Recorded every server frame now (it was 30 times a second from the drawing, out of step with the game), and the killer's place and aim are blended smoothly between frames instead of jumping.
  - Everyone else is shown where they were at that moment too, you included (alive, as you were when he shot you). Their places and turning are blended the same way.
  - The killer's own body is hidden during it (you are looking through his eyes).
  - COD3_KILLCAM_OTHERS=0 keeps everyone else where they are now, if that part misbehaves.
- New bot kit, Rusher 2 (one a side), exactly as you asked: the Sten, the Bren as his second gun, Mills bombs, built on the scout so his class special is the artillery called with the binoculars (his own, not the killstreak one).
  - Very aggressive: a flanker who presses every fight.
  - His errands go round: push up to the enemy's spawn, clear the low ground (inside, room to room) toward them, back to the objective and hold it a while with the Bren, a flank round the outskirts, then again.
  - Takes the Bren when holding the objective, the Sten on the move.
  - Artillery: someone in sight 900 to 4500 away and no one within 600, he takes out his binoculars, puts them to his eyes, marks the spot and goes back to his gun (at most every 30 s, when it is ready).
  - In bot_kits.txt as "rusher2"; the grenade column now also takes "mills".
- Rifle grenades: 328's log showed no rifleman or Demolitionist bot had any (the class special is never given to bots). They are now given once a life, and the bots take them out by name (the way they take out mines and packs), fire, and go back to their gun.
- Binoculars with the sides missing: that is the old on-and-off problem from 0.5.244-252, not something from these builds (in 328 it happened before any killcam). Next time it happens, a screenshot with them up, and whether it is every time or only sometimes, helps.

## 0.5.328: killcam replay, second try

- 327 handed the killer's recorded state to the game, but you still saw your own body: on the host PC the game draws your view from your player's state on the server itself, not from what it received. Now, only while the picture is being made, your player on the server holds the killer's recorded state, and your real one is put back straight after, before the match moves on. So the view, the gun in hand and the aim should be his.
- Rifle grenades: 327's bots fired none. This build logs what each rifleman and Demolitionist has for rifle grenades (owned or not, rounds), so the next fix is exact.

## 0.5.327: the real killcam replay (first try)

- The killcam is now replayed by the game itself from your killer's own recorded state: meant to show his first-person view with his gun in hand, his aim moving the way it did and his shots, for the last 4 seconds before you died (first try: tell me what you see). Before, only the camera was moved to where his eyes were.
- Found how the game feeds your screen: on the host PC, the picture is made from your own player's state copied straight from the server. During the killcam it is handed the killer's recorded state instead, marked as "following" him (the way a spectator watching him would see it), so nothing in the real match is changed.
- Still the same: the other players (and your own body) are where they are now, not where they were. That is the next part.
- If anything looks broken, COD3_KILLCAM_REPLAY=0 goes back to 0.5.326's camera-only killcam.

## 0.5.326: Demolitionists drive, bots use their rifle grenades, killcam names the killer

- Killcam: the HUD (the "You are critically wounded" text and the rest) is hidden while it plays and comes back after; the banner names who killed you ("KILLCAM - Bot 7").
- Two Demolitionists a side (was one), and more riflemen: an extra rifleman line near the top of bot_kits.txt, so more bots carry rifle grenades.
- Demolitionists play aggressively (rushers, pressing the fight whenever not badly hurt) and throw their sticky bombs more often (every 5 to 8 s instead of 9 to 14 s).
- Demolitionists use vehicles: they take one even with a fight going on (unless an enemy is within 500), always as the driver, drive it to a far part of the map (over 1500 away, a different kind of place each time), get out there to fight, and take a vehicle again later.
- Every bot that has a rifle grenade (riflemen, Demolitionists) now switches to it and fires it at enemies it can see 400 to 2200 away, with the arc raised for the distance, never near its own side; then it switches back to its gun. About every 10 to 16 s, Demolitionists every 6 to 9 s. Log: "fires a rifle grenade".
- Note: bot_kits.txt in the game folder was updated (the Demolitionist and rifleman lines moved to the top; your other lines kept as they were).

## 0.5.325: killcam groundwork 2 (diagnostic)

- No change in play. Every 10 s the log records where the game keeps the players it draws, for the killcam's next step: everyone shown where they were (you alive and moving, the killer's shots), not where they are now.

## 0.5.324: killcam from the killer's real eyes

- 323 moved the view, but to the wrong place: a bot's record gave its height as 0 and its aim as level, so the camera sat at the killer's feet. The killcam now takes the killer's position from the game's own entity (and a bot's aim from it too).

## 0.5.323: killcam shows

- 322 played the killcam but nothing showed: it changed the game's camera settings after the view was already made from them. Now the killer's view goes into the view itself, just before the game finishes it.

## 0.5.322: KILLCAM (first version)

- When another player or a bot kills you, "KILLCAM" comes up and you see the last 4 seconds before your death through the killer's eyes, at real speed, then the normal death view and respawn.
- First version: the camera replays the killer's view; the other players and effects are shown where they are now, not where they were. On the host's PC (and offline with bots); the joining PC comes later.
- COD3_KILLCAM=0 turns it off.

## 0.5.321: killcam groundwork (diagnostic)

- Nothing changes in play. The log records how the game builds your view (its view modes when you die and respawn, the camera's numbers, where other players sit on the client side), so the killcam can be built on it in one go.

## 0.5.320: co-op v36 from the friend

- Co-op: the host warns on screen when the joining PC runs an older build (both need the same CoD3.exe); a joiner restarted after the host dies no longer sits in an unskippable film (the host says his film is over every second, and after 15 s the joiner's own A ends it). Messages unchanged from v35.

## 0.5.319: the mortar camera where the campaign puts it

- Measured from the screenshots: the campaign camera at the mortar is not your eye. It sits right behind the tube (about 15 behind the muzzle, a little over it), 3.4 times nearer than 318's view. Now the same.
- No arms hanging in view at the mortar (they come back when you put it away).
- The launch glow and smoke at the muzzle go away after 1.5 s.
- The view no longer changes after the first shot.

## 0.5.318: the mortar view exactly as the campaign

- Your eye now sits exactly where the campaign puts it over the tube: 48 units behind it and 49 above its base (the campaign's tube stands on a ledge 17 units up; here the view comes down 17 instead).
- No camera jumps: RT never starts the campaign's loading step.

## 0.5.317: mortar lined up with the campaign (measured)

- Measured in the campaign at Laison: you stand (no crouch), the tube is 48 units straight ahead of you, facing exactly where you face. The multiplayer mortar now does the same; the tube sits on the ground, so you look a little more down onto its top.
- The round comes in from your right, over the muzzle, then down the tube.
- The tube no longer stands on top of you (it pushed you aside in 315).

## 0.5.316: the campaign mortar measured

- Campaign only: while you are at a mortar (Laison), the log records exactly where the game puts you, your view and the tube ("mortar probe:" lines), so the multiplayer mortar can be lined up to match. Nothing in play changes.

## 0.5.315: mortar without camera jumps; co-op v35

- Mortar: the campaign's loading step is off. It sent the camera across the map at every shot and never showed arms in multiplayer. You crouch over the tube, look level, and the round is fed in and fired by RT.
- Co-op v35 from the friend (smoother soldiers, grenades, deaths and weapon drops, sprint): both players need this build.
- MAKE-FRIEND-UPDATE.bat also makes a source zip.

## 0.5.314: mortar camera stays put when firing, no black screen (test)

- The campaign mortar's loading step pressed and held one of your buttons after loading (how the campaign fires its round). In multiplayer it never let go: the camera flew off while firing, and putting the mortar away mid-load could leave the screen black. That press is now left out.
- A put-away no longer restarts the mortar the next time you set it up.
- Camera a little lower again: mortar_view default -8 0 -18.

## 0.5.313: mortar aim steady after the first shot (test)

- After each shot the mortar's interaction goes back to its idle step, so the next RT loads again and the aim ring reads the same as on the first shot (it stayed stuck in the loading step before).
- The log shows the aim (pitch, yaw, range) once a second.

## 0.5.312: mortar feed from the hands, lower camera, no screenshot lag

- The round now comes up from your hands, over the muzzle, then slides in and fires.
- The camera at the mortar is lower (shows more of the tube): mortar_view default -8 0 -14.
- Screenshots no longer lag the game: the full frame log and per-draw pictures (the big -draws.bin files) only happen with COD3_FRAME_LOGS=1.

## 0.5.311: no crash; the round fed into the tube (test)

- 310's crash fixed: the round is no longer put in the hands (its model breaks the arms' moves in multiplayer).
- RT now feeds a round: it appears over the muzzle, slides in, and then fires (about 0.8 s). Everyone sees it.
- mortar_view takes two more numbers, the muzzle's forward and up (default 24 42), to line the round up with the tube.

## 0.5.310: the mortar round in your hands (test)

- At the mortar you hold the campaign's own round (mortar_shell) on the multiplayer's interaction arms, so the loading moves should show the round going into the tube.
- If it crashes: set COD3_MORTAR_ROUND=0 to go back to bare arms.

## 0.5.309: mortar camera back and down (test)

- At the mortar the camera sits a little back (8) and down (6), through the interaction's own view offset.
- New console command: mortar_view <forward> <right> <up> moves it live.

## 0.5.308: mortar explosions back (test)

- Each round's landing again sets off the panzerschreck's and the frag's explosions (seen before), plus the campaign round's own. The round's explosion data is logged once to find why it shows nothing.

## 0.5.307: mortar faces your aim, you crouch at it (test)

- The tube turned a quarter left: its muzzle now leans away from you, the way you aim.
- You crouch when the mortar goes down (the campaign's eye height at it) and stand back up when you put it away.
- mortar_yaw takes a fifth number: 1 crouch at the mortar, 0 stand.

## 0.5.306: mortar tube stands where you stand (test)

- The campaign puts you right on the tube's spot (its offsets are 0 forward, 0 right, eye 45 up), and the arms are drawn in the tube's frame. The tube now stands at your feet and turns with you.
- mortar_yaw now takes four numbers: facing, forward, right, up (e.g. mortar_yaw -90 0 0 0), so the tube can be lined up live.
- Once a second while on the mortar, the log shows what the interaction does with the view and the arms ("mortar look").

## 0.5.305: mortar placed where the campaign stands you (test)

- The tube is placed using the campaign turret's own player offsets (mortar_81mm_turret forward/right/up), so the arms' moves line up with it. The values are logged.

## 0.5.304: mortar arms on the multiplayer's own interaction arms (test)

## 0.5.303: mortar crash fix 2 (test)

- The campaign round's arms are set up before you hold it (the crash).
- Console: mortar_yaw <degrees> turns the tube while you are at it; tell
  Claude the number that faces your aim.

## 0.5.302: mortar arms fix (test)

- At the mortar you hold the 81mm round (the campaign's), so its arms
  play the moves; putting it away ends the interaction cleanly (no more
  camera in the void).

## 0.5.301: mortar crash fix (test)

- The campaign mortar's first step is campaign-only and crashed
  multiplayer: it now begins at its second (holding the round). The tube
  faces the way you aim.

## 0.5.300: campaign mortar arms (test)

- Setting up the mortar killstreak now starts the campaign mortar's own
  interaction: its arms and the loading of the round. Test: streak_give
  mortar, d-pad down, d-pad right, watch your hands, press RT.

## 0.5.299: the campaign mortar visible

- spawnvehicle mortar (or mortar_81mm) puts down the campaign mortar with
  its model.

## 0.5.298: the campaign mortar's files, done properly (test)

- The game now makes small copies of Laison's files on your PC (the first
  time a map loads) holding only the mortar's own files: no Laison level
  data, so the map draws as normal. Test: the world draws, then
  spawnvehicle mortar_81mm on Eder Dam.

## 0.5.297: maps draw again

- Laison's file is off again (the world drew invisible with it, loaded
  before or after the map). The mortar killstreak works as in 0.5.290.

## 0.5.296: Laison's file after the map (test)

- Laison's file is loaded after the map instead of before, so the map's
  own textures are found first. Test Fuel Plant's floors and walls, and
  spawnvehicle mortar_81mm on Eder Dam.

## 0.5.295: Fuel Plant fixed

- Laison's file is no longer loaded (it made Fuel Plant's floors and walls
  invisible). The mortar killstreak works as in 0.5.290.

## 0.5.294: Fuel Plant's invisible world (fix attempt)

- Laison's mortar files are no longer searched when the map draws its
  floors and walls.

## 0.5.293: weapon limit test switch

- COD3_WEAPONS_PAST_63=1 lets the campaign mortar's turret and round use
  the game's spare weapon room past 63 (for full maps like Fuel Plant).
  Test only.

## 0.5.292: weapon limit fix

- The game holds at most 63 weapons on a map. Fuel Plant already uses
  all 63 with the campaign guns, and the mortar's additions went past it
  (why your gun stayed out). Nothing goes past 63 now; the binocular
  stand-in hands are gone.

## 0.5.291: the campaign mortar's files (test)

- The campaign mortar's own files are loaded from Laison: the 81mm as a
  vehicle, its turret, its round, the arms' moves and effects.
- The mortar killstreak's rounds use the campaign's own launch flash and
  dirt explosion when they load.
- Test: spawnvehicle mortar_81mm in the console, and try to use it.

## 0.5.290: Fuel Plant plane

- 4% higher again (11% over the other maps' height); its circle round the
  whole map again.

## 0.5.289: mortar hands fixed, Fuel Plant plane

- The mortar's hands and round should now show (0.5.288 put the gun away
  but drew nothing in its place).
- Fuel Plant: the UAV plane flies 7% higher and its circle is a little
  further inside the map.

## 0.5.288: mortar hands

- At the mortar your gun is put away: you hold an 81mm round in the
  campaign mortar's own arms pose. Your gun comes back when you put the
  mortar away.

## 0.5.287: more memory for maps

- With big memory on, maps now get 608 MB to load into instead of 495
  (about 113 MB more), so the campaign guns, both UAV planes and the
  mortar all fit together, with room to spare for more.
- The mortar's tube is back in front of you.

## 0.5.286: loading fix

- 0.5.285 froze loading Eder Dam: the mortar's model file did not fit in
  memory beside the campaign guns. It is left out for now: the mortar
  works, but its tube is not drawn in front of you.

## 0.5.285: the mortar killstreak

- 4 kills in a row earns a MORTAR (it shows in the killstreak box, below
  the UAV). Pick it with d-pad down, set it up with d-pad right.
- The campaign's 81mm mortar is set down in front of you and you stay put
  behind it. Turn to aim it; look up to fire further, down to fire closer
  (600 to 5000 units). A red ring on the screen shows where the round
  lands; the panel at the bottom shows rounds left and the range.
- RT fires: 10 rounds, one every 1.6 seconds. Each round is loaded, fired,
  flies its arc, whistles in and explodes.
- Much bigger blast than a grenade: a sure kill close in, damage out to
  more than twice a frag's reach, less through walls. It never hurts you
  or your own team.
- D-pad right puts it away; rounds left are kept for later. Dying or
  running out puts it away too.
- Works for both PCs: the other player sees your mortar, the rounds and
  the explosions. Both PCs need this build.
- Console: streak_give mortar (to test).

## 0.5.284: the Demolitionist bot class

- New bot loadout, saved in bot_kits.txt as you described it:
  DEMOLITIONIST: main weapon the Bren, second main weapon the MG42 (in
  place of the pistol), sticky bombs as his grenades, and rifle grenades
  (built on the rifleman, whose special they are). Weight 2, so a couple
  spawn in every match (at least one a side).
- bot_kits.txt can now give any kit a second main weapon and sticky bombs
  (two optional columns after smokes).
- Fix for 0.5.283: bots in the 3 new slots would have spawned without
  their kit; they get it now.

## 0.5.283: full 32 (test)

- Bots can now use the last 3 player slots the game normally keeps for
  split-screen players on your PC. FILL at 32 players gives 31 bots solo
  (32 in the match), or 30 bots with your buddy (32 total).
- These slots are filled last, so anyone joining in the lobby still gets
  in first. Someone joining mid-match when all 32 are full can't get in
  yet (a bot would have to leave for them: later).
- Test solo first: 32 players, BOTS: FILL. Watch for anything odd on YOUR
  screen (split screen, camera, HUD, controls) and count the scoreboard.

## 0.5.282: bots keep learning; scouts look out of windows

- Lookouts: every long-range kill (1100+ away, yours, your buddy's or a
  bot's) teaches the bots where the shooter stood and which way he looked
  (a window, a ledge, a roof edge). The same spot again scores it higher.
  Saved per map, so it builds up game after game.
- Snipers go to the best-scoring lookouts for their watch and watch that
  way first. Scouts and snipers walking past a lookout stop 4-6 s, sweep
  the view out of it, then move on (they don't camp in it).
- Sharpening: at the start of each game the old lessons (where bots died,
  got kills, got stuck, and the lookout scores) count 85%, so recent games
  matter most and the bots keep adapting.
- Includes 0.5.281 (killfeed colours by team).

## 0.5.281: killfeed colours by team

- Both names in a kill line are coloured from YOUR point of view: your
  team green, the enemy red. Your teammate kills an enemy: green killed
  red. An enemy kills you or a teammate: red killed green. (Before, the
  one who died was always red, so you saw "red killed red".)
- Each PC colours it for its own player, so your buddy sees his side green.

## 0.5.280: plane higher on Poissons, circle a little tighter

- Poissons: the plane flies another 700 units higher.
- Every map: the UAV's circle is 2% smaller, a bit in from the outskirts.
- 279 check (from your log): other players' reload / weapon-raise sounds
  were dropped (20+ of them), your own were still played.

## 0.5.279: no more bot reloads in your ears; Poissons plane higher

- The bots' reload, rechamber and gun-handling sounds are no longer
  played as if they were yours (their reloads went through the very same
  first-person sound as your own). Their gunshots are untouched.
- Poissons: the UAV plane flies higher (it was only ~200 above the
  ground).
- 278 worked: the lobby BOTS setting filled the match, and the anti-armor
  bots were given mines and laid them.

## 0.5.278: bots get mines; reload sound trace, take 2

- Mines: no bot class carries mines, so none were laid. Anti-armor bots
  and campers (not medics or support) now get mines each life and put
  them down near the flag or where they hold.
- Reload sounds: the first trace looked in the wrong place. This one
  watches the reload animations' own sound events. Play a few minutes
  with bots reloading near and far from you, then I read the log.
- Smokes left as they are (you said they're perfect now).

## 0.5.277: bots as a lobby setting + bot behaviour pass

- BOTS is now a match setting: System Link -> Advanced Options, turn the
  page (L1/R1) to GAME MODES, BOTS: OFF, 2, 4, 8, 12, 16, 22 or FILL (as
  many as the player limit leaves). The host's game adds them when the
  match starts; everyone else gets them from the host. Anyone who hosts
  with our build can use it.
- Far fewer smoke grenades: one at most per bot (none for heavies and
  anti-armor), and 45-65 s between a bot's smokes (was 16-26 s).
- More bazookas / Panzerschrecks: three anti-armor kit lines now.
- Mines: bots that carry mines put them down near the flag (or where
  they camp), up to three a life.
- More bots play the objective (38% of each side, was 25%), fewer camp.
- Diagnostic for the bot reload sounds you hear as your own: play a
  minute near and far from bots, and I read the "reload sounds" lines.

## 0.5.276: two-PC UAV fixes

- Every plane message reached your buddy's PC twice, so every hit
  counted double on his copy (that's why your second plane dropped to
  one rocket on his screen). Messages are now counted once.
- His own UAV: kills are counted on the host, so his PC never knew he
  had earned one. The host now tells his PC; he gets "UAV EARNED" and
  d-pad right calls it, and everyone sees his plane.
- Both PCs must use the same player limit (both 24 or both 32): the
  first try had you at 32 and him at 24.

## 0.5.275: the UAV plane for everyone in the lobby

- System Link is peer to peer: every PC runs its own copy of the game
  world, so the plane only existed on the PC that called it. Now that PC
  tells the others, and each PC flies its own copy on the same circle:
  everyone sees it, hears its engine and flybys from where THEY stand,
  and sees the hit flashes and explosions.
- Hits are shared: your buddy's bullets on his screen take health off
  the plane on yours too, and when it goes down, it goes down for all.
- 32-player prep: the bot copies sent to a joiner now go up to 30 bots
  (was 20) and the tables for them hold 32 (were 24: would have
  overflowed at 32).
- BOTH PCs need this version: after it builds, run MAKE-FRIEND-UPDATE
  and send your buddy the update.
- Test plan: 1) a 24-player game with your buddy: both of you call UAVs,
  see/hear each other's plane, shoot it down. 2) then 32 players (F11)
  and send me BOTH logs: the game's own network code still has a few
  24-player spots I can only fix from what the two logs show.

## 0.5.274: see your hits on the plane

- Bullet hits: a small flash on the plane where the bullets land.
- Bazooka / Panzerschreck hits: an explosion on the plane.
- Shot down: it explodes and puffs smoke as it starts to fall, then
  blows up again (with smoke) where it hits the ground.
- (Includes 0.5.273: your engine sound + crash sounds.)

## 0.5.273: your engine pick + crash sound

- Engine: ARPL_P47_2 restarted every 5.45 s (your pick) is the default.
- Shot down: an explosion when the killing hit lands, and a crash
  explosion when it hits the ground 2.5 s later.

## 0.5.272: steady engine + flyby only when it passes

- One steady engine sound for the plane from the moment it arrives;
  the swoosh flyby only when the plane passes near you.
- If the engine stops after a few seconds, tune it live in the console:
    plane_engine ARPL_P47_1 5000   (start it again every 5 s)
    plane_engine ARPL_P47_2 0      (another engine, once)
  Tell me which sounds best and I'll make it the default.

## 0.5.271: Stuka for the Germans (try), cleaner plane sound

- 270: the plane sound works! The "lots of engines" were 6 different
  flyby recordings started 3 s apart, playing over each other. Now one
  flyby every 7 s.
- Stuka: the game only finds models from its campaign file when that
  file is loaded early, the way the very first UAV build did it (the one
  where you saw a plane off the map: that was the Stuka). Loaded early
  again; the German UAV uses the Stuka when the game finds it, else the
  Typhoon. Test: be American, streak_give enemyuav.

## 0.5.270: plane sound, the real fix (hopefully)

- Found why every plane sound was silent: the game only plays sounds
  through its list of "sound events", and multiplayer has no plane event,
  so the request was thrown away. The plane's flybys now ride on a rifle
  shot event with the sound swapped for the plane's (the same trick that
  makes the Lee-Enfield shot work).
- Test: streak_give enemyuav and listen. Also plane_model stuka_flying
  once (it logs which Crossroads models the game can find).

## 0.5.269: Stuka fix try + bot sound tracking

- The Stuka was never found by name, so the game kept the Typhoon on it.
  Its archive was last in a list that holds at most 32; it now goes
  earlier. Test: plane_model stuka_flying (you should see a Stuka now).
- Bot reload sounds: this build logs, for each reload sound, whose player
  it belongs to and how far away. Play a minute with 2 bots near and far.
- Plane sounds: every P-47 sound is silent even though the rifle shot
  plays the same way: their group in the campaign sound file is likely
  never loaded. Next on the list.

## 0.5.268: try any sound by name

- plane_sound name ARPL_P47_2b now   plays that sound right away (any name)
  Try: ARPL_P47_1b, ARPL_P47_2b, ARPL_P47_3b, ARPL_P47_4b, ARPL_P47_1

## 0.5.267: Stuka test command

- plane_model stuka_flying        puts the Stuka right in front of you
- plane_model stuka_flying clear  same, with one suspect flag forced off
- plane_model v_Typhoon           the Typhoon the same way, to compare
  Tell me which ones you can see.

## 0.5.266: plane visible for both teams; sound test works without bots

- The German Stuka never showed up (it flew its circle, invisible). Both
  teams now get the Typhoon, the plane you've already seen working.
- 265's sounds waited for a gunshot, so with no bots nothing played.
  Now they play anyway after a moment.
- Test (no bots needed): type  plane_sound shot me now
  You should hear a rifle shot. Yes or no tells us where the problem is.

## 0.5.265: plane sound, new approach

- 264 test: the whole-map circle and the height are good. None of the
  plane_sound commands made a sound, not even the flags' sound, so the
  problem was WHERE the sound was played from, not which sound.
- Now the plane's sounds are played from inside the game's own gun sound
  code, the same way our scoped Lee-Enfield shot is played (and you hear
  that one). Flybys play at your own player by default.
- Test: call a plane (streak_give enemyuav) during a match with bots
  shooting. Listen for flybys every 3 s. Check: plane_sound shot me
  (plays a rifle shot through the same path; if you hear that but no
  flybys, the flyby names are the problem, not the path).

## 0.5.264: UAV circles the whole map; height fixed; sound test command

- Whole map: the circle is now worked out from the map's spawn points
  (they cover everywhere play happens), not from where the players are.
- Height: it was set from the highest player, so with you in noclip it
  went higher each call. Now it's the players' average height + 1400.
- Sound: instead of another blind build, a console test so we find the
  answer in one session. Call a plane (streak_give enemyuav), then type:
    plane_sound flag origin   (a sound we KNOW you hear, the flags')
    plane_sound flag me
    plane_sound flag bot
    plane_sound flyby plane
    plane_sound flyby origin
    plane_sound flyby bot
    plane_sound engine bot
  Each plays once right away (stand near a bot for "bot"). Tell me which
  ones you heard. The plane's flybys then keep using the last choice.

## 0.5.263: fixes 262 (plane gone, bazooka silent)

- 0.5.262 broke two things, both mine:
  - The plane disappeared on Merville: my "bring it down on Poissons"
    rule was firing on every map and flew it down into the trees. Now the
    plane flies at its normal height everywhere, and lower only on Poissons.
  - Your bazooka went silent: the engine loop I added used a setting the
    game didn't expect, and it stopped other sounds. Taken out.
- Sound: I found exactly how the game plays the flags' flapping sound
  (which you hear). The plane's flyby sounds are now played that same
  way, from a point near you in the plane's direction.
- Rocket hit markers: the X shows when the rocket reaches the plane.

## 0.5.262: UAV engine sound (looped, like the flags), rocket hit markers

- 0.5.261: still silent; you shot it down with the panzerschreck.
- Sound: single sounds started by my code never reach your speakers.
  Looped sounds do (the flags flapping on the maps are played exactly
  that way). So the plane now has its P-47 engine as a loop, coming
  from the plane's direction near you, and getting quieter as it flies
  further away.
- Rocket hit markers: a rocket hit used to count the moment you fired
  (so the X flashed while you were still watching the rocket fly). Now
  it counts when the rocket would reach the plane, so the X shows then.
- Still to come: an explosion when a rocket hits and when it crashes.

## 0.5.261: UAV plane sound, try 3

- 0.5.260: you saw the plane on Poissons (fixed), still no sound.
- The flyby sounds now play the same way the game's sounds that you DO
  hear are played (the scoped Lee-Enfield's shot, for example): on your
  own player, whenever the plane is within reach. They won't come from
  the plane's direction yet, but you should hear them while it's near.
- Test: any map, streak_give enemyuav, listen while it circles. Then, if
  you have time, load Merville and then Poissons in the same session
  (the crash from before) and tell me if it still crashes.

## 0.5.260: UAV plane on Poissons, try 2

- 0.5.259 on Poissons (fresh start): no crash, the plane was made and
  flew its circle, but you never saw it. The game marked it as outside
  the map's world there (Poissons' sky is low: it's a town). Now, when
  that happens, the plane comes down a bit at a time until it's back
  inside, staying at least 300 above the highest player.
- Test again: fresh start, Poissons, streak_give enemyuav. Did you also
  hear the flybys this time (on any map)?

## 0.5.259: UAV plane sound you can hear; Poissons crash avoided

- 0.5.258 on Merville: circling, hit markers, damage all working.
- Sound: the flybys were playing, but on the plane itself, thousands of
  units away, too far to be heard. Now they play from a point near you,
  in the plane's direction, so you hear it from where it is in the sky.
- Poissons crashed (the graphics side ran out and hung) when you went
  there after Merville with the plane files loaded. The plane stays on
  Poissons for now; test: start the game fresh and go straight to
  Poissons. If that works, the problem is only memory left over from the
  map before, and I'll clear that on map change.

## 0.5.258: UAV hit markers, shooting it really works, flyby sounds

- 0.5.257 on Merville: perfect circle (thanks). But no shot ever counted
  (the game doesn't let bullets hit a plane made this way) and no sound.
- Shooting it: now every shot anyone fires is checked against the enemy
  plane's position: a shot that passes through it counts. About 15 rifle
  or MG hits brings it down; a bazooka/panzerschreck hit counts as 300 of
  its 600. Bots' shots count too.
- Hit markers: a white X flashes in the middle of your screen each time
  one of your shots hits the enemy UAV.
- Sound: the P-47 flyby sounds (from the game's own sounds) now play on
  the plane every 3 seconds while it's up.
- 5% slower again.
- Test: streak_give enemyuav, then shoot it.

## 0.5.257: shoot the UAV down; bigger circle, a bit slower, engine sound

- You confirmed the plane circles. Changes from your notes:
  - Bigger circle: it now goes round all the ground the players have
    covered on the map so far (not just where they are right now).
  - 5% slower.
  - Engine sound: the P-47's engine loop from the game's own sounds,
    attached to the plane so it comes and goes as it flies past.
- Shoot it down: the enemy's plane takes damage from bullets and
  explosions (rockets count extra). About 12 rifle hits, 20 MG hits, or
  one rocket plus a few bullets. Your own team can't hurt it. When it's
  shot down it dives into the ground and the UAV it was flying for ends
  ("YOUR UAV WAS SHOT DOWN" / "ENEMY UAV SHOT DOWN").
- Test: console streak_give enemyuav puts the OTHER side's plane up so
  you can shoot it (your own team's can't be hurt). streak_give uav then
  d-pad right is still your own.

## 0.5.256: plane check (no play change)

- 0.5.255 tested by you on Guns: the map loaded fine, the UAV ran, but
  you saw no plane. The log says the plane was made and told to fly, but
  not where it actually went. This build writes its real position every
  2 seconds so I can see what it does. Same test again please: console
  streak_give uav, d-pad right, look up for 30 seconds.

## 0.5.255: fixes the freeze loading a map; Allies' plane is now the Typhoon

- 0.5.254 froze while loading Merville. The two plane files plus the
  campaign guns' files were too much memory for the biggest maps: the
  last campaign guns file never finished loading. Two changes:
  - The Allies' plane is now the Hawker Typhoon. Its file is 2.6 MB;
    the P-47's was 13 MB (it came packed with tanks and guns).
  - The plane files load last, after the campaign guns, so the guns
    always get their memory first.
- Germans still get the Stuka. Everything else from 0.5.254 is in here
  (the plane moves now, circles over the players, right side).

## 0.5.254: the UAV plane actually flies over the fight now

- 0.5.253 tested by you: the plane showed up but sat far off the side of
  the map and never moved. Three mistakes of mine, all fixed:
  - I was using the game's "turn" command instead of its "move" command,
    so the plane only ever turned in place where it first appeared.
  - I read the players' positions from the wrong place (they all read
    as 0), so the circle's centre was wrong.
  - The sides were swapped: you're American and got the Stuka. Americans
    now get the P-47, Germans the Stuka.
- Same test as before: console streak_give uav, then d-pad right. It
  should fly in, circle above everyone for 30 seconds, then fly off.

## 0.5.253: the UAV is a real plane now (part 1: you can see it)

- Get 3 kills in a row, then press d-pad right to call the UAV, same as
  before: enemies show on your compass for 30 seconds. NEW: a plane now
  flies in, circles high over where everyone is fighting for those 30
  seconds, then flies off. Allies get the P-47 Thunderbolt and Germans get
  the Stuka, both taken from the campaign.
- To test it fast: open the console and type streak_give uav, then press
  d-pad right.
- Killstreaks now count all 32 players (they stopped at 24 before).
- Not yet: shooting the plane down (part 2, next build), and its engine
  sound.
- If a map won't load or the plane looks wrong (sideways, upside down,
  not there at all), set uav_plane = 0 in CoD3Recomp.ini to turn the
  plane off, and tell me what you saw.

## 0.5.252: no more freeze when you die; binocular check aimed better

- 0.5.251 tested by you: you see the ranks over everyone now. Done.
- The couple-of-seconds freeze when you died: that was our own leftover
  test tool. On your first three deaths each match it took 5 screenshots
  and wrote a whole frame's worth of drawing details into the log. The
  death-view problem it was hunting was fixed back in 0.5.236, so it is
  off now (it only runs if we turn it on for a test).
- Binocular sides: the check from 0.5.249 only looked at the first 700
  things drawn each frame, which are all the world. The binocular mask is
  drawn last, with the HUD. It now lists only the flat HUD/overlay draws,
  at both zooms, once per match. Nothing for you to do except tell me if
  the sides were see-through that match.
- 24-player play: no change.

## 0.5.251: status icons over heads for players past 24

- Second half of the "no icon over their head" problem with 32 players.
  The game has two kinds of over-the-head markers. 0.5.250 fixed the first
  (the friendly marker list). The second is the icon list itself: the rank
  or class icon over a teammate, the medic sign over someone who is down,
  and the mark over an enemy who has been spotted. That list only had room
  for 24 players, with other game data right after it, so players 25 to 32
  got nothing. Those 8 now get their own extra space, so the game fills and
  draws icons for all 32.
- Checked and NOT player limits (no change needed): about 15 other places
  that still say 24 or 25 turned out to be text compares (A to Z), sizes
  of 24-byte records, and a 5..24 state switch. Nothing to fix there.
- Next to look at: a vehicle-seat check that only finds the first 24
  players (82428010).
- 24-player play: no change.

## 0.5.250: rank icons over every teammate's head (32 players)

- You saw it: with 32 players, some teammates had no rank icon over their
  head, so they were hard to tell from enemies. The game's code that
  collects your teammates for those icons only looked at the first 24
  players. It now looks at all of them. The place the icons are kept
  already had room for 32, and the code that draws them already draws 32,
  so nothing else had to move.
- The same for the compass: the list of players it shows also stopped at
  24; now it takes all 32 (its room was already 32 too).
- 0.5.249 tested by you: the binoculars worked; the draw lists from that
  good run are kept to compare with a bad one if the sides go again.
- 24-player play: no change.

## 0.5.249: brightness slider; binocular sides looked at closer

- F11, Graphics: "Brightness", 50% to 150% (100% is the game's own). It
  works in multiplayer and the campaign alike, right away on Apply, and is
  saved as brightness in CoD3Recomp.ini. Blacks stay black and whites
  white; the middle tones come up or down. (Putting the slider into the
  game's own multiplayer options menu would mean changing the game's menu
  files, so it lives in F11.)
- 0.5.248 tested by you: the scoreboard showed 15 and 14 with 28 bots: the
  32-player scoreboard works.
- Binoculars: 248's change (the aiming blur drawn while they are up) did
  not fix the sides at the x2 zoom. This build writes, once at each zoom,
  a list of everything drawn in a frame (CoD3MP.log, "render: draw list")
  so the two can be compared and the missing black found.

## 0.5.248: the scoreboard shows the players past 24

- With 32 players on, the scoreboard (both the in-match one and the
  end-of-round one) used to look only at the first 24 players. Now it looks
  at all of them: in team games each side's list (up to 16) is complete,
  and the team totals count everyone.
- Free-for-all with more than 24 players: the list still has 24 rows (that
  is all the game's scoreboard can draw), so the last few joined players
  are left off it. Their scores still count.
- 24-player play: no change.
- Binoculars: you saw the sides go black, then not, with the HD textures
  off too, so the textures are not the cause. While the binoculars are at
  your eyes the game's aiming blur pass is now drawn (it is off by default
  and may be what paints the black round the lenses), and the log says
  what was left out each time you lower them ("render: binoculars down").
- BIG-TEST.bat / BIG-TEST-32.bat no longer open and close the scoreboard
  by themselves every 25 seconds (set COD3_BIG_SCOREBOARD=1 to bring it
  back). Hold Back yourself to check the scoreboard.
- Kill feed: "red killed red" is the rule you picked working: an enemy
  (red) killed one of your team (the one who dies is always red).

## 0.5.247: 32 players is a setting now (test)

- F11, Multiplayer: "32 players (test, restart the game)". With it on and
  the game restarted, a match takes up to 32 players: you and up to 28
  bots (debug.txt: bot_add 28). It is saved as max_players in
  CoD3Recomp.ini. BIG-TEST-32.bat still works as before.
- Still a test: the scoreboard lists only 24 players, and a System Link
  game between two PCs at 32 hasn't been tried (both PCs would need it on).
- The friend update (MAKE-FRIEND-UPDATE.bat) now also packs the co-op
  launchers (PLAY-COOP-HOST/JOIN.bat), COOP-README.txt and the newest
  co-op test findings zip, and installs them.

## 0.5.246: kill feed colours, take two

- 0.5.245 tested by you: smokes are back (the bots throw them). But the kill
  feed still showed green killed green.
- Why: 0.5.245 changed the colour the game uses in team games, but your
  match takes the game's other colouring, which paints a name green when
  it is "a player on this PC", and the bots count as being on your PC.
- Now the colours are set right where the kill line is shown: the one who
  dies is red, the killer is green if he's on your side, red if not.
- Audio sounding late in the Rimling test: still looking. The sound itself
  isn't dropping anything; the game ran slower (28-33 fps) with all the new
  smoke, which is the likely cause.

## 0.5.245: kill feed: whoever dies is red

- Your pick: in the kill feed the player who dies is always shown red
  (you included), and the killer keeps his team's colour (your team green,
  the other team red). Bots and players alike.
- Includes 0.5.244 (bot smoke grenades, safer effects memory), not yet
  tested.

## 0.5.244: bots throw smoke grenades again; safer effects memory

- Smoke grenades: 0.5.243's log showed why the bots never throw one. Each
  bot is given his side's smoke grenade, but with 0 of them in it. Their
  smokes (and extra frags, if any are missing) are now filled in when
  they spawn. The log says "rounds set to N".
- Rimling crashed on 0.5.243 while loading (Farm played fine). It crashed
  somewhere new, in the game's own network-message code, so it may be a
  rare one. To be safe, normal 24-player games now get only the one
  bigger effects pool that actually ran out, not all nine. 32-player games
  keep all nine (they were tested fine).
- Kill feed: 0.5.243's log shows the teams are read right (every bot on
  side 1 or 2, you on side 1), so the colours follow teams: your team
  green, the other team red. Tell me which way you want them (see below).
- Binoculars not fully black when aiming: noted, next.

## 0.5.243: bigger effects memory for normal games too; kill feed colours; bot smokes

- 0.5.242 tested (you ran both tests): the 32-player game (you + 28 bots)
  now plays the full time with no crash. The 24-player games on Rimling and
  Farm also played through.
- But the 24-player Rimling game ran the same effects memory dry 46 times
  (the thing that crashed the 32 game). It didn't crash this time, but it
  could. The bigger effects memory is now on for every game, not only 32.
- Kill feed: you said bots you kill aren't red any more. The log now says,
  for each kill line, which team each name is on, and if a dead bot's team
  reads as "none" at that moment, his real team is used for the line.
- Smoke grenades: the bots never threw one in any of the three tests.
  Every bot is given smokes, but the check that counts them always reads
  zero, so they think they have none. The log now shows exactly what that
  check sees (the first three times), so the next build can fix it.

## 0.5.242: 32-player test, round three: the effects memory and the damage table

- Tested 0.5.241 myself (Claude ran BIG-TEST-32 and BIG-TEST from your game
  folder). Normal 24-player games: both maps (Rimling, Farm) ran the full
  2.5 minutes with 20 bots, no crash, no warnings.
- The 32-player game (you plus 28 bots, Rimling) played 2 minutes and then
  crashed. The game's effects (smoke, sparks, dust) take their memory from
  nine pools of 512 blocks; with 29 players the 1 KB pool ran dry ("is
  empty" 22 times in the log) and an effect was built half-made. With 32
  players those pools now get twice the blocks (about 8 MB more).
- Also for 32: the game remembers who hurt each player in a table with room
  for 24. Damage from players 25-32 was being added onto another part of the
  hurt player's data. It now goes to a spare spot instead.
- Normal games are still 24 players; none of this changes them.
- Still to do for 32 as a setting: the scoreboard only lists 24 players, and
  a game between two PCs set to 32 hasn't been tried.

## 0.5.241: 32-player test, round two

- The first 32-player test (0.5.240) ran the full match on Rimling with you
  plus 28 bots, about 40 fps, no crash. Bots in the new slots spawned,
  fought, died and respawned.
- Fixed from its log: the game keeps a pool of 24 objects, one per player.
  The 25th player got none, and the game wrote into memory it shouldn't.
  With 32 players the pool now holds enough.
- Fixed: player names for the scoreboard, and a team count, were read from
  the session's old player list (stale since 0.5.213). This affects normal
  24-player games too.
- Normal games are still 24 players. The 32 test is BIG-TEST-32.bat.

## 0.5.240: first 32-player test build (normal games stay at 24)

- **Normal games are unchanged: still 24 players.** 32 players only switches
  on for the new test, BIG-TEST-32.bat.
- What the test from 0.5.239 showed: of the ~900 places in the game's code
  that use 24, about 350 run during a match. 189 of them were worked through
  by hand (the server's player list, the game's player list, the session's
  player list, the "nobody" markers, and the player numbers the session
  sends). Those now follow a setting: 24 normally, 32 in the test.
- **BIG-TEST-32.bat** (game folder) loads Rimling with 32 players allowed and
  adds 28 bots, then closes by itself. Claude runs it and reads the log.
  Expect crashes or odd things in this test at first; that is how the rest of
  the work gets found.
- Also found and fixed: four places where the game still read the session's
  old player list after 0.5.213 moved it (finding a player by his account,
  counting players, clearing a field, and a backwards walk). These run in
  normal 24-player games too.

## 0.5.239: big-game prep: the player-limit probe and BIG-TEST.bat

- No change to how the game plays. This build measures, so the next ones
  can raise the 24-player limit safely.
- The game now counts how often each of the 910 places in its code that
  use the number 24 (or 23 and 25) runs, and which numbers they see. It
  writes that to **cap-probe.csv** in the game folder every 30 seconds.
- **BIG-TEST.bat** (game folder) loads a map, joins, adds 20 bots, lets them
  play for 2.5 minutes, opens the scoreboard now and then, then closes the
  game. Results: big-test-<map>.log and cap-probe-<map>.csv.
- Also helpful: play one normal System Link match with bot_add 20 for a few
  minutes (the test above uses devmap, which skips the lobby). Afterwards
  leave cap-probe.csv in the game folder for Claude.

## 0.5.238: loading pictures for 7 DLC maps; second photo set for the rest

- Your picks are in as loading pictures: Rimling 6, Seine River 4
  (brightened), Aller Haut 7, Hilltops 9, Isle 9, Shipyard 5, Champs 5.
- PHOTO-TOUR-RESHOOT.bat takes a new set of 12 shots (other camera spots)
  for Gare Centrale, Stalag, Guns and Marseille.

## 0.5.237 test 6 (no new build): first co-op game between two PCs on 0.5.237

- **Working:** the opening film is shared, the other player's soldier
  appears, and crouching, jumping, moving, shots and grenades show up on
  both PCs.
- **Still wrong:** the joining player's soldier gets hurt by his own smoke
  grenades, a few grenades don't get thrown on the host's side, his soldier
  can vanish (the game then borrows Dixon), and after a mission restart the
  next mission doesn't start together (the host lands on the main menu).
- Findings sent to your friend: coop-test-6-for-friend.zip.

## 0.5.237: your friend's co-op v33 + v34 merged (grenades, sprint, fixes from test 5)

- **No more random shots and crouches** on the other player's soldier: a
  shot now only counts while the fire button is held (reloads, melee and
  weapon switches no longer fire it), and crouch/prone come from the game's
  real stance, not the camera height.
- **Jumps** no longer float the soldier up and down.
- **Grenades and smoke** thrown by either player now land on the other PC
  too.
- **Sprint works in the campaign** (hold or tap sprint while moving forward).
- **Friendly fire on the other player's soldier** is off for the host (a
  melee on it failed the mission in test 5).
- **Next mission / retry:** the joining copy now follows the host into the
  next mission instead of the session ending.
- Both PCs need 0.5.237. Tested by your friend on one PC, not yet between
  two. Includes the photo tour (0.5.236).

## 0.5.236: the photo tour shoots like a photographer

- **12 pictures a map** instead of 8: 4 from high over the map, 2 from roof
  height, and up to 6 at eye height, standing in a street and looking down
  it. The streets are picked from where the bots walk: the longest open
  lines through each map, spread around it, never two from the same corner,
  tilted a touch up so the buildings fill the picture.
- **Maps the bots haven't learnt yet** (Gare Centrale, Seine River, Stalag,
  Hilltops, Isle, Champs, Shipyard) get a learning run first: 16 bots run
  around the map for four minutes, then the game restarts with no bots and
  takes the pictures. So the tour takes longer on those maps (about 6
  minutes each).
- **Marseille loading picture:** your shot 7 is now also set for the second
  picture the loading screen uses (the Call of Duty 3 splash).
- PHOTO-TOUR.bat does all 11 maps; PHOTO-TOUR-REST.bat does all but
  Marseille.

## 0.5.235: your friend's co-op update (v32) merged; campaign options kept

- **Co-op: each player gets his own soldier.** Instead of borrowing one of
  the mission's riflemen (which the mission kept swapping out, 8 times a
  minute in test 4), each copy now spawns a fresh friendly soldier of its
  own for the other player.
- **Co-op: crouch and prone carry over**, and when the other player fires,
  his soldier fires too (real bullets on that side).
- **Co-op: finishing Forest no longer sends the joiner back to the start**
  (the end of the mission was taken for a restart). Gates and doors swing
  on both PCs.
- **Campaign: your options stay put.** Brightness, volume and the other
  Options are saved when you change them and put back the next time you
  start the campaign (they reset every launch before).
- Both PCs need 0.5.235 to play co-op together. Still not done: grenades,
  jump animation. Tested by your friend on one PC only, not yet between
  two PCs.
- Includes everything in 0.5.234 (death flash, Marseille loading picture).

## 0.5.234: death flash fixed; Marseille's loading picture; what really slows Marseille

- **Death flash (the sharp "split screen" glimpse):** found it. When you
  die, the game blurs the screen and lays two shifted copies of it over
  each other (its "double vision"). Our "Blur while aiming" setting is off,
  and switching it off also removed that blur, so the two copies were
  sharp: two of every star icon, two bodies. Now the blur is drawn while
  you're down, and blur while aiming stays off when you're alive. Die
  once and check: it should look soft and dreamy, not doubled and sharp.
- **Marseille loading picture (your pick, shot 7):** in
  textures\replace\maps\ordnance. Maps can now have their own loading
  picture (textures\replace\maps\<map>\), since the DLC maps all share one.
  The game also saves any 1024x512 picture with no replacement into
  textures\dump, so if Marseille's loading screen still shows the old
  picture, the right one is in there and I'll swap it.
- **Marseille's slowdown is not your graphics card.** The "gpu" figure
  counts the time from a frame's first drawing command to its last, waits
  included, so it's as long as the frame whenever the card is waiting on
  us. The real limit is our drawing thread: about 7 microseconds of its
  time per thing drawn. Quiet spots draw about 1,500 things a frame (55
  fps); the big views over the city draw 5,000 to 6,000 (25 fps). Turning
  off MSAA or console depth won't fix that. Next: one run of
  PROFILE-CPU.bat standing in a slow spot, to name the functions that take
  the time.
- The draw-by-draw graphics card timing from 0.5.228 is off again (it cost
  frame rate and was what made the card look guilty).

## 0.5.233: free photo mode (fly around yourself)

- **PHOTO-FREE.bat:** asks which map, loads it, spawns you with god mode and
  noclip so you can fly anywhere (move keys; jump goes up, crouch goes
  down). F10 hides the HUD and gun, F12 takes the picture. Quit when done.
- **PHOTO-TOUR-REST.bat:** the automatic tour for the 7 maps the first run
  didn't reach.
- The first tour run's pictures (Rimling, Marseille, Gare Centrale, Seine
  River: 8 each) are numbered contact sheets for you to pick from.

## 0.5.232: photo tour: shots from above, game closes itself

- Rimling's four pictures came out (no HUD, no gun) but stood at head
  height against walls and trees, one inside a house, and the game froze
  instead of closing, so the tour stopped there. Now 8 pictures a map from
  high above, around the map and looking into its middle, and the game is
  closed outright after the last one so the next map starts.

## 0.5.231: photo tour fixed (team menu, one map per run)

- The first try stopped at the team menu (a test map opens it like any
  game) and the next map change ended the game session ("game session no
  longer available"). Now the tour presses A for you through the team and
  class menus, and PHOTO-TOUR.bat starts the game once per map (load, spawn,
  4 pictures, quit, next map). About a minute and a half a map.
- The test maps opened as four-player split screen: the tour now takes the
  three extra screens out, so the pictures are full screen.

## 0.5.230: your friend's co-op v31 merged (shared films, smoother movement)

- **Films in co-op:** no more auto-skip (it left the joiner stuck in his
  film). The host presses A to skip, but only once the joiner has loaded
  too ("Co-op: waiting for your friend to finish loading..." until then);
  when the host's film ends, the joiner's ends with it.
- **Smoother soldiers and players** on the other PC: they're shown a tenth
  of a second behind and slid between positions, and soldiers no longer
  restart their walk every quarter second.
- **Firewall / Tailscale:** allow UDP ports 28960-28965 (two new ports).
- Both PCs need 0.5.230 for co-op. Includes the photo tour and F10 photo
  mode from 0.5.229.

## 0.5.229: photo tour for DLC loading pictures; F10 photo mode

- **Photo tour:** double-click PHOTO-TOUR.bat in the game folder and leave it.
  The game loads each of the 11 DLC maps by itself, hides the HUD and the
  gun, flies the camera to four spots a map (high up, looking into the
  middle of the map) and takes a picture at each, then quits. About a
  minute a map. The pictures land in the screenshots folder; you pick the
  good ones and I turn them into loading pictures.
- **F10 photo mode:** in any game, F10 hides the HUD and the gun (F10 again
  brings them back); F12 then takes a clean picture.
- Includes everything in 0.5.228.

## 0.5.228: three medics a team; Marseille is the graphics card; the death flash found

- **Medics work** (you were revived in 0.5.227's test). There were still only
  2 in 20 bots: each team hands out every kit in the list once before the
  weights count, so the weight did nothing. bot_kits.txt now has three medic
  lines (shotgun, Thompson, MP40): about 3 medics a team.
- **Marseille:** the new timing shows the graphics card itself is the limit
  there: 20 ms of card time a frame in quiet spots, 55-90 ms in the heavy
  ones (smoke, fire), on your RTX 2070. This build times the drawing step by
  step on the card (one frame in eight) so the next log names the exact
  effects that cost the most.
- **The death flash:** your screenshots show it. When you die from an
  explosion or a bot, the game freezes a picture of the moment you were hit
  and lays it over the death camera, fading it out ("double vision"). On the
  Xbox that picture is soft; here it shows up sharp, which looks like a
  split-screen glimpse. This build logs your first death's drawing so I can
  find that exact layer and blur it like the console does.
- Texture dumping was still on in your settings (left from the campaign
  menu pass): turned off.

## 0.5.227: your friend's co-op update (v30) merged

- **Co-op campaign:** your friend's newest co-op code is in. Players should
  move smoothly instead of jumping forward, stand-ins that a mission deletes
  are replaced, planes no longer drive along the ground on the joiner, and
  if the host's mission fails and restarts, the joiner restarts with him.
  The host launcher can also ask which mission to start once your friend
  connects (to skip Saint-Lo's grenade pickup, which still stops things).
- **Films in co-op:** a mission's film skips itself as soon as the level has
  loaded, so you both start together. Only in co-op; normal campaign is
  unchanged.
- **Both PCs need this build:** the co-op lobby message changed size, so an
  older copy and this one won't see each other.
- Everything from 0.5.226 (medics, Marseille timing, death screenshots) is
  included.

## 0.5.226: medics stand you up for real; Marseille: it's the drawing side

- **Medics:** last build's "send the revive" went nowhere: on the host the game
  sends that message to itself and never hears it. Now the medic runs the
  game's own revive script directly when he uses his kit at the body: full
  health, back on your feet, his revive counted. Log: "stands slot N up".
- **Medics coming to you:** every time you go down, each medic on your team
  writes in the log how far away he is and what he is busy with, so if none
  comes we know why. More bots pick the medic kit now (bot_kits.txt: medic
  weight 6, was 3; last match had only 2 medics in 20 bots).
- **Marseille:** the new timing lines show the bots cost about 2 ms and the
  game's server about 7.5 ms a frame. The other 15-30 ms is the client and
  drawing side, even with no bots at all (25-35 ms on that map alone). So the
  fix is in the graphics path, not the bots. This build turns on the graphics
  timing lines by default in multiplayer, so the next log shows which step
  of drawing eats the time.
- **The flash when you die:** for your first three deaths the game now takes
  five screenshots in the first half second (screenshots folder) and logs
  what the view is doing, so the "split-screen glimpse" can be pinned down.

## 0.5.225: medics revive directly; Marseille slowdown measured

- **Medics:** no bot revive has ever got through, because the game's own
  check (a short look from the medic's eyes at the body) never fired for a
  bot. Now, when a medic bot reaches a body and uses his kit, the revive is
  sent straight to the game the same way the game does after a successful
  check. Medics also only give up for an enemy close by (300, was 500):
  last match 6 of 7 trips were abandoned because deaths happen mid-fight.
  Log: "revive: slot N sends the revive for slot M directly", then "REVIVED".
- **Marseille (ordnance) slowdown:** your profiling run showed it isn't the
  graphics: the game's main thread is 97-100% busy the whole time on that
  map (graphics thread 77%). This build measures how much of that main-thread
  time is the bots ("perf: a game frame: bots X ms ...") so the next fix goes
  at the right part.

---

## 0.5.224: shotgun bots rush in; more smoke; medics keep the body in sight

- **Bot grenades work** (your 0.5.223 test) and the killfeed colours are fixed.
- **Shotgun bots:** a bot holding a shotgun now always plays aggressive
  (unless nearly dead): sprints in weaving from beyond ~450 units, closes to
  point-blank, never stops to snipe at range, and shoots instead of running
  in to melee.
- **More smoke:** besides when hurt or falling back, objective players and
  rushers now throw a smoke to cross under when they push forward with an
  enemy in sight 700 to 2500 away (at most every 16 to 26 s each).
- **Medics:** last match a medic reached YOUR body twice and lost it each
  time: when you're dead your player moves with your spectator view, so the
  "body" kept leaving. Medics now go to where you fell. They only go for
  bodies within 1000 units (longer trips always ended with the man already
  respawned), and dead bots wait up to 25 s for one.
- **Co-op test 2 ready:** your friend's new co-op code (v29) is in: stand-ins now
  work on any mission (Saint-Lo too), and each new mission starts fresh.
- **Marseille frame rate:** your log shows the game's graphics thread is
  busy 100% of the time on that map (about 4,500 draws a frame, 15-22 ms of
  work) - see the request below for a profiling run so I can fix the right
  thing.

---

## 0.5.223: co-op campaign experiment merged (Forest)

- **Your friend's co-op campaign test is in the build** (coop.cpp, from the
  coop-for-friend package). Two players in the campaign's Forest mission over
  Tailscale: the host plays as normal, the joining player's game follows the
  host's world (soldiers' places, health) and each side borrows one of the
  mission's American riflemen as the other player. Off unless started with
  the co-op launchers, so the normal campaign is unchanged.
- **To try it:** both of you need the same build. The host runs
  PLAY-COOP-HOST.bat (beside CoD3.exe), the other runs PLAY-COOP-JOIN.bat;
  each asks for the other's Tailscale address (Enter uses the System Link
  peer from F11). Allow CoD3.exe through the firewall (UDP 28962-28963).
  Both go to the main menu; the host picks Forest from chapter select and the
  other game follows. Logs: coop-host-play.log / coop-join-play.log.
- Rough edges (from its notes): only Forest; the stand-in soldiers have
  their own brains; story/objectives run on each PC separately. Never yet
  tested between two PCs. See COOP-README.txt in the game folder.

---

## 0.5.222: bot grenades, next try; revive trace

- **Bot grenades:** your 0.5.221 test showed the bots' throws now really run
  (the game starts the throw the way it does for you), but the game kept
  thinking the bot was holding his gun, so it had nothing to throw. When you
  throw, the game switches what you're holding to the grenade for the length
  of the throw. Bots now get the same switch at the start of their throw.
- **Rifle grenades:** you saw 3: that's 4 in total, one already loaded on the
  rifle plus 3 spare. Mines get 4 the same way (check when you can).
- **Medics:** a medic reached a body and tried 3 times, but the game's revive
  never even checked (it marks a miss when it does). This build logs exactly
  what he's holding and doing while he tries ("bots: revive trace"), so the
  next log tells us why.
- **Killfeed:** the log shows the fix working (10 bot-on-bot kills drawn as
  you see them). Tell me if the colours look right now.
- Two more campaign mission pictures in HD (17 of the 38 campaign menu
  pictures done; the rest a few per build).

---

## 0.5.221: campaign at 144 fps in its menus too; more campaign pictures in HD

- **Campaign frame rate:** campaign levels already ran at 144 (your last
  campaign log shows 144 fps in a level, held there by frame_cap = 144 in the
  F11 menu; set it higher or off for more). Its menus stayed at the console's
  rate. Now, as in multiplayer, only films (intros, briefings, a moving menu
  background) keep the console's speed, so they play at the right speed;
  menus and levels run as fast as your PC and the cap allow.
- **Campaign pictures upscaled (4x):** the whole start menu (background, logo,
  frames, font, button icons, lock), the three sepia war photos, and the
  mission pictures and their thumbnails as they finish (all in
  textures\replace; no rebuild needed for those).

---

## 0.5.220: killfeed colours; bot throws not cancelled; 4 mines/rifle grenades given; more medics

- **Killfeed:** when bots killed each other both names showed green. The
  game colours each name by comparing that player's team with "you", but a
  bot's kill happens while the game is briefly set up as that bot, so "you"
  pointed at the wrong player and everyone looked like a teammate. The
  killfeed line is now drawn as you see it: enemies red, teammates green.
- **Bot grenades and smokes:** your test showed a bot's throw now really
  starts, then gets cancelled a moment later: the bot was also pressing
  crouch and sprint during the throw (you press nothing else). Now for about
  two seconds after a throw starts the bot doesn't crouch, sprint or fire,
  so the grenade gets out.
- **4 mines and 4 rifle grenades (this time for real):** 0.5.219 changed the
  wrong number. Mines and rifle grenades are handed out by their own part of
  the game (1, 2 or 3 by rank); that now gives 4, and 4 is also the most an
  ammo crate fills you back to. Log: "class: mines special set to 4".
- **Medics:** there was only ONE medic bot in a 20-bot match, and he reached a
  body once but never used his kit (he kept waiting to face it exactly). He
  now uses it after 2 seconds at the body, and medics are 3x more common in
  bot kits (bot_kits.txt, in your game folder).

---

## 0.5.219: bot grenades for real; 4 mines and 4 rifle grenades; bots feel for walls

- **Why bot grenades never landed (from your last match):** when you throw,
  the very first instant of the throw also tells the game WHICH grenade
  (its weapon number), then goes back to your gun. Frags and smokes both use
  the same throw button. The bots pressed the button while still naming
  their gun, so nothing happened. Now a bot's throw names the grenade
  (frag or smoke from his kit) for the first moments, uses the same button
  you do, and doesn't sprint while throwing. Log: "throws a grenade ...
  weapon N" and "frag button found" / "smoke button found".
- **4 mines and 4 rifle grenades:** every mine and rifle grenade now holds 4
  in total (what's loaded plus spares), for everyone including bots. You start
  with them, and an ammo crate fills you back up to 4 once you've used them.
  The log lists each one ("weapons: ... mine ... max").
- **Bots on the flag crouch less:** holding a flag with nobody around they
  crouch a third of the time and stand and shuffle about the rest; fighting
  on the flag they crouch half the time and strafe when up.
- **Bots feel ahead for walls:** while moving, each bot checks a little way
  ahead (and to both sides) several times a second. If a wall is coming he
  turns along it toward the more open side instead of walking into it; if
  he's boxed in he backs off. If he keeps meeting the same wall (3 times in
  12 s) he remembers that spot and plans a different way round from then on.
  Log: "bots: feelers".
- **Medics:** last match they set off to revive 11 times but gave up 8 times
  as soon as any enemy was in view. Now only an enemy close by (500) stops
  them, and never once they're at the body.
- Includes 0.5.218 (room for more players in the game's player list).

---

## 0.5.218: more players, step: room in the game's player list

- **What it is:** the game keeps one small list of the players in a match:
  exactly 24 places, with the map's own object stored right after them.
  Players 25 to 32 would have overwritten that map object. This build makes
  the list bigger (room for 32) and moves the map object to the end, in all
  42 places the game uses it.
- **What you'll notice:** nothing. Still 24 players and 20 bots. This only
  makes room. While we stay at 24, the map object is kept in both its old and
  new place, so anything we missed still works.
- **The log checks it:** "player limit: entity list ... +100 X +132 X spare
  slots clean". Both numbers should match and the spare slots stay clean.
- Also in this build (no rebuild needed for pictures): the campaign start
  menu pictures as they finish upscaling.

---

## 0.5.217: campaign start menu in HD; dead bots shout for a medic

- **Campaign start menu upscaled (4x).** From your menu pass
  (textures\used-campaign.txt: 38 menu pictures the campaign shows; the rest
  were movie frames): the start menu background, the CALL OF DUTY 3 logo,
  the menu frames, the controller button icons, the menu text font and the
  lock icon. They go straight into textures\replace (game folder only,
  never GitHub). No rebuild needed for these: just start the campaign.
- **Still to do, a few per build:** the mission pictures (17 big ones and
  their small thumbnails) and the three sepia photos.
- **Dead bots shout "Medic!"** A second and a half after a bot falls he calls
  for a medic, the same call the game makes when a player picks "call medic"
  on his death screen (the voice line and whatever the game shows with it).
  Medic bots already go to the bodies (0.5.216).
- texture saving is off again in CoD3Recomp.ini.

---

## 0.5.216: medic bots revive; the log works again

- **CoD3MP.log was empty in 0.5.215.** Hiding the console window broke where
  the log text went: everything printed went nowhere, so your last match
  left a 0-byte log (that match's grenade results are lost). Fixed: the log
  is written again with no console window.
- **Medic bots revive teammates.** Found how the game's own revive works: a
  medic takes out his medic kit (mp_revive), stands over a dead teammate's
  body (within about 100 units, looking at it) and fires it. Medic bots now
  do exactly that when no enemy is close: they pick the nearest dead
  teammate (you or a bot) within reach that no other medic is going for,
  run to the body, look down at it and use the kit, up to three tries.
  The log says "bots: revive: slot N (medic) goes to revive slot M" and
  "REVIVED" when it works.
- **Dead bots wait for a medic.** A dead bot normally comes back after 4
  seconds. If a medic is on his way to him, he now waits for him (up to 20
  seconds) instead ("calls for a medic" in the log). The game's own "Medic!"
  shout and the icon over the body are not done for bots yet.
- **You can be revived by bots.** When you die, stay down (don't pick a class
  and respawn straight away) and a medic bot nearby should come for you.
- **Campaign pictures:** texture saving is switched on in CoD3Recomp.ini for
  the campaign menu pass (see below). Turn it off again afterwards, or tell
  me and I will.

---

## 0.5.215: bots find the real grenade buttons; no console window; campaign menu list

- **No more black cmd window.** CoD3MP.exe now opens only the game window.
  Everything still goes to CoD3MP.log as before. If you ever want the live
  console back, start it with `--console` (or set COD3_CONSOLE=1). First-time
  setup (no game folder yet) still opens one, because it asks questions.
- **Bot grenades and smokes.** The log proved the bots were pressing buttons
  that do nothing: they "threw" but never spent a grenade. Your own presses
  in the last log were different buttons. Now each throw tries the next
  likely button and checks whether the bot's grenade count actually went
  down; the first one that works is kept for every bot. The log says
  "bots: frag button found" / "bots: smoke button found". A bot with no
  grenades left no longer pretends to throw.
- **Fastest way to settle it:** in a match, throw one frag and one smoke
  yourself. When your own count drops, the game logs which key you pressed
  ("bots: host spent a frag ... keys pressed") and hands that key straight to
  the bots.
- **More players:** found that the game's entity list keeps exactly 24 player
  slots, with the level's own object right after them (slot 25), used in 41
  places. Going past 24 players has to move that or give the extra players
  slots elsewhere. Written down for the next step; the limit is still 24
  players / 20 bots.
- **Campaign menu pictures:** the campaign now keeps its own list of the
  pictures it shows (textures\used-campaign.txt), so multiplayer no longer
  wipes it. Next: browse the campaign menus once, send that file, and the
  missing ones get upscaled a couple at a time.

---

## 0.5.214: prepare the join path for extra player slots

- Replace the old six-at-a-time client search with a bounded search that can safely handle a future 32-entry server.
- Add explicit session-roster mapping for all 32 reserved records. Runtime addressing stays at 24 until player limits and sentinels are activated together.
- Player and bot limits remain 24 and 20. This is preparation for added players; 32-player gameplay is not enabled yet.
- Build and runtime status: docs/build-logs/validation-214.txt.

---

## 0.5.213: move the session roster into 32-entry storage

- The game now has a separate 32-entry block ready for its session-player
  records. It copies the current 24 entries and checks that the eight spare
  records stay untouched, with a guard to catch overruns.
- The playable limit remains 24. This update prepares the session layer for
  16v16; it does not yet let players or bots use the additional slots.
- The change can be rolled back for comparison with
  `COD3_NO_SESSION_RELOCATION=1`. The user validated the storage checks in one solo run through a match and map change. The active cap stays 24; expanded-slot behavior and two-PC capacity validation remain outstanding.

---

## 0.5.212: remove forced renderer dump from scoped ADS

- Lee-Enfield ADS tracing still records each aim transition and scope settings,
  but no longer forces a full renderer-frame dump as soon as you aim. That
  diagnostic dump could add a hitch exactly when testing the scope.
- The user confirmed scoped ADS works normally again on 0.5.212. This build preserves those weapon values while continuing player-storage work.

## 0.5.211: cache campaign world-model bounds

- The per-frame weapon-model fit no longer walks every vertex of each saved
  campaign mesh just to rebuild its unchanged bounds. It computes and caches
  those bounds when the mesh is captured, then reuses them while fitting.
- This targets CPU work that could contribute to frame drops. It does not
  change the Lee-Enfield's first-person scope overlay; the black-screen issue
  still needs a targeted in-game repro and renderer trace.

---

## 0.5.210: reserve and watch eight more server-client records

- The game now allocates room for 32 server-client records and clears the
  full block. The active limit stays 24; no new player or bot slot can use the
  extra space yet. The log marks slots 24â€“31 and reports any writes to them.
- This is a bounds-check step toward 16v16. Session players, player-number
  sentinels, game/UI loops and other limits still need to be expanded before
  the lobby can accept more than 24.
- Includes 0.5.209's joiner-side mirror spawn fix and corrected throw trace.

---

## 0.5.209: joiner-side bot spawn path

- Fixed the likely reason the other player saw host bots stay dead on his
  screen. His game had joined each bot into the session, so the normal spawn
  route treated the bot as owned by the host and sent placement over the
  network. The joiner now places recognized copies locally and runs the
  game's spawn finalizer in a scoped spare local-client context, restoring the
  joiner's view state afterwards.
- The attached match log had no bot grenade/smoke decisions or throw traces,
  so it cannot confirm why their animations are missing. The throw trace now
  records the final command weapon after selection, making the next test log
  useful when a bot actually attempts a throw.
- The 24-player cap is unchanged. The relocated 32-record storage passed its
  checks in the previous match; expanding server clients, sessions and UI is
  still required before raising the cap.

---

## 0.5.208: larger player storage; merged world-model fitting; scope investigation

- First step toward 16v16: server player records now live in a dedicated
  32-record allocation. The playable limit is still 24: server clients,
  sessions, player-number sentinels and UI must be expanded in later builds.
  Logs check relocated pointers, the unused eight records and an end guard.
  COD3_NO_PLAYER_RELOCATION=1 restores the original storage for comparison.
- Merged the Team Native 0.5.206 package's world-model fitting, MG42
  third-person correction and optional bot pose tools, preserving 0.5.206's
  crate fix and 0.5.207's mirror lives, role balance and throw tracing.
- Investigating intermittent black screens when ADS with Lee-Enfield Scoped.
  Added bounded ADS-transition and render tracing; no confirmed fix yet.
- Real-match and System Link validation pending. Both PCs need the same EXE.

---

## 0.5.207: Your friend sees the bots die; balanced bot roles

- **Bot deaths for whoever joins (untested).** Your friend saw bots freeze,
  then vanish, instead of dying. The 0.5.206 logs showed why: on his
  machine, his copy of you is alive (and his game plays your deaths
  itself), but his copies of your bots were never spawned there. With no
  live copy, there was nothing to kill and no death animation to play.
  - Your game now tells the other machine, four times a second, whether each
    bot is alive, which life it's on, and how it last died (killer, cause,
    weapon, direction, hit location).
  - His game spawns its copy of each bot the game's normal way when that bot
    starts a new life. When your game says the bot died, his copy dies
    through the game's own death code, with your killer, cause and
    direction, so the death animation plays.
  - His log says it with lines like "mirror number N spawned here" and
    "mirror number N dies here as on the host". If this causes trouble,
    starting his game with COD3_NO_MIRROR_LIFE=1 turns it off.
- **Bot roles are balanced per side.** Roles were picked by a random roll
  that came out lopsided when bots were added together (last match: 7
  campers, 8 flankers, 4 objective, 1 rusher out of 20). Each new bot now
  takes the role its side is shortest of: about 25% objective, 25% rusher,
  28% flanker, 22% camper. That's 3, 2, 3, 2 with 10 per side.
- **Bot grenades and smokes: not working, being traced.** You were right:
  the bots decide to throw ("throws a grenade" in the log), but nothing
  comes out. Your Fuel Plant match had no grenade or smoke throw at all, and
  the Merville one about 6 throws for 71 tries. They do carry grenades. This
  build logs what happens during the first 12 bot throws and during your own
  ("throw trace" lines). **Throw a frag and a smoke yourself** in the next
  match, so the log has a working throw to compare against.
- Both PCs need 0.5.207 to play together: send your friend the new update
  zip. Keep native_commands = 0.

---

## 0.5.206: Bots stop walking into crates; where the 24 players live

- **0.5.205 played well:** no crash, no effects shortage (the most effects
  at once was 329 of 8,192). The bots played War's flag as takers and screens,
  and spread out more (the nearest teammate was usually 350-620 units away,
  was 150-470).
- **Fixed: bots stuck against crates and pipes (Fuel Plant).** 0.5.205's hill
  climbing also counted flat-topped crates and pipes 20 to 60 units high as
  "climbable". Bots walked into them and stayed there: 272 "stuck" lines in
  6 minutes, one bot stuck 8+ times in the same spot. Now the climb has to
  match the tilt of the ground: a slope can rise as steeply as it really
  slopes, but a flat top only allows the normal step (18 units). Hills still
  work up to 45 degrees.
- **More players: found where the 24 players are.** The game keeps them in
  one block of 24 records of 2,992 bytes each (at 0x82BF21D0). There is no
  room after it, so it has to be moved to a bigger block. The good news:
  only about 6 functions point at it directly; the rest go through each
  player. That makes moving it much smaller than the study feared. That's the
  next player-limit build.
- Keep native_commands = 0. Both PCs need the same build to play together.

---

## 0.5.205: Effects crash fixed, bots climb hills, smarter War flag play

- **The crash after a while with 22 bots is fixed (untested).** The host ran
  out of room for effects: muzzle flashes, bullet impacts, smoke and
  explosions. The game keeps a fixed number of them (4,096). With 22 bots
  firing (MG42s especially), that filled up from about 6 minutes in. From then
  on, the effects code damaged memory until the game went down. Your
  friend's game never ran out.
  - The game now gets twice the room: 8,192 effects, twice the effect parts
    and twice the effects memory. That's about 5 MB more.
  - The "pool:" line in CoD3MP.log now also shows how many effects are in use
    and the most at once. If those keep climbing all match, something is
    leaking, and the next log will show it.
- **Bots walk up and down hills.** The check for "can I step there" tested
  the ground flat, so any slope over about 8 degrees uphill (13 degrees down)
  counted as a wall, and the bot stood still or turned back. Now, when the
  flat test fails, the bot tries the step in short pieces up the slope, up to
  45 degrees, as steep as a player can walk. Walls and cliffs still stop it,
  and flat ground costs no extra work.
  - This should also help the clumping: on Merville most bots were stuck in
    the low middle of the map, because the hills around it were "walls".
- **Smarter War flag play.**
  - About 40% of each side now plays the flag in play (3 to 7 bots). The
    objective-role bots go first, then rushers and flankers; campers keep
    their posts. Before, it was at most 4 bots per side.
  - Half of them are takers: they go onto the flag itself, where the capture
    counts (before, they stood in a ring 120 to 360 units away and seldom
    took it). Takers also keep pushing when the enemy is not close.
  - The other half screen: they hold 200 to 450 units out, on the side the
    enemy comes from, each at their own angle.
- **Your friend not seeing enemy death animations: diagnosis build.** His
  log shows his game never learns that one of your bots died. On his
  machine, every bot's health stays at 100 all match. Your game does record
  how each bot died, the same way it does for you. This build logs, on his
  machine, whether that death information arrives for each bot, and for
  you as a comparison. Send both logs after a match and I can fix the right
  side.
- **More players: a correction.** The study's plan to grow the players'
  records into the free space after them was wrong. That array is the four
  local players' (split screen), not the 24 server players'. That's why the
  "free" space was in use. This build logs where the 24 players' records
  really are, so the next step can be planned on facts.
- Keep native_commands = 0. Both PCs need 0.5.205 to play together.

---

## 0.5.204: Stability fix: the freeze after adding bots

- **The crash is fixed.** It wasn't a real crash: the game froze for good (FPS 0)
  about 15 seconds after bot_add 24, while a bot was respawning.
  - What happened: the game keeps one shared store of small memory blocks.
    Most of its small allocations come from it, including the "endon" and
    "wait" records of the game scripts every player runs. With 24 bots, plus
    the campaign weapons, sounds and maps that are now loaded beside the
    game's own, that store ran out.
  - The original game never checks for this. It wrote a record into memory it
    didn't have, which broke a list, and then went round that list forever.
  - Now, when the store is full, the block comes from the game's bigger
    general memory instead. The game itself has a setting for exactly this,
    but it was switched off for this store. As a second guard, if one of those
    lists ever does break, it's noted in the log and skipped instead of
    freezing.
- **New "pool:" lines in CoD3MP.log** every 30 seconds show how full each size
  of that store is. Extra lines appear the moment it runs out. That tells us
  how close a full lobby gets and what fills it, which matters for more
  players.
- **About the frame drops:** in your log, the slow part (11-13 FPS, once down
  to 3) came right after the map loaded, from about 30 s to 115 s, before you
  added any bots. Later the same view ran at 50-60 FPS. The time was going
  into the PC side of drawing, not the game's own code. The cause isn't
  pinned down yet. If it happens again, tell me what you were doing (menus,
  class editor, F11 menu, just spawned) and send the log.
- Nothing else changed: bots, guns and your friend's weapons are as in
  0.5.203. Keep native_commands = 0.

---

## 0.5.203: Your friend's new guns: MG42, scoped Lee-Enfield, sticky bomb (merged)

- **From your friend's team, their second 0.5.900 package**, merged on top of
  0.5.202 (all of 0.5.202's bot work is in):
  - **MG42**, a new main weapon: the MG42's real gunfire and fire rate, its
    model in your hands and on the ground, aims from the shoulder.
  - **Scoped Lee-Enfield** ("LEE-ENFIELD (SCOPED)"): its own scope, sounds
    and reload; works the bolt out of the scope like the scoped Kar98.
  - **Sticky bomb** (the British No. 74) as a custom class grenade.
  - **The MG34 now aims from the shoulder too** (no forced prone bipod).
    Set mg34_no_deploy = 0 in CoD3Recomp.ini for the original.
  - The G43's reload lined up with its sound (3.45 s).
  - The game loads one more campaign level's weapons (Laison's, for the
    scoped Lee-Enfield), so its map memory is a little bigger (495 MB, was
    480 MB). Keep native_commands = 0.
- **Bots can carry the new guns:** bot_kits.txt gets "heavy mg42" and
  "scout lee_enfield_sniper" lines (added to your file). The scoped
  Lee-Enfield counts as a sniper rifle: slow scoped walk, less sway, and bots
  with it take overwatch.
- Their notes, as they wrote them:

- Sticky bomb (British No. 74) as a custom class grenade: kind 4, "STICKY
  BOMB (BRITISH)" in the class editor. It is multiplayer's own: item 30,
  weapon_stickygrenade, WEAPON_STICKYGRENADE in weapusge.cod (noBounce: it
  sticks; 3 s fuse; 900 damage in a 147 radius in MP; US grenade throw
  animations), loaded on every map but given to no class. Tested: spawned
  with two in the grenade slot (Eder Dam). Throwing/sticking UNTESTED by hand.
- MG42 (primary 20 / sidearm 22, appended): the campaign has it only
  mounted and in AI soldiers' hands (w_ge_MG42_nobipod; no first-person
  model or arm animations), so it is a copy of the MG34 with the MG42 model
  others see in your hands and on the ground, the MG34 in your own view,
  the MG34's icon, the MG42's real gunfire, the same sounds the game's own
  multiplayer MG42 (the Horch turret) plays: Forest's MG42_FIRE_1ST_LASTSHOT
  (2.58 s, first person) and MG42_FIRE_3RD_LASTSHOT (3.06 s, others), which
  match MG42_HORCH_FIRE_1ST/_3RD byte for byte in size and length; on maps
  whose bank has the Horch cues and no campaign banks, those; else the
  MG34's (Forest's MG42_FIRE_1ST_LOOP is the mounted gun's loop, never
  stopped when played a shot at a time) and its fire rate, 50 ms a shot (the MG34's 70). Its reload and bipod
  sounds are the MG34's. The AI model in your own view was tried: it sits
  above the view with only a hand, so not used. Tested: made as weapon 45, its model found,
  spawned with it. User confirmed firing and sound with MG42_FIRE_3RD; the _LASTSHOT cues found in the auto-test log, UNTESTED by ear.
  Stats checked against the game's MG42 data (no handheld MG42 file exists):
  the turrets' 50 ms (MP Horch) fire time is used; damage and range stay
  the MG34's (same 7.92 mm round; the turrets' 50/110 damage is for mounted guns).
- MG42 first-person body: the MG34 view model's gun body is swapped, only
  while the MG42 is in hand, for a copy of the AI MG42's body (mesh
  "mg42.w_ge_MG42_nobipod"), turned a quarter about the up axis, scaled 0.40,
  lined up on the MG34 body's barrel end and moved 3 back and 0.4 across, which
  puts its sights where the MG34's were and its receiver against the MG34 drum
  (sub_82446AB8 finds the meshes; MpFixesMg42View changes the drawn mesh's
  buffers in place each frame). The MG34's bipod (base and legs) is not drawn
  while the MG42 is held (its counts and index size set to none). The drum,
  charging handle and all animations stay. COD3_MG42_BODY="scale x y z rear"
  tunes the fit. Fit chosen from side-by-side screenshots against the MG34.
- MG42 aiming: the MG34's class (3) made aiming a bipod deploy (forced prone,
  "Prone blocked", wall mounting); the MG42 takes the BAR's class (0) and its
  deploy animations are left out, so it aims from the shoulder. While aiming
  (player state +172, the sights' fraction, over 0.5) a second MG42 body is
  drawn, moved (-0.2, +6, -0.7) so its front sight sits on the sight line and
  the receiver is out of the eye (it clipped into the view). No wall mounting.
- Scoped Lee-Enfield ("LEE-ENFIELD (SCOPED)", primary 21 / sidearm 23): Laison's
  WEAPON_LEE_ENFIELD_SNIPER as a copy of the scoped Kar98: the Lee-Enfield view
  model with Laison's scope model (attach1Model lee_enfield_scope, tag
  w_lee_enfield), the brRFL moves incl. reloadSCOPE, its icon and 77 of its file's
  numbers (10 rounds, zoom 15, 1.4/0.7/1.0 s reload parts; damage the Kar98's).
  Its shot (a sniper's: event 13) is played as the Kar98's shot event with the
  cue Lee_Enfield_Sniper_FIRE_1ST (laison.wbk) and SniperRingoff; its bolt is
  GR_Enfield_rechamber. Laison's weapons archive and bank are loaded too; the
  map block is 495 MB (480 hung the load with four campaign archives).
  Its reload now plays (session 4, see below).
- Session 4 (2026-10-17, built and auto-tested; hand test pending):
  - MG42 aiming view: the MG42's rear sight is now where the MG34's is. Both
    bodies were written out as meshes (diagnostic COD3_MESH_DUMP=folder) and
    the sights measured: the MG34's rear notch at y -6.57, z 3.75 of its body
    (front post y -22.5); the MG42's notch at x -18.55, z 4.76 of the AI
    model (front post x 2.4, tip 4.69). The old aiming fit (-0.2, +6, -0.7)
    had put the MG42's rear sight about 10.7 units behind the MG34's, so the
    camera sat in front of it ("halfway down the barrel"). New fit: scale
    0.40, x -0.22, y -4.72, z -0.97, then tipped 0.5 degrees muzzle up and
    turned -0.7 degrees about that notch (new BodyFit pitch/yaw, the 6th and
    7th numbers of COD3_MG42_ADS_BODY / _CYCLE) so the front post sits in the
    notch on the screen's centre (screenshots, COD3_MG42_ADS_CYCLE).
  - MG42 raise/lower: the aiming body is now made again step by step from the
    hip fit to the sights' fit as the sights' fraction (+172, smooth 0..1)
    moves, and dipped up to 1.0 lower half way (COD3_MG42_DIP). Before, one
    jump at 0.5 let the gun slide through the view, riding high. (A third
    body buffer could not be allocated, so the aiming body's own is reused.)
  - MG34 aims from the shoulder too (new ini `mg34_no_deploy`, default 1): in
    the sub_82486BF8 hook the stock mg34's class (+176) becomes the BAR's and
    its lmgDeployedAnim/DeployAnim/BreakdownAnim (+148/+152/+156) are
    emptied, as for the MG42. No forced prone / bipod deploy / wall mount.
    Set `mg34_no_deploy = 0` for the stock behaviour.
  - Scoped Lee-Enfield: endless rounds fixed. Its campaign fireDelayMP 0.1 s
    (+1520) made every shot skip taking a round from the clip in
    multiplayer, so it never emptied and never reloaded (that was the
    "silent reload"). Now 0, the scoped Kar98's. With rounds counting down
    the reload plays its sounds through the scoped Kar98's events (16 start,
    14 each round, 17 end) and CampaignReloadCue: GR_Enfield_Scope_start,
    _reload x10, _end (laison.wbk), seen in the auto-test log. Its clip is
    now the Lee-Enfield's (`lee_enfield`, 10 rounds) instead of one of its own.
  - Scoped Lee-Enfield rechamberWhileAds 0 (campaign 1): it drops out of the
    scope to work the bolt each shot, as the scoped Kar98 does (with 1 the
    bolt was worked behind the scope, seen only after the first shot).
  - Scoped Lee-Enfield double shot: with fireDelay 0 its shots also send
    the scoped Kar98's own gunshot events (weapon_kar98_sniper_weapon_fire_*),
    which played SPRINGFIELD_FIRE_1ST + SniperRingoff 0.1 s before its own
    Lee_Enfield_Sniper_FIRE_1ST (found in COD3_SOUND_TIMES). Those events
    are now silent for a lee_enfield_sniper shooter (sub_82554BA0 hook), as
    is the Kar98 ring-off its own shot event carried (COD3_ENFIELD_RINGOFF=1
    restores that one). Auto-test: no SPRINGFIELD/SniperRingoff cue left.
  - Both Lee-Enfields: the right hand clipped into the butt at spawn and
    after every reload, but not after a shot (the rack's last pose). The
    campaign's British idle (w_arms_brRFL_idle_a) raises the right hand into
    view on multiplayer's arms. Both now keep the Kar98's idle
    (w_arms_geRFL_idle_a: weapon fields +72/+76 and the animation entry,
    KeepsCopyIdle); the sights' idle stays British. Screenshots at spawn,
    after a shot and after a reload now match, hand out of view.
    COD3_ENFIELD_BRITISH_IDLE=1 restores the British idle.
  - Screenshots taken in the same second no longer overwrite each other
    (overlay.cpp: -2, -3, ... added to the name).
  - Unused, off by default: a Lee-Enfield stock bend (COD3_ENFIELD_BEND /
    _CYCLE) written while looking for the clipping; not needed.
  - Diagnostics (env only): COD3_MESH_DUMP, COD3_RELOAD_TRACE (sub_824B13A0
    reload sound kinds), COD3_WEAPON_DIFF="a b", COD3_MG42_BLEND_SHOTS,
    COD3_PS_WATCH=2 (hex), COD3_ANIM_ITEMS=1 (animation entry records). The weapons log lists each campaign gun's clip and
    ammunition numbers.
- G43 reload 3.45 s (campaign 3.95): its one reload sound, GR_G43, starts with
  the reload (mag out 0.4 s, mag seated 2.4 s, bolt 3.0 s), while in
  multiplayer the animation's last part ends with the reload time; at 3.95 the
  bolt was pulled ~0.5 s after its sound. Lined up from timed screenshots.
- Diagnostics (env only): COD3_SOUND_TIMES=1 logs every sound and cue with ms
  times; COD3_RELOAD_SHOTS=1 screenshots through a reload; COD3_SHOTS="s s".

## 0.5.202: Bots aim down their sights, leave spawn, push, check the flag, and snipe from overwatch

- **Bots aim down their sights now.** They never did, and that's why no
  rockets flew in 0.5.201: a bazooka or Panzerschreck only fires while
  aimed. Launchers and sniper rifles always aim. Other guns aim past 700
  units, unless the bot is charging in or pushing hard. The shot waits until
  the sights are up.
- **Bots learn where each team spawns**, from where players appear after
  dying.
  - **Leaving spawn:** for their first 15 seconds a bot heads out along a
    learned route to a spot at least 500 units from his own spawn, before
    anything else.
  - **Pushing:** half the rushers push all the way up to the enemy's spawn
    area and wait there a few seconds in ambush.
  - **Checking the flag:** a quarter of the bots, every other errand, go and
    look at the War flag and watch it a few seconds.
  - **Defenders and attackers:** campers and bots that keep dying favor spots
    on their own half; rushers and bots on a streak favor the enemy's half.
- **Snipers watch from overwatch.** A bot with a scoped rifle takes high
  ground, lies down scoped in, and watches the busiest ways (where players
  walk most) in turn for 35 to 60 seconds. Then he moves up toward the enemy
  and does it again.
- **More players:** Phase 1 has started. Every place in the game's code that
  has to change for more than 24 players is now listed: 1,107 places in 421
  functions (docs/player-limit/sites.csv, explained in
  PLAYER-LIMIT-STUDY.md). The next builds patch them a group at a time,
  starting with a safety-check build.
- 0.5.201 on Rouen (map known): about 49% of bots within 300 of a teammate
  (was 56% on Rouen in 0.5.199), 7 to 15 bots on errands at a time.

## 0.5.201: No more rocket volleys, bots stop shooting their teammates

- **Bazookas and Panzerschrecks fire one rocket at a time.** Bots fire by
  tapping the trigger every other frame, which rifles need. With a launcher
  that looked like a volley of rockets. Now a bot presses once, then waits
  2.5 seconds. He also won't fire it at anyone closer than 300 units (his
  own blast), or when a teammate is within 300 units of the target.
- **Bots hold fire when a teammate is in the way.** Before every shot a bot
  checks whether a teammate stands in his line of fire (a little wider the
  farther the shot, for the gun's spread). If one does, he doesn't shoot. He
  steps aside to clear the line, then shoots.
- **Grenades** aren't thrown when a teammate is within about 300 units of
  where they would land.
- The log says when it happens ("holds fire: teammate N in the way",
  "fires his launcher ... (one shot)").
- 0.5.200 on Ederdam, a map the bots had never seen: fewer bots bunched
  (48% within 300 of a teammate on average, down to 25%; was 56%), and
  high and low ground now picked (22 of 36 errands; was 3 of 37).

## 0.5.200: Bots spread out over the map

- What your Rouen test showed: 56% of the time a bot was within 300 units
  of a teammate. Three reasons:
  - **11 of 20 bots had the objective role** and all ran to the one War flag.
  - **Everyone else went for the nearest enemy**, so they all arrived from
    the same direction. An errand (outskirts, high or low ground) was dropped
    for any enemy within 900 to 1700 units, which with 20 bots is almost
    always (only 37 errands all match).
  - **High and low ground were almost never picked** on a map as flat as
    Rouen (3 of 37 errands).
- Now:
  - Only a quarter of bots get the objective role (was 40%), and at most 4
    per side play the War flag. They spread round it 120 to 360 units out,
    instead of piling onto it.
  - Bots on an errand keep to it unless an enemy is in sight or within 550.
  - An enemy already chased by two teammates counts as farther away, so the
    others go after someone else.
  - Closing in on an enemy they can't see, bots take their own lane up to
    520 units to either side, so a group arrives from several directions.
  - Teammates keep about twice the distance from each other when they're not
    fighting.
  - High and low ground are picked from the top and bottom 15% of the map's
    heights, at least 32 units above or below the middle, so even flat maps
    have some.
- The log now measures it every 30 seconds ("bots: spread: ... nearest
  teammate median ..., N within 300, N on errands"), so we can see if it
  helped.
- Map learning: Rouen now has 528 places learned, on top of Merville and
  mp_guns.

## 0.5.199: The Gewehr 43 and campaign gun sounds (merged); stuck-bot clock fix; vehicles; Support and Medic packs

- **Fixed a bug that broke stuck detection in 0.5.198.** While a bot walked
  to a vehicle, the code that notices stuck bots was reading two different
  clocks. It thought bots were stuck twice every frame (30,847 log lines on
  mp_guns) and kept restarting their unstick attempts, so a really stuck bot
  never escalated to jumping, dropping down or wandering off. One clock again.
- **Bots try harder to get in vehicles.** In 0.5.198 they reached the
  motorcycles and pressed use 40 times but never got on. Now they also press
  the console's "use" (X), walk right up to it, and look at it. The log says
  each try ("presses use ... link"). If they still can't get on, the log
  tells us what to try next.
- **Support and Medic bots drop their packs (first try).** A Support bot
  drops his ammo pack, and a Medic his health pack, when two or more teammates
  are close and no enemy is in sight. Then he goes back to his gun. At most
  once a minute; the game's own recharge timer still decides whether the pack
  is ready. The log says each step ("bots: pack ...").
- **German bots get German smoke grenades** (smokegrenade_axis, the Germans'
  own).
- Smoke throws are logged more fully (the log had stopped counting them).
- Map learning: mp_guns learned 687 places in your test, added to Merville's
  1,054.

- **The Gewehr 43 is in (from your friend's team, their 0.5.900).** The
  campaign's G43 with its own models, animations and real campaign weapon
  numbers, in Create-a-Class (main weapon or second weapon). And **every
  campaign gun now plays its own campaign gunshots and reloads** (Sten, Bren,
  FG42, FG42 Scoped, Lee-Enfield, G43). Their full notes are below. Keep
  native_commands = 0 in CoD3Recomp.ini.
- Bots can carry the G43 too: bot_kits.txt has a "rifleman gewehr43" line
  (added to your existing file; new files get it by default).

### Merged into 0.5.199: Gewehr 43 in multiplayer (Team Friend's 0.5.900, on the 0.5.193 package)

- New campaign gun, the Gewehr 43 (Forest's), made like the Lee-Enfield,
  Sten, Bren and FG42: a copy of the M1 Garand with the G43's own view and
  world models (w_g43_VM, g43_world), HUD icon and `geg43` animations.
  Appended after the Mills bomb, so every existing weapon keeps its number;
  custom class primary 19 / sidearm 21 (older saved choices unchanged).
  Forest's `sp\forest\weapons.cod` is campaign archive 2 (big_memory only),
  `forest.wbk` its sound bank (Fuel Plant's slot null).
- Its own numbers: `G43Fields[]` in mp_fixes.cpp sets 93 fields from the
  campaign G43's weapon file (read from memory in single player Forest)
  wherever it differs from the Garand's, the file's MP value where it has
  one, at the offsets of the game's own field table (0x825D77D0): 10-round
  magazine, 120 reserve, fireTime 0.1 s and fireDelay 0.1 s, reload 3.95 s
  (top-up 2.5 s), partial reloads allowed (noPartialReload 0), adsZoomFov
  65, melee 150, drop/raise 0.4 s, its recoil, sway, spread and view
  offsets. Damage stays the Garand's (84, 53%, 1000/3000) on purpose: the
  campaign file's MP damage (100, to 8000/10000) was never multiplayer's
  balance (its Garand says 50 where multiplayer's is 84).
- Every campaign gun plays its own campaign sounds now: the G43
  (GEWEHR43_FIRE_1ST/3RD, GR_G43 from forest.wbk), and the Sten, Bren,
  FG42, FG42 Scoped and Lee-Enfield (STEN_FIRE, BREN_FIRE, fg42_FIRE,
  fg42sniper_FIRE, LEE_ENFIELD_FIRE; GR_STEN, GR_BREN, GR_fg42,
  GR_enfield_reload, GR_Enfield_rechamber from mace2.wbk). Before, none of
  them did in multiplayer (the Bren and FG42 both played the BAR):
  - The campaign banks were only registered with the bank manager
    (sub_824964E8), never loaded into the sound engine. Now they are, as
    the map's is: sub_824AE270(manager [0x82BCBF7C], key, 1).
  - The engine's bank memory (start-up configuration +24/+28,
    sub_821297E8) was 0x880000 from the map block: too small for
    forest.wbk (8.8 MB) and mace2.wbk (13.8 MB), so the loader let them go.
    Taking 36 MB more of the map block hung the map load. Now the engine
    is given its own 36 MB of physical memory (Guest::AllocatePhysical);
    the map block stays 480 MB.
  - Shot events are named weapon_<copy>_weapon_<what> in this game
    (weapon_bar_weapon_fire_1st); only bare <copy>... names were matched,
    so no campaign gun's shot was ever swapped. Both forms now
    (CampaignDonorEvent).
  - The engine searches slots in order and the campaign banks sit before
    the map's, so multiplayer's own guns would have found the campaign
    versions of shared names (THOMPSON_FIRE_1ST is in forest.wbk). The
    look-up now searches every other slot first, the campaign banks last
    (OrderedCueLookup).
  - Reloads: each copy's reload cue becomes the gun's own
    (CampaignReloadCue), and the campaign animations' gr_* names play the
    bank's own cues instead of the copies'.
  - A campaign cue still not found falls back to the copy's, never silence.
  All only with campaign_guns and campaign_sounds on.
- `campaign_animation.h`: the G43's own ADS and fire animations for the
  Garand moves it has none of. `campaign_sound.h`: the Garand's events used
  for it, its clip ping/ejection muted, its reload cue GR_M1G -> GR_G43.
- New ini `g43_reload` (seconds, 0 = 3.95) to tune without a build.
- IMPORTANT: `native_commands = 1` (F11 "Native command lists
  (experimental)") loses multiplayer world geometry; keep it 0.
- Tested by the user in an offline match: models, map, firing, sound,
  reloads (partial too) and handling "perfect". UNTESTED: System Link
  between two PCs (both need this exe), bots with the G43 (not given to
  them).

## 0.5.198: Bot kits you can customize, many more grenades, fixes from the 0.5.197 test

- **Bot kits are yours to set.** A new file, bot_kits.txt, sits beside
  CoD3MP.exe. The game writes it the first time with good defaults. Each line
  is one kit:
  - how often it's used
  - the class it builds on (support, light, rifleman, medic, heavy,
    antiarmor, scout)
  - the main gun: the class's own, any multiplayer gun, or the campaign guns
    (lee_enfield, bren, sten, fg42, fg42_scoped)
  - how many frag and smoke grenades
  New bots take whichever kit their team has least of. Save the file and the
  next bots you add use it, no restart needed.
- **Way more grenades.** Every bot now gets frags and smokes by default.
  Classes whose kit has no grenades are given them when they spawn (for
  example, riflemen had no frags and most classes had no smoke). Bots throw:
  - frags about twice as often, and also at enemies bunched together or who
    just ducked behind cover
  - smoke when they're hurt or falling back, sooner than before
  - smoke ahead of themselves when they run for the flag or charge in from
    range
- **Fixed: every gun walked slowly while aiming in 0.5.197.** The scope check
  matched every weapon, so all guns aimed-walked at the sniper speed and
  swayed half as much. Now it's only the scoped rifles (snipers and the
  scoped FG42).
- **Fixed: bots never got into vehicles.** A bot picked a vehicle and gave up
  on it in the same moment (78 times in your log). They now walk over and
  press use.
- **Fixed: a bot standing above an enemy he can't reach.** One bot stood five
  minutes on a floor right above his enemy, looking straight down. Now an
  enemy close by but a floor away, and out of sight for 6 seconds, is left
  alone for 20 seconds.
- **Last resort for stuck bots:** a bot that hasn't gotten anywhere for 70
  seconds goes back to spawn.
- **Bot errands use learned routes.** In 0.5.197 most errands walked straight
  at far spots and half were given up. Bots now pick spots a learned route
  reaches, or something close by. Merville already has over 1,000 learned
  places saved from your test.
- **More players study:** the probe showed the free-looking memory after the
  player data is in use. More players means moving that data too, so it's a
  bigger job (see PLAYER-LIMIT-STUDY.md).

## 0.5.197: Bots learn the maps, use vehicles and mounted guns; HD textures; sprint and sniper feel

- **Bots learn each map as you play.** Every half second the game notes
  where everyone walks (you and the bots), how people get from one spot to
  the next (stairs, ladders, ramps, drops), and where people die and get
  kills. The bots plan routes over the places players have really walked. It's
  saved per map (bots\maps\<map>.txt beside CoD3MP.exe), so they get better
  every match. The first match on a map they still roam the old way while
  they learn; after a few minutes they start using it.
- **Bots spread over the whole map.** When no enemy is close or in sight,
  each bot has his own kind of spot:
  - some walk the outskirts,
  - some climb to high ground and watch from there (lying down or crouched),
  - some go down into low ground (trenches, gullies) and wait crouched,
  - the rest hunt as before. Campers take the high ground.
  They pick spots away from where other bots are going, and avoid spots where
  players keep dying. A bot who dies three times on his way to his kind of
  spot tries a different kind.
- **Bots adapt to how they're doing.** A bot on a streak plays bolder; one
  who keeps dying plays it safe from cover.
- **The frozen bot is fixed.** In your last test one bot stood 3.5 minutes
  in one spot. He'd reloaded next to a mounted machine gun (reload is also
  the "use" button) and got stuck on it. Now a bot on a mounted gun fights
  from it while an enemy is in reach, and gets off when nobody is. If that
  fails he's sent back to spawn. Bots remember where mounted guns are:
  they don't reload there by accident, and campers sometimes man them on
  purpose.
- **Bots in vehicles (first try).** Up to two vehicles a team get a bot
  crew: a driver and a gunner. They walk to a free vehicle and get in. The
  driver heads for the War flag or the map's outskirts and backs up when he
  gets stuck; the gunner and riders shoot from their seats. A bot will also
  hop in beside you when you're driving nearby. They get out after two
  minutes or if the vehicle is stuck. The log says every step ("bots:
  vehicle ..."), because steering and getting in are untested: please send
  the log. F11: "Bots use vehicles and mounted guns" turns this off.
- **Sprint isn't unlimited any more.** Run out of breath and you have to walk
  for 1.5 seconds before you can sprint again. When you stop sprinting
  yourself, your breath starts coming back after 0.75 s and is full in
  3 s. (0.5.42 had refilled it almost straight away.)
- **Sniper scopes:** walking with a scope up is slower (30% of normal; other
  guns keep their aiming walk speed), and the scope sways half as much. Both
  are in F11: "Scoped walk speed" and "Scoped sway". Hold breath still
  steadies it further.
- **HD textures for the world.** The texture swapper from 0.5.70 (menus) now
  has a tool for whole maps: UPSCALE-TEXTURES.bat upscales a map's
  textures 4x on your graphics card (bushes, trees, walls, vehicles, guns).
  See HD-TEXTURES-README.txt. They live on your graphics card, not in the
  game's memory, so they need no extra game memory. The game no longer
  stutters while it saves textures (they're written in the background), and
  each map now gets its own list of the textures it uses.
- **The Kangaroo for your friend:** the Kangaroo comes from your
  vehicles.txt, which the friend update never included, so his game showed
  the map's normal vehicle there. MAKE-FRIEND-UPDATE now packs it and the
  installer puts it in place. Big HD texture folders stay out of the update
  zip (use MAKE-TEXTURE-PACK.bat for those).
- **More players study, first probe:** the log now says whether the free
  memory after the 24 players' data stays unused during a match ("player-limit
  probe"), and where the lobby's player list lives. Nothing changes in play.
- Bots' kits are logged once a life ("bots: kit ..."), to find how the
  Support's ammo packs and the Medic's health packs are used, so bots can
  hand them out next.
- Found: the "model detail" setting has never done anything. The game never
  reads r_lodscale/r_lodbias (leftovers from the Quake 3 engine). The real
  detail levels are per model and SpeedTree's; that's later work.

## 0.5.196: Bots get unstuck, spread out, use ladders and drop off ledges

- What you saw on 0.5.195: bots stuck up high, near ladders they never used,
  and bunched up in front of obstacles. Your log showed why. A stuck bot
  jiggles back and forth, so the old check (did he move a bit this second?)
  never called him stuck. One bot stood in one spot for 20 seconds like that.
- Stuck now means "hasn't gotten anywhere in 3.5 seconds", jiggling or not.
  Each time it happens he tries harder: first another way round (and he
  remembers that spot), then a jump, a drop off a ledge, or a ladder, then
  a short wander in a fresh direction to break free.
- Spreading out: teammates closer than about two metres push each other
  aside. Bots going for a flag each pick their own spot around it instead of
  all running to the same point.
- Ladders: bots now look for ladders all around them, out to about 4 metres
  and at two heights (before: straight ahead, about 2 metres). They look when
  their target is above or below them, or when they're stuck. On a ladder they
  now climb DOWN when what they want is below them.
- High ground: bots can step off ledges up to about 4.5 metres when their
  target is below. That's under fall-damage height. They never step into a
  pit or walk through a wall.
- To test (solo is fine): same as before, and watch the spots where bots got
  stuck. Log lines: "stuck (no progress ...): tries ...", "approaches native
  ladder", "drops down toward a lower goal".

## 0.5.195: Smarter bots: prone, cover, smoke, melee charges, flanking, and no more jump loops

- What you saw on 0.5.191: some bots fail a jump, then keep trying the
  same jump instead of finding another way.
- Now a bot remembers where he got stuck. That's a jump that left him in
  the same spot, or walking straight into something for about 3 seconds.
  He won't try that same move again for 45 seconds, and longer if he keeps
  failing there. He turns and takes a different way round instead. He
  remembers up to 6 spots at a time, keeps them when he dies and respawns,
  and forgets them on a new map.
- To test (solo is fine): host a match, add bots, and watch the ones that
  used to get stuck. They should give up on a bad jump after one try and
  head another way. The log says "jump at ... failed (strike 1 ...): going
  round" and "stuck going to ... rerouting" when it happens.
- Prone: bots now actually go prone. They go flat to shoot at long range,
  when they're hurt, when they're playing defensive, and in cover when badly
  hurt. They stand back up to move or jump.
- Cover: besides seeking cover when hurt or reloading, they sometimes take
  cover in a long gunfight too (rushers don't). In cover they pop up to shoot
  and duck back down every 1.5 seconds, or lie flat if badly hurt. They throw
  smoke to cover the run when they're at half health or less.
- Falling back: badly hurt (35 or less) with you close, a bot turns to face
  you, throws smoke between you, then sprints away for about 3 seconds.
- Melee charges: close in (under about 6 metres) with an empty gun, a big
  health lead, or an aggressive mood, a bot sprints straight at you to melee.
- Aggressive or defensive: each fight, a bot picks a style from his role and
  his health versus yours. Aggressive bots close in and strafe hard. Defensive
  bots keep their distance, stay low and go prone.
- Flanking: flankers (and some objective bots with no flag to play) go round
  the side to come at you from an angle instead of running straight at you.
- Tracking: bots now fight whoever they can actually see, not the nearest
  enemy behind a wall. When you duck out of sight they aim at the spot (and
  height) where they last saw you for 3 seconds, so they look up stairs and
  down drops instead of staring through the floor.
- Log lines to look for: "goes prone / crouched / standing", "charges in to
  melee", "falls back behind smoke", "flanks round to", "throws smoke".
- Also in this build: 0.5.194's host-only bot list (still needs a test with
  your friend).

## 0.5.194: The host's bots for everyone in the match

- What you two found: bots showed up only on the PC that ran bot_add. Your
  friend's log and yours explain it. Each PC keeps its own list of the players
  in the match, and a PC only shows a player who is on that list. Bots were
  only ever on the host's list, so the other PC had nowhere to put them.
- Now the host's game sends everyone a bot list (name, team, class) every
  second. A joined player's game adds the host's bots to its own list the same
  way the game adds a real player who joins. Nobody else types anything, and
  the bots still run only on the host.
- bot_add now works only for the host. A joined player who types it gets
  "only the host can add bots" in the log, instead of ghost bots that only
  he could see.
- To test: your friend (or you) hosts and adds bots. The other one joins and
  does NOT add bots. Check whether the joined player sees the bots, whether
  they move, and whether they show on the scoreboard. Then send both
  CoD3MP.log files (from both PCs, before launching the game again).
- Not done yet: a bot removed on the host may stay on the joined player's
  screen until the next map. The bot jump fix is next after this.

## 0.5.193: Varied native/custom bot kits and primary weapon selection

- User confirms191 bots now move. Some still fail jumps and retry the same
  obstacle; explicit failed-route memory is the next separate patch.
- Balance11 loadout templates by team count, then total count: seven native
  kits plus existing FG42, Sten, Bren and Lee-Enfield custom-primary kits.
  Seven native classes remain0..6; custom primaries use suitable base kits,
  NOT invented out-of-range native class IDs. Bots retain their assigned kit.
- Enforce assigned base class inside native82206028 kit callback. Override
  its main-weapon name through existing WeaponName/CampaignStandIn handling,
  so native grant/ammo/model setup and map fallback remain in use. Humans'
  custom classes and sidearms are unchanged.
- Live191 proved slot order differs: rifle/pistol in either1065/1066, and
  previous comment claiming fixed secondslot primary was wrong. Examine both
  slots, reject Colt/P38 as primary, require owned valid definition and ammo.
  Request armed primary through normal usercmd; preserve sidearm fallback
  when primary runs out. No forced unlimited ammo or fabricated ownership.
- Tests PASS balanced20-bot distribution/all11 profiles, custom four present,
  both inventory orders, pistol-only/empty/unowned rejection and ammo fallback.
  MP/SP builds PASS;191 movement/aim and death/network/input hooks preserved.
  Four installed hashes match. Live loadout/model/respawn verification pending.
-192 prototype was never installed; this corrects its slot assumption before
  testing.189 peer visibility remains unconfirmed despite all20 native sender
  schedules advancing. No GitHub push/package/game file upload.

## 0.5.192: Bot loadout prototype - built, not installed

- Prepared11 balanced loadout templates: seven native kits plus existing
  FG42, Sten, Bren and Lee-Enfield through the native custom weapon-name path.
- MP/SP builds and loadout policy tests PASS. Not installed: live191 memory
  inspection exposed unreliable inventory ordering (most firstslot rifle,
  second pistol; some reversed). Prototype assumed secondslot primary.
  Kit callback can also receive another class despite saved record fields.
- Superseded by193 before user testing. Fix both inventory-order assumptions
  and class enforcement in the native kit callback. Preserve191 movement.

## 0.5.191: Use native rounded-capsule navigation traces

- User190: many bots on both teams still stand at spawn. Live20-bot match
  confirms several living bots repeatedly have route0 and move0/0; others
  move.189 counters advance for all20 bots, but peer visibility is untested.
- Navigation body checks were box traces (r9=0). Native82571EA0 selects
  rounded-capsule mode with r9=1 into82534200, forwarded into824A0468.
  Use that mode for body clearance/support; point sight rays remain0.
- Add throttled completed-result trace logs, including actual fraction,
  solid flags, normal and hit handle. Read-only external scratch sampling
  can catch a query in flight; zero fields there are not completed results.
- MP/SP builds PASS; source checks verify capsule wrapper contract and
  unchanged190 AI/network/death/input code. Four installed hashes match.
  Startup/native live movement test pending; do not claim idle bots fixed.
- User also reports pistol spawns and requests diverse classes, including
  already-customized FG42, Sten, Bren, Lee-Enfield. All four are loaded in
  the current live MP weapon table (names verified). Loadout patch next.

## 0.5.190: Keep bots moving through tight spawn areas and doorways

- Route checks now try normal feet height first; only use an18-unit step
  after checking upward clearance. Previously raising the entire hull could
  reject a doorway the standing player can walk through.
- If the96-unit detour has no route, try a bounded32-unit supported escape.
  Still require body clearance and floor support. No forced movement through
  walls, teleportation, invented ladders or paths across missing floor.
- Long-range shooting pauses now require a visible enemy. Hidden enemies
  no longer cause the bot to stop and crouch to aim through a wall. Campers
  patrol8seconds before a maximum3second no-enemy lookout pause, replacing
  the old40-60second spawn hold. Objective flag defense/cover holds remain.
- Add5second per-bot mobility diagnostics: role, health, position, requested
  movement, route availability, stalled samples and visibility.
- Geometry tests PASS: low doorway, too-low ceiling, cramped-spawn escape,
  enclosed/void rejection, wall detours, low jumps and ladder detection.
  MP/SP builds PASS.189 network integration and185 death/input/crash hooks
  preserved;188 drop rearm preserved. All four installed hashes match.
- Includes189 send-clock isolation. Startup PASS: visible190 MP menu144FPS. Hosted live movement and two-PC
  bot visibility pending. Local collision steering remains bounded; this is
  not a global map navigation mesh or completed squad-intelligence rewrite.

## 0.5.189: Isolate each bot's native network-send timers

- User188 confirms host sees dropped guns. Friend running the same188 can
  see the host but ALL bots are invisible and can kill him. Both teams have
  bots stuck after spawning. Pick-up usability remains unconfirmed.
- Native82403840 uses CurrentClient-indexed full/near/far send timers at
  object+(8884/8888/8892+client)*4. All bots borrow client3, so one consumes
  that timer before the next bot runs. Save/load persistent timers per bot,
  restore scratch client's timers, reset on changed session/player identity.
- Add per-bot native serialization/send-call counters for diagnosis. These
  are calls, NOT proof of peer receipt, roster creation or rendered models.
- MP/SP builds PASS; scheduling test PASS for20 independent bots across
  3seconds, scratch restoration and changed identity reset. Think unchanged.
- Built checkpoint; not installed separately. Included in the next movement
  build. Remote visibility still requires a two-PC test; roster/receiver
  diagnostics may still be needed. No guarantee this alone fixes all peers.

## 0.5.188: Re-arm bot weapon drops after each completed respawn

- A fresh kill in 187 produced no drop attempt: asynchronous respawn could
  leave the previous life's drop flag set. Reset it when the bot is observed
  alive again, preserving one native drop attempt per corpse.
- Log fresh pickup handle, position, model, rig and expiry for inspection.
  Native item/model/network handling is unchanged.
- Lifecycle tests PASS across eight asynchronous respawns; existing capture,
  duplicate and ownership tests PASS. Source preservation checks and MP/SP
  builds PASS; all four installed artifact hashes match, VERSION 188.
- Startup and live visible/pickable drops remain pending. Initial 187 native
  creation succeeded, but that alone does not prove rendered pickup models.

## 0.5.187: Run the native weapon-drop path for bot deaths

- User186 reports bots can kill them while prone; dropped guns still absent.
  Native drop requires a local-player place; bots deliberately occupy none.
  Their custom respawn also skips the normal network receiver's drop call.
- Capture the held owned kit weapon before native death clears it; queue one
  native drop on the dead frame, before respawn. Temporarily satisfy only
  that bot's local-place check within that drop call, using its isolated
  context. Keep native ammo/eligibility, item creation and network handling.
- Log whether native pickup creation returns an entity, separating missing
  creation from invisible models. Failed creation restores temporary slot
  order; repeated death callbacks cannot duplicate drops in the same life.
- Lifecycle tests PASS; sourceguards PASS aim/navigation/crashguard unchanged.
  MP/SP builds PASS; installed with no gameprocess, all4artifact hashes match,
  VERSION187. Startup PASS: visible MP menu144FPS. Live dropped-weapon
  creation/visibility still pending.
- UAV markers, idle behavior, remote visibility and renderer list's original
  corrupting writer remain open. No claim these are fixed in187.

## 0.5.186: Correct bot aiming above/below the target

- Native firing math uses forward.z=-sin(pitch), while the bot used the
  opposite pitch sign. Live185 sample confirmed a higher target needed
  -2.88 degrees but the bot used+2.89. Correct pitch for both command and
  shot; grenade loft follows the native upward sign too.
- Normalize wrapped viewpitch before learning spawn offsets. Preserve
  valid crouched/prone muzzle height instead of raising all bots to60.
- Tests PASS: nativevector alignment across elevations/yaw,16bit wrapping,
  stance eyeheight/fallback and grenade loft. Source guards PASS: navigation,
  team assignment, shot scheduling and185crash guard unchanged. MP/SP builds
  PASS; installed after gameclose; all4artifact hashes match, VERSION186.
  Startup PASS: visible MP menu144FPS. Live vertical aiming unconfirmed.
- USER185: held guns visible and bots killable. Snapshot logged11 death
  calls (including host),4 prevented corrupt-list copies, no queuewatch write.
  Root list corruption remains under investigation. Missing dropped guns,
  bot UAV markers, idle behavior and paired-client visibility remain open.

## 0.5.185: Guard the renderer-list clear implicated in bot death crashes

- Both183 and184 logs captured the same writer clearing a critical pointer
  after bot death: native820D3638 from82436FC0.184 crash evidence archived.
- For the exact temporary renderer list, discard old elements before the
  native negative-capacity clear branch allocates0 bytes and copies them.
  Retain native allocation/free cleanup; other lists and normal clears run
  unchanged. Log original metadata and reset completion for live diagnosis.
- Regression PASS: reproduced zero-byte-copy failure; guard prevents it while
  preserving cleanup, nearby canaries and normal/empty/other-list behavior.
  Source guard PASS: all184 navigation/team/weapon/death/input hooks unchanged.
  MP/SP builds PASS; installed with no processrunning, all4artifact hashes
  match and VERSION185. Startup PASS: visible MP menu144FPS.
  Live bot-kill test pending.
- Crash fix remains unconfirmed in-game; reason for negative metadata still
  under investigation. Prone aiming and remote visibility remain unresolved.

## 0.5.184: Persistent obstacle detours, supported jumps and native ladders

- Moving roaming bots now use obstacle routing too. They keep a chosen
  waypoint until reached/blocked, remember recent positions, and search
  alternate routes using bounded body sweeps and floor-support checks.
- Try low obstacles with a clearance/landing-tested native jump. Failed
  attempts back off for8 seconds and seek another route; blind/random hops
  removed. No teleporting through geometry.
- Nearby native ladder surfaces get a supported approach; once attached,
  native forward input climbs. Combat aiming no longer turns the bot away
  while climbing. Failed progress triggers cooldown and alternate routing.
- Helper tests PASS: wall detour, query cap, enclosed/no-floor rejection,
  low jump, tall obstacle/roof/gap rejection and ladder-only detection.
  Source guards PASS: all four roaming branches routed; death/render/input
  isolation hooks unchanged. Final MP/SP builds PASS; all4 installed
  EXE/PDB hashes match; VERSION184. Startup PASS: visible MP menu144FPS.
  Live obstacle/jump/ladder behavior remains unconfirmed.
- User183 confirms visible held guns. Remote teammate/dropped gun visibility
  remain unconfirmed. Reported3v1 bot split is expected with a solo host
  (total3v2). Also reports prone-target aiming still shoots overhead.
- Combat crash reproduced183; watch captured the actual clearing writer
  (820D3638 from82436FC0) after bot death. Evidence archived locally.
  Crash and aiming are NOT fixed by184. Navigation remains experimental;
  local collision routing is not a complete map navigation mesh.

## 0.5.183: Balance new bots across both teams

- User requests bots alongside either human, visible from either team;
  chose automatic balancing. Old code forced all bots opposite the host
  during setup, session creation, every Think correction and respawn.
- Each new bot now joins the smaller team, counting connected humans and
  assigned bots. Ties choose opposite host. Bot retains its own side across
  respawns and host team changes; newly created slots get a fresh choice.
  Existing bots are not shuffled when humans change teams or disconnect.
- Tests PASS: solo hosts either side, friends together/opposed, 20 additions.
  All six setup/session/think/respawn call sites checked; death/render hooks
  unchanged. MP/SP builds PASS (existing kernel printf warning).
- Installed after user closed182; no process verified, all4EXE/PDB SHA256
  matched; VERSION183. Startup/live team assignment and paired
  client visibility pending. This fixes assignment; remote visibility is
  still unresolved. Weapon fix182 also awaits live visual confirmation.

## 0.5.182: Recover an owned gun when a bot's kit changes

- Latest181 runtime: many empty-handed bots request weapons17/18/29 despite
  inventory00801300 (owns8/9/12/23; kit slots12/8). The remembered lastHeld
  weapon survives class/respawn changes; native selection rejects that stale
  unowned request.8280 matching empty/unowned command observations in log;
  count is repeated observations, not distinct bots or independent trials.
- Only when held AND pending weapons are0: retain remembered weapon if still
  owned; otherwise choose owned primary/secondary from current kit, or0 when
  kit notready. Submit normal usercmd selection; no forced model/weapon writes.
  Existing ammo switching, teleport and death/watch/event routines unchanged.
- Actual helper tests PASS stale IDs fromlog, valid cached gun, secondary/no-kit,
  invalid IDs and changedkit. Source checks confirm death/watch sections intact.
  MP/SP builds PASS; installed with no gameprocess, all4EXE/PDB SHA256match,
  VERSION182. Logsarchived. Startup PASS: visible multiplayer menu at144FPS.
  Live held/dropped visibility still unconfirmed.
- User confirms latest save/return works perfectly (181 uses heldX2sec).
  New network report: friend could not see bots on his team; joining hostteam
  made them visible. User saw friend/bot scores on kills. Exact friendroster
  status unconfirmed. Track team-dependent remote bodyvisibility separately
  from weapons/scoring. No friendpackage produced: user updated it themselves.

## 0.5.181: Hold X to go back to your saved point, from anywhere

- **Justin:** holding R3 kept saying "RETURN POINT BLOCKED" (or rejected the
  hold); he wants it to take him back to wherever he saved, from anywhere,
  even on odd geometry.
- **Hold X (Xbox controller) for 2 seconds** to go back (was R3; Justin
  asked for X). Works while moving, aiming down sights or on any ground;
  it needs only that you're alive, in play, not in a menu and not in a
  vehicle. X still reloads/uses as normal on a press; only a 2-second hold
  returns you, and "RETURNING" shows once it has been held half a second.
- **Never blocked:** the game's check for clear floor at the saved point is
  now only used to fine-tune the landing. If it finds none, you're put on
  the exact spot you saved, which is where you were standing.
- **Unchanged:** saving with L3 (hold 1 second) still needs you standing
  still, so sprinting isn't affected. R3 is plain melee again. Holding X
  also starts a reload if your clip isn't full.
- **Test:** save with L3 somewhere odd (a slope, a ledge), walk off, hold X
  for 2 seconds while moving and while standing; tap X to reload as usual.

## 0.5.180: Two-second R3 return hold

- User confirms179 teleport worked perfectly; requests R3hold2seconds.
- R3 return now requires2000ms continuously held while stationary/eligible.
  L3 save remains1000ms. Save/return notices reflect2seconds.
- Actual timer tests PASS: R3no action at1000/1999ms, action at2000ms,
  release/cancel/no-repeat and clockwrap; L3 still1000ms. MP/SP builds PASS.
  Bot/death/watch/UAV logic unchanged. Other botissues remain unresolved.
- Installed with no gameprocess; all4 EXE/PDB hashes match, VERSION180.
  Startup180 PASS: visible menu leftopen. Live2sec test pending.

## 0.5.179: Hold R3 one second; save the current position directly

- Later178 log DOES show one savedpoint and three native returncalls. Earlier
  no-success snapshot was incomplete. Userperception still pending; original
  failure cause not established.179 retains requestedR3hold/feedback changes.

- User: unsure L3 saved; R3 did not return. Requests holding R3 one second
  and saving wherever standing.178 log had no tactical-point success lines.
- Both L3(save/replace) and R3(return) now use separate one-second hold timers,
  fire once until release, cancel on movement/menu/ADS/death. Save captures
  current finite player coordinates directly; no save-time landing trace.
  Return retains the full-box grounded/clearance trace and native origin update.
- Replace volatile render-triangle eligibility with native local-state2;
  allow stationary crouched/prone positions and tolerate normal vertical
  gravity samples. Host live health/state/pause/ADS/velocity/local-state checks
  were valid. Raw hold detection retained even when gated, enabling rejection
  logging. Notices show saving, saved, returning and missing-point status.
- User latest: bot weapon models absent both in hands AND after weapon drops.
  Unresolved; bot combat/death/watch code unchanged. Previous statement that
  bots were untested is superseded by this visual report; crash still unconfirmed.
- MP/SP builds PASS; actual routing/timer tests PASS for independent1sec holds,
  release/cancel/no-repeat, raw diagnostics, moving melee, ADS and keyboard.
  Installed after no process verified; all4 EXE/PDB SHA256match, VERSION179.
  Startup179 PASS: visible MP menu leftopen. Live179 hold test pending.

## 0.5.178: Experimental stationary L3 save / R3 return point

- User requested a fun tactical-insertion-style return point: hold L3 while
  still to save; clarified R3 teleports back. A new one-second hold replaces
  the saved point. It is a manual return point, with native respawn unchanged.
- Controller-only physical L3 sampled before sprint filtering. Hold must remain
  stationary, upright, alive, on foot, out of ADS and menus for one second.
  Moving L3 keeps sprint; scoped L3 keeps steady aim. With a saved point,
  stationary R3 tap returns once per press; moving R3 keeps melee. Keyboard
  sprint/melee unaffected. Point resets with host/map-clock change.
- Full player-box native trace checks ground/clearance before saving or return;
  blocked positions reject with notice. Native8255E118 updates entity/player
  origins and teleport bit; clears velocity on return. On-screen save/return
  notices and tactical-point log lines. No game files uploaded.
- Bot crash/watch/death/UAV routines unchanged.177 live log confirms pointer
  watch/helper armed; user reports some bot weapons still invisible. Crash,
  unwanted movement, partial guns, UAV and artillery remain unresolved.
- MP/SP builds PASS, including final controller-only refinement. Actual input
  routing/hold tests PASS: timing, one-shot, release/cancel, wrap, ADS, moving
  sprint/melee, keyboard and missing-marker behavior. Native teleport test
  pending. Installed after userclosed177; all4EXE/PDB hashes match, VERSION178.
  Startup178 PASS: visible multiplayer menu, leftopen; live teleport pending.
  Both build stages, test/scope
  logs and177 runtime snapshot archived in docs/build-logs.

## 0.5.177: Trace the first writer clearing the native error-queue pointer

- 176 FAILED extended combat: user reports crash and residual unwanted movement.
  Kills of8,15,7 credited. All three native death before/after headers valid0.
  After thirdkill, unrelated globals become0 and invalid indirectcalls occur;
  queue POINTER becomes0, so176's countFFFFFFFF was guest0, not queue header.
  No producer enqueue logged. Do not describe this as a proven queue overflow.
- Existing hardwarewatch armed synchronously from AddBot skipped its own/main
  thread (ArmWatchpoints explicitly excludes caller).177 changes target to
  queue-pointer global82A87860 and arms from a helper so main is included.
  Existing exception handler should name the writer; runtime watch still needs
  verification. No gameplay/death-path redesign in this diagnostic patch.
- Null queue now returns without reading or clearing guest0. Previously176's
  count quarantine could write0 to guest0 after pointer destruction, itself
  touching the host weapon view. Other valid queue processing unchanged.
- MP/SP builds PASS; extracted actual drain wrapper tests PASS including null
  queue no-write, valid0..8 native calls, oversized counts quarantine.
  Installed while no gameprocess; all4 EXE/PDB hashes match, VERSION177.
  Startup177 PASS: visible menu, leftopen. Combat/writer-watch test pending. Crash, movement, UAV, weapons and artillery
  remain unresolved; this is diagnostic containment, not a crashfix claim.

## 0.5.176: Trace and contain the repeated bot-kill crash

- 175 user test FAILED: killing a bot crashed. Same native824309F0 drain as172;
  175 fault RVAa84002 guest13E10088 after2GB demand commits. Preserved full log.
  Bot7 died, host kill credit logged; native death/animation/script steps ran.
- Further native analysis identifies the list as the ERROR MESSAGE queue,
  not a draw-command list.8210DCF8 only admits8 entries (260bytes each),
  constructor8210DD70 allocates8*260. Fresh175 menu read-only snapshot:
  queue82685ED0 count0 buffer50113440. No evidence yet of what corrupts it.
- Diagnostic176 logs error enqueue format/arguments/caller, queue header before
  and after player death, and count/header before native drain. If count>8,
  log and reset that invalid count instead of scanning arbitrary guest memory
  up to2GB. This is containment, NOT a confirmed fix for the original corruption.
  Valid0..8 counts retain native processing.175 stance-event fix retained;
  live175 logs show bot165 events intercepted under client0, caller8246BBF0.
- MP/SP builds PASS; extracted actual queue-wrapper tests PASS (valid0..8
  delegate,9/1000000/FFFFFFFF quarantine without native walk). Installed after
  menu session closed; all4 EXE/PDB SHA256 hashes match; VERSION176.
  Startup176 PASS: visible multiplayer menu, left open. Match test pending.
  Native producer/death evidence and logs in docs/build-logs.
- Live176 first kill PASS: bot8 death/native scripts, hostkillcredit and respawn;
  process responding. Error queue0 both before/after, no containment triggered.
  One kill only; repeated kills and prone confirmation still pending. Crash
  fix notclaimed. Runtime snapshot archived in docs/build-logs.


## 0.5.175: Keep bot stance events out of the host's local controls

- 174 FAILED the prone test: user still forced up after1-2sec. Its extra
  stance-latch save did not resolve the source of the commands.
- Live isolation on174: debug bot_sticks1,0,0,0 disables AI commands but retains
  bots/session processing. User confirmed stays prone. Then bot_buttons1 makes
  them fire without AI stance/jump/movement: user again confirmed stays prone.
- Native8246A7D8(entity,event,param) cases165/166/167 write stand/crouch/prone to
  stance latch82B57804[CurrentClient], then return. These are local input
  effects, distinct from the bot body movement. Events can be handled outside
  the isolated bot think/session scopes, with the host's client current.
- Skip only165..167 for validated bot entities; host and every other event
  continue through native handler. Log source bot, event, current client/caller.
  This is the only gameplay change from174. Death, UAV, weapons, artillery code
  unchanged. Does not disable AI stance commands or clamp the host prone.
- MP/SP builds PASS; actual-wrapper test PASS768 cases (all host/unknown
  sources delegate; only bot165..167 skipped). Native-case review PASS.
  Installed after userclosed174; all four EXE/PDB SHA256 hashes match, VERSION175.
  Original debug.txt restored and verified: AI buttons/movement active again.
  Startup175 PASS: visible multiplayer menu, left open. Live match test pending.
  Build/tests/runtime evidence archived in docs/build-logs.

## 0.5.174: Isolate the host stance latch and normal bot session context

- 173 user result: cannot remain prone longer than 1-2 seconds; UAV still has
  no bot markers; only some bot guns appear. Artillery also fails to fire.
- Read-only173 trace confirms host stance command bits change alongside eye
  height. Native82525338 consumes a separate stance latch at82B57804, outside
  171's kbuttons/HostInput saves. Native82571930 clears all four stance latches.
- Preserve host latch plus slot0 hold fields (+1608..1619) across isolated bot
  steps; log any restoration. Normal82400810 bot updates now use the bot's
  private gun view, scratch local context, and command slot, just as explicit
  bot think does. Previously the normal session tail could run them under0.
  ReKitBots now uses BotClientScope instead of calling its script under host0.
- This patch targets bot context/input isolation. UAV and death code unchanged.
  Live173 weapon snapshot: many living bots have in-hand/pending0 with owned
  guns; visible guns have nonzero IDs and a second rig part. Marker timestamp
  team+72 is0 for most bots; radar inactive at snapshot, so no marker fix claimed.
  Artillery173 log reached ready (520B5264), failed firing step still unresolved.
- MP/SP builds PASS (existing kernel printf warning), source checks PASS.
  Installed after user closed game; all four EXE/PDB SHA256 hashes match,
  VERSION174. Startup smoke test PASS:174 visible at multiplayer menu, left open.
  Live match test pending.

## 0.5.173: Restore bot weapon/session updates with narrower input isolation

- 172 live test FAILED: bot weapon models missing; shooting a second bot crashed.
  User confirms kill-feed credit AND death animations still work.
- Preserved 172 crash log in work/CoD3MP-0.5.172-crash.log. Matching PDB resolves
  fault RVA a83152 to native824309F0, a 260-byte-entry renderer work-list drain;
  its read reached guest165000FC after 2GB of demand commits. This identifies
  the failure site, not the writer that corrupted the list. Cause unproven.
- Restore the 171 supplemental bot session updates removed in172. Require each
  bot's private gun-view block, explicitly lock physical input for scratch
  client3, and filter82400810 so its all-player tail updates only the selected
  bot during that supplemental call. Native normal frame remains unfiltered.
  Scratch lock, selected-player scope, local place, timers and gun-view pointer
  are restored afterward. Existing key/input restore retained as a backstop.
- Death/obituary hooks and UAV routine unchanged. Static checks PASS, MP/SP
  builds PASS, installed both EXEs/PDBs with matching SHA256. Existing unrelated
  kernel printf format warning remains. Startup smoke test PASS:173 visible at multiplayer menu, left open.
  Match regression test pending: bot guns,
  multiple kills, crash stability and host prone/stand. UAV remains unresolved.

## 0.5.172: Stop replaying the host player update for every bot

-171 user result: bot kills now appear correctly in the kill feed. Random
  prone/stand changes still occur, and UAV still shows no bot markers. Death
  animation was not separately confirmed by this latest report.
-167's extra call to82403840 for each bot also runs that routine's tail over
  all24 session players, including the host, under the bot's scratch local
  context.171 restored key/input bytes afterward, but that cannot undo player
  actions already processed by the repeated session ticks.
-Remove the supplemental per-bot session ticks; keep the real native session
  frame once.171's now-confirmed lethal-hit/obituary path remains unchanged.
  This is the only gameplay change. UAV work follows separately.
-Source checks PASS: no supplemental82403840 call, death hooks and UAV routine
  unchanged. MP/SP builds PASS; both EXEs/PDBs installed, executable hashes verified, VERSION172. Build/source-check logs archived in docs/build-logs. Live prone/kill-feed regression pending.

## 0.5.171: The bots' deaths played on the host (death animation, kill feed); prone guard

- **Justin:** still no death animation or kill feed for bots; lying prone
  he sometimes stood up as if someone else had the controls.
- **Found (0.5.170's trace):** at Justin's death the game ran its whole
  "player dies" routine: the gun dropped, the death animation, the scripts'
  death steps and the kill message. At a bot's death none of it ran. The
  game hands a lethal hit to the online session, which plays the death on
  the dead player's own console; a bot has no console of his own, so his
  death was never played anywhere.
- **Fix:** a bot's lethal hit now takes the game's own offline path, which
  plays the death right there on the host, then posts the kill.
- **Prone:** the bots' per-frame session update now can't touch the host's
  keys or input state (they're put back after it, and the log says if a
  bot's update had changed them).
- **Test:** War with about 10 bots: kill several (death animation? your
  kill in the feed?); lie prone near bots for a while.

## 0.5.170: Bots melee, throw smoke, reload and jump; tracing the bots' deaths

- **Justin:** still no death animation for any bot; he wants the bots to
  melee up close and to use what their class carries (smoke grenades,
  bazookas, artillery).
- **The buttons:** found in the game's own table of its keys, so these are
  the real ones and not guesses: melee, smoke grenade, reload, the class
  grenade, lean, use, and the jump (the "up" move).
- **Melee:** a bot with an enemy in sight within about 70 units swings.
- **Smoke:** a bot at half health or less with an enemy in sight throws his
  smoke grenade, once every 30 seconds or so.
- **Reload:** a bot whose gun's clip is empty, with rounds to spare,
  presses reload.
- **Jump:** a bot that wants to go forward but hasn't moved in a second
  (stuck on something low) jumps; running about with no one in sight,
  bots now and then hop (rushers and flankers more often).
- **Not yet:** bazookas and the scout's artillery need the bot to pick
  that weapon or the binoculars; next.
- **Death animation:** 0.5.169's log showed the bots' records die exactly as
  the host's does (state 4 and the dead movement), so the missing animation
  is in the steps after. The log now names the game's death steps as they
  run (the server's, the scripts' "Player_Dying" and "DeathState", landing
  on the ground) and whose death each is, for the host's and the bots'.
- **Test:** War with about 10 bots: get close (do they melee?), hurt them
  (smoke?), watch them reload and jump; kill a few bots AND get killed by
  one, then send the log.

## 0.5.169: Fix: bots respawning every frame; three menu pictures upscaled

- **Justin:** with 24 bots none came in right: all of them flashing in and
  out as if falling, and they couldn't be killed.
- **Cause:** 0.5.168's death-state change was put in the middle of the
  "dead long enough? respawn" check, which then ran for every LIVING bot
  every frame. That check is back as it was, and the death state is set
  on its own, so its own test is still in.
- **UAV:** the log now says, every 2 s while a UAV is on, what the compass
  checks for the first six bots: his side against the host's, and when he
  last "fired" against the level's time.
- **Menus:** textures\used.txt showed every big menu picture before the
  match was already upscaled except three sepia photos (tanks, troops) seen
  just before the match. They're upscaled x4 now (in textures\replace,
  not GitHub).
- **Test:** War with plenty of bots: do they come in and play normally; kill
  some (death animation? kill feed?); watch the compass as they come in;
  call a UAV.

## 0.5.168: Bot deaths marked as the host's are; bots enemy on the compass from the start; roles and classes; a list of the menus' pictures

- **Justin:** he wants a mix: some bots rushing, some roaming the edges of
  the map, some playing the objective, some camping; and each with a
  different class and weapons.
- **Roles:** every bot is now one of four: objective (goes for the flag in
  play), rusher (sprints at you, runs the map when no one's about), flanker
  (long runs around the map, changing direction now and then) or camper
  (holds a spot crouched, moving on every minute or so). In a fight they
  all fight. The log says which each bot is.
- **Classes:** each bot gets one of the game's seven classes at random, so
  their guns differ. (Custom classes and the campaign guns for bots come
  later.)
- **Menu pictures:** every picture the game shows is now listed in
  textures\used.txt with whether an upscaled copy was used, to find the
  Create Lobby and System Link pictures still missing one.
- **Death animations:** the host's player record goes from state 3 (in
  play) to 4 a moment after he dies; the bots' stayed 3, and the game draws
  a player in state 3 as alive, so a shot bot froze standing and vanished
  at respawn. A dead bot is now put in state 4 too, and back to 3 when he
  respawns. The log says "dead: state 3 -> 4" for the first few.
- **Compass:** a new bot's side was 0 for a moment before it was picked,
  which the compass showed as a friendly green arrow. His side is now set
  when he's made.
- **Test:** War with plenty of bots: kill a few (death animation? kill feed?), watch the compass as they come in; a mix of behaviour and of guns; then
  walk through Create Lobby and the System Link screens.

## 0.5.167: The bots' deaths through the game's own player update; the UAV shows bots

- **Deaths and kill feed:** a player's death is written down and sent to
  every console only by the game's once-a-frame update of the players at
  this console; the bots were never part of it. Now each bot gets that same
  update every frame, so his deaths are recorded like the host's: the death
  animation and the kill feed should follow.
- **UAV:** Justin: the UAV never showed the bots. The compass only marks
  enemies who fired in the last 15 seconds, and a bot's shots never counted
  as firing for it. While a UAV is on, every bot now counts as having just
  fired: all of them on the compass, the whole time it runs.
- **Test:** kill some bots (death animation, kill feed); call in a UAV and
  look at the compass; still no trouble for you.

## 0.5.166: Smarter bots: they aim at you prone, grenade your cover, hide to reload

- **Justin:** make the bots better: shoot him when he lies down, take
  cover more, throw grenades.
- **Prone and crouched:** the bots aimed at a standing player's head
  height whenever someone's eyes were low, so they shot over anyone lying
  down. They now aim at where your head really is, and look from their own
  eyes when they crouch.
- **Grenades:** besides at someone in the open, a bot now throws one at
  someone he just saw duck behind cover nearby.
- **Cover:** a bot goes for cover when his gun runs dry with someone about,
  and sooner when hurt.
- **Compass:** right after a bot respawned he showed for a moment as a
  friendly green arrow: the game's spawn gave him the host's side for a
  frame. His side is now set straight after the spawn.
- **Test:** go prone in front of a bot; hide behind a wall after he has
  seen you; watch whether they back off to reload.

## 0.5.165: Learning the bots' deaths; not every bot camps the flag

- **Justin, on 0.5.164 (20 bots):** the kill feed shows him dying to the
  bots, but not his kills on them; still no death animations; and most bots
  just stood still.
- **Standing still:** in War every bot went to the one flag in play and
  held it, crouched. Now a third of them hunt instead and the rest go for
  the flag.
- **Deaths (learning):** when anyone dies, the game's record of that player
  in the match is now written to the log. Comparing a bot's death with the
  host's shows what the bots' deaths are missing.
- **Test:** War with bots: kill a few bots, and get killed a couple of
  times too (both are needed for the comparison).

## 0.5.164: The bots' deaths and kills, sent under the host's console id

- **Justin, on 0.5.163:** still no death animations (they freeze and
  vanish) and the kill feed doesn't show his kills on them.
- **Why (likely):** the game passes a death and a kill around as a message
  naming the console the player belongs to; each bot's record carried a
  made-up console id nobody answers to, so those messages went nowhere.
  They worked when the bots used the host's own record, id and all.
- **Now:** each bot's record carries the host's console id.
- **Test:** kill some bots: death animation, kill feed; many bots still fine.

## 0.5.163: The bots' deaths, from their own "console" marked as this one

- **Justin, on 0.5.162:** bot_add 20 works: lots of bots all over the map.
  But still no death animations: they freeze and vanish.
- **Why:** the deaths (and kill feed) worked only while a bot shared the
  host's own "console" record; several parts of the game look straight at
  that record's "this console" mark, which each bot's own copy had off.
- **Now:** each bot keeps his own record, but marked as this console's.
- **Test:** kill some bots: death animation, kill feed; bot_add 20 still
  fine; nothing odd for you.

## 0.5.162: The bots' deaths and kill feed (local place answers)

- **Justin, on 0.5.161:** the bots hold their guns again. Still no death
  animation, and nothing in the kill feed.
- **Why:** those worked back when a bot held one of the console's local
  player places (0.5.137-0.5.144). The game asks "is this one of ours" by
  those places, and the bots no longer hold one.
- **Now:** for a bot that question answers yes, without giving him a place
  (no split-screen limit of 3 comes back).
- **Also:** bot_add 20 said 20, but only 14 were really added, and only
  the first 6 were drawn (the rest never got the session player the game
  draws them through). Now all 20 get added and each one gets drawn.
- **Test:** kill the bots: death animation and kill feed; three bots, no
  trouble for you; then bot_add 20.

## 0.5.161: Bots counted as this console's players again (guns, deaths, kill feed)

- **Justin, on 0.5.160:** three bots in, all stable, no bugs for him. But
  the bots have no gun in their hands, no death animation, and their deaths
  don't show in the kill feed.
- **Why:** all three are things the game expects an online player's own
  console to send; a bot has none. They were made online-style players
  (0.5.145) because counting them as this console's caused the zoom, the
  host's guns going full auto and freezes, but that trouble was really
  their controls being written over the host's (fixed in 0.5.159).
- **Now:** the bots count as this console's players for all of that again,
  while keeping everything of their own that fixed the bugs.
- **Test:** three bots: guns in their hands, death animations, kill feed;
  and still no trouble for you (gun, zoom, prone, freezes).

## 0.5.160: The bots' death animation back

- **Justin, on 0.5.159:** two bots, no bugs: his gun, view and controls are
  fine, and the bots move well and kill him. Only their death animation is
  missing (since 0.5.145).
- **Why:** the game plays a death for an online player by telling that
  player's console to; a bot has no console of his own, so nobody played it.
- **Now:** for their deaths the bots count as this console's players, so
  the host plays the animation, as before 0.5.145.
- **Test:** kill each bot: he falls; also try a third bot.

## 0.5.159: Found it: the bots' controls were written over the host's

- **Justin, on 0.5.158:** it froze or crashed again after bot 2, and his gun
  glitched for a second.
- **Found (0.5.158's log named the step):** each frame the bots' "controller
  input" (where to move, look, fire) was written into a list the game keeps
  for the players at this console, which only has room for 4. Bot 1 (player
  4) landed on the host's own input state: that is what made him stand up
  from prone, feel like someone else had control, and glitch his gun. Bot 2
  (player 5) landed on the host's gun view: gun gone, then the crash.
- **Now:** each bot's input goes into a space of his own, and the server
  reads it from there. Nothing of theirs touches the host's any more, for
  any number of bots.
- **Test:** War, bot 1, bot 2, then more: gun steady, prone stays prone, no
  freeze or crash; the bots still move and shoot.

## 0.5.158: Pinning down the step that takes the host's gun

- **Justin, on 0.5.157:** still the same: soon after bot 2 his gun was gone.
- **What the log showed:** the War effects weren't it (none were skipped),
  and the processor watch didn't catch the write, though the host's gun
  view pointers were wiped right as bot 2 was added.
- **Now (to find it):** those pointers are checked after every single step
  of adding, setting up, spawning and running the bots, and of the game's
  frame around them; the log names the first step after which they changed.
- **Test:** War, bot 1, then bot 2.

## 0.5.157: Found what breaks the host with bots in War

- **Justin, on 0.5.156:** it still crashed soon after bot 2; dig deep.
- **Found:** War's script gives every player little screen effects (a view
  kick and such) through two of the game's own functions, by player
  number. Those functions are meant for the players at this console
  (numbers 0 to 3): they switch the game's "which local player" to that
  number. For a bot (4, 5...) that number was past the end of the game's
  lists for local players, so the next code that used it wrote over the
  host's gun view pointers: the gun reset to nothing, then the crash. The
  log of 0.5.156 shows those pointers wiped right when bot 2 joined.
- **Now:** those screen effects are skipped for anyone not at this console,
  and the game's "which local player" isn't touched. The processor watch is
  back on the gun view pointers in case anything else still writes them.
- **Test:** War, add bot 1, bot 2, more: gun stays, no crash.

## 0.5.156: The host's gun view object itself guarded

- **Justin, on 0.5.155:** the game crashed as soon as he added the second bot.
- **What the log showed:** the pointer was never emptied; it always points
  at the host's own gun view. That gun view itself is what gets wrecked.
- **Now:** it is kept exactly as it was around every bot step (the log lists
  what a step changed), and the processor watches the part of it the crash
  reads, naming whatever writes it.
- **Test:** War, bot 1, then bot 2.

## 0.5.155: Catching what empties the host's gun view

- **Justin, on 0.5.154:** after the second bot his guns were invisible and
  he couldn't switch them back.
- **What the log showed:** the spot 0.5.154 watched was never written; the
  host's gun view isn't scribbled on, it's taken away: the game's pointer to
  it is emptied when a bot is set up, and the game then reads an empty one.
- **Now (to find it):** the processor watches that pointer from the first
  bot on and names whatever code empties it.
- **Test:** War, add bot 1, then bot 2, until the gun goes.

## 0.5.154: Catching what wrecks the host's gun with a second bot

- **Justin, on 0.5.153:** one bot was fine; adding the second, his gun
  glitched and the game crashed soon after.
- **What the log showed:** the bots' own gun view memory works, but the
  moment the second bot comes in, something still writes into the host's
  gun view through another way, and the game later crashes on it.
- **Now (to find it):** when the second bot is added, the processor itself
  is told to report every write to that spot of the host's gun view, with
  the game code that made it. The put-back guard also lists exactly what
  each bot step changed.
- **Test:** War, add two bots, play until it breaks.

## 0.5.153: Each bot has his own gun view memory

- **Justin:** with bots meant to be a game setting, they should have their
  own gun view memory rather than going through the host's.
- **Now:** every bot gets a block of memory of his own for the game's
  first-person gun view, used whenever anything runs for him (his set-up,
  thinking, spawning, the game's own runs of his player). Nothing he does
  lands in the host's any more, however many bots there are. The put-back
  guard from 0.5.151 and 0.5.152 stays as a safety net.
- **Test:** War with two or more bots: your gun stays, you can move, no
  freeze or crash; the bots still move, shoot and go for the flag.

## 0.5.152: The game's own bot spawns and thinking guarded too

- **Justin, on 0.5.151:** his gun was almost right, then glitched, then he
  froze, as if something else had a little control. (The bots now go for
  War's flag in play: the log shows them following it as it moves.)
- **Why (from the log):** besides our own steps, the game itself spawns and
  runs the bots' players, and those runs too wrote into the host's gun view
  memory: his gun was taken out of his hands, then the game crashed in that
  same gun code.
- **Now:** whatever the game runs for a bot's player keeps the host's gun
  view memory and field of view as they were.
- **Test:** two bots: your gun stays, you can move, no crash.

## 0.5.151: Bots play War's flag; the host's gun guarded

- **Justin, on 0.5.150:** after the two bots came in he had no gun again,
  and a vehicle he got in wouldn't drive or let him out. He took War flags.
- **War:** his captures in the log showed how War keeps its flags: one flag
  at a time is in play (the middle one first, then the next along after
  each capture). The bots now head for the flag in play and stay on it,
  unless an enemy is close; they fight for it like players do.
- **The gun:** the game keeps the host's gun view in memory that the bots'
  own runs of the same code also wrote into. Now that memory is put back
  exactly as it was after every bot's thinking and set-up.
- **Test:** War with two bots: they go for the flag in play; your gun shows;
  vehicles work.

## 0.5.150: No more gun flicker from the bots

- **Justin, on 0.5.148:** with a second bot his guns are fine now, but they
  still glitch in and out a little while he just holds them.
- **Why:** the game's first-person gun view was also run for the bots, who
  have none; it shared its saved field of view with the host's and kept
  knocking it to 0 for a frame.
- **Now:** that view is skipped for bots, and the shared saved value is
  healed when it breaks. Also has 0.5.149's War flag logging.
- **Test:** a War match with two bots: guns steady; take a flag or two.

## 0.5.149: Learning War's flags (for the bots)

- **Justin:** the bots should play War's flags.
- **Found:** War keeps its flags as the map's "war_flag" pieces in a list,
  each with its own set of the script's values (which flag it is, who holds
  it, how far it's taken), named by number, not by word.
- **Now (learning only, nothing changes in play):** in a War match, every
  2 seconds each flag's values are written to the log when they change
  ("war: flag ..."). One match where flags get taken shows which value is the
  holder; then the bots get sent to the right flag.
- **Test:** play a War match (bots or not) and take a flag or two.

## 0.5.148: The host's gun view healed (no gun with two bots)

- **Justin, on 0.5.147:** with two bots his view shows no gun, though he can
  switch weapons.
- **Why (from the log):** his own weapon view keeps a saved copy of the field
  of view and puts it back every frame. A bot's code had once left the field
  of view at 0 at the moment it was saved, so his view kept coming back to 0
  and the gun was drawn with no view at all.
- **Now:** when that saved copy is broken, it and the field of view are both
  put right, so it can't keep coming back.
- **Test:** two or more bots: your gun shows.

## 0.5.147: No phantom split-screen player for the bots (freeze, gun gone)

- **Justin, on 0.5.146:** with two bots there was no zoom, but he froze and
  his gun disappeared.
- **Why (from the log):** the game came to run a second "local player" (a
  split-screen player that doesn't exist) on the place the bots use for
  their set-up; its view and weapon code then wrecked the host's view, gun
  and controls each frame.
- **Now:** while the bots are online-style players, the game's three spare
  split-screen places are kept empty, and that phantom player's frame is
  skipped.
- **Test:** add two or more bots: you can move, your gun shows, no zoom.

## 0.5.146: The host's field of view kept while bots think

- **Justin, on 0.5.145:** one bot shows and there is no zoom, but his death
  animation is gone. With a second bot nothing crashed, but his own guns
  stopped drawing properly, he stood up from prone by himself, and he heard
  reload sounds.
- **What the log showed:** every frame, while a bot thought, the game's
  weapon-view code wrote the bot's own saved field of view (0) over the
  host's.
- **Now:** for every bot, however many, the host's field of view is put back
  exactly as it was after the bot's thinking and set-up.
- **Still open:** the bot's death animation (since 0.5.145), the reload
  sounds; the prone and gun drawing may be fixed by this, to be checked.
- **Test:** two or more bots: your guns draw and pull out normally, prone
  stays prone, no zoom.

## 0.5.145: Bots join as online players (no limit of 3)

- **Justin:** he wants every bot he adds to work, not a patch per bot.
- **Why the old way couldn't:** each bot posed as one of the host's split
  screen players, and the game only has 4 of those; past that, memory was
  overwritten (the zoom, the crashes).
- **Now:** every bot is his own online player, as if he had joined from
  another console; the game takes up to 24 of those. Such a player was
  invisible before (0.5.135) because the game draws an online player from
  what his console sends; for a bot the host now draws him from his own
  body, as it does its own player.
- The old way can be put back from the console: `bot_remote 0` and
  `bot_local 1` before adding bots.
- **Test:** add 1 bot, then several: visible, with his gun, kills in the
  feed, no zoom, no freeze or crash when one dies.

## 0.5.144: The host's field of view guarded (the zoom with two bots)

- **Justin, on 0.5.143:** two bots still zoom him in, and switching teams
  and respawning doesn't bring it back.
- **What the log shows:** the zoom starts the moment the second bot is
  created. The game's view starts from a stored field-of-view number, never
  lower than 1, and the host's came out at exactly 1: that stored number is
  being wrecked.
- **Now:** the number is watched around every step of a bot's set-up and
  thinking; if it goes bad it is put back, and the log names the step that
  did it ("bots: the base field of view went ..."), so the real cause can be
  fixed next.
- **Test:** add two bots: no zoom.

## 0.5.143: The bots' thinking uses their local number too (the zoom)

- **Justin, on 0.5.142:** adding the second bot no longer crashes the game,
  but still zooms him in (log: the host's field of view drops to 1).
- **Why:** 0.5.142 fixed the bots' set-up, but each frame, while a bot
  thinks and moves, the port still told the game his player number (4, 5)
  as the local player, past the 4-entry lists. The second bot's number
  landed on the host's view.
- **Now:** the bot's thinking uses his local number (1 to 3) as well; the
  one place that needs his real player number (fetching his commands) gets
  it back.
- **Test:** two bots: no zoom; both still move, aim and shoot.

## 0.5.142: Bots get their own local player number (towards two bots)

- **Justin, on 0.5.141:** killing the bot no longer freezes him (confirmed).
  Adding a second bot zoomed him in and crashed the game.
- **Why:** the game keeps some things for up to 4 local players (split
  screen), numbered 0 to 3. While setting a bot up, the port told the game the
  bot's player number (4, 5, ...) as the local player, so the game wrote past
  the end of those 4-entry lists and over other data. One bot happened to
  survive it; a second did not. Also, the second bot took the first bot's
  local place while the first was dead.
- **Now:** each bot gets his own local number 1 to 3 when added and keeps it;
  his set-up and his local place use it. A dead bot's place is given back
  once he is alive again. Up to 3 bots fit this way.
- **Test:** add a bot, then a second one: no zoom, no crash; kill each.

## 0.5.141: The bot leaves his local place while he is dead (host freeze)

- **Justin, on 0.5.140:** still frozen in place when the bot dies. He did
  see the bot's death animation (confirmed).
- **What 0.5.140 showed:** the flag it watched was never touched by a death
  (wrong lead). But the log shows the real trouble: right at the bot's death
  the game's session update calls through a wrecked object, and other parts
  of the game then trip over bad pointers. Something of this machine's local
  players gets wrecked when a bot who holds a local place dies.
- **Now:** as soon as a bot is dead, he gives up his local place, before the
  session update sees him; he gets it back when he respawns. Log: "bots: slot
  N is dead: leaves his local place".
- **Test:** add a bot, kill him, check you can still move and pause, and that
  his death animation still shows.

## 0.5.140: A bot's death no longer freezes the host

- **Justin, on 0.5.139:** still frozen in place when the bot dies; he can't
  even pause.
- **Why:** the game's handling of a session player's death (`sub_82404DD0`)
  ends by clearing a flag of this machine whenever the dead player belongs to
  this machine. The bot shares the host's machine, so his death cleared the
  host's flag, and the host's controls and pause stopped.
- **Now:** after that handler runs, if the flag was cleared while the host is
  still alive, it is put back. Log: "bots: a session death: local flag ..."
  and "bots: a bot's death cleared the host's local flag: put back".
- **Test:** add a bot, kill him, check you can still move and pause.

## 0.5.139: A bot is spawned out of his local place (the host's freeze)

- **Justin, on 0.5.138:** the bot's animations look right now. But he is
  still frozen in place each time the bot dies, though 0.5.138 put the
  current client back (log: "current client was 4 ...: put back").
- **Why (likely):** the bot's respawn goes the game's way through the
  session; holding a local place (0.5.137), he is spawned as one of this
  machine's local players, and that set-up takes the host's controls. Before
  0.5.137 (no local place) his spawns never froze the host.
- **Now:** for the length of his spawn (`Respawn`), the bot's local place is
  emptied, then given back. Log: "bots: slot N spawned out of his local place".

## 0.5.138: The host's controller kept on the host after a bot dies

- **Justin, on 0.5.137:** adding a bot no longer zooms him in (log: "slot 4
  takes local place 1"). The bot's kills now show in the kill feed, and he
  thinks he saw the bot's death animation. But after he killed the bot he was
  frozen in place: the log shows his commands still arriving, health 100,
  state 3, and his position not changing.
- **Why (likely):** the game's current client (0x82BCBE9C, also the local
  player it is working for) is left on the bot's after his death, so the
  host's controller drives nobody.
- **Now:** before and after every server frame, with bots in local places, a
  current client other than the host's 0 is put back to 0 (logged, first 30
  times: "bots: current client was N ...: put back"). Only the host plays on
  this machine, so 0 is always his.

## 0.5.137: Each bot takes a free local place (not the host's)

- **Justin, on 0.5.135 with `bot_remote 0` and two bots:** the zoom-in came
  back and the game crashed (guest read at 0x37660000 in `sub_8251A848`, the
  animation code, from `sub_82593D50` in the server frame). One bot works.
- **Why (likely):** the session keeps its machine's four local players as four
  bytes at +0x5991 of its object (session-player numbers, 24+ for none;
  `sub_824046D8`, `sub_823D8FB0`, `sub_823D8FF0`). The bots were in none, so
  "which local player is this" found nothing and fell back to the current
  client (the host, 0): his view zoomed, his guns misbehaved, and with two
  bots both landed on the same place.
- **Now:** when a bot is given his session player, he also takes a free local
  place (1 to 3) of his own. The host's place 0 is never touched. Up to three
  bots this way; a fourth logs "no free local place". Console `bot_local 0`
  turns it off for new bots. Log: "bots: slot N takes local place K".
- Watch: no zoom on bot_add, host's guns normal, two bots without a crash,
  and that the screen does NOT split (four local places are split-screen's).

## 0.5.136: Bots share the host's machine object again (visible)

- **Justin, on 0.5.135:** the bot is invisible again with his own "not on
  this machine" object. So the player update that draws him
  (`sub_82400810`) is for this machine's own players; a remote one is drawn
  from his machine's network messages, which a bot doesn't send.
- **Now:** `bot_remote` is off by default again (0.5.134's behaviour: visible,
  reloads, switches). The host's full-auto/endless-ammo moment and the
  missing kill feed and death animation stay open; they need the specific
  local-player paths the bot wrongly takes found and fenced off, not a
  remote identity.

## 0.5.135: Each bot is his own machine's player, not a second local one

- **Justin, on 0.5.134:** the bot now reloads (his reload sounds heard) and
  switches to his pistol when his main gun runs dry (log: "out of rounds for
  19: asks for 15"). But for a while Justin's own guns went full auto with
  endless rounds and his hits on the bot didn't count; then it stopped and the
  bot killed him. The bot's deaths never show in the kill feed, and he still
  has no death animation.
- **Why (likely):** a session player's +4 is a counted holder (+4 its count)
  of a machine's player object (+8). That object's +0x38 byte means "on this
  machine" (`sub_821AE440`, which `sub_824046D8` uses to pick the local or
  the network path) and +0x34 tells machines apart (`sub_821AE448`). The
  bot's entry, copied from the host's, shared both, so the game took him for
  a second local player of the host's own machine.
- **Now:** each bot's session player gets its own copy of the holder and the
  object (0x100 bytes each, its count set high so the game never frees our
  memory), "not on this machine" (+0x38 = 0) with an id of its own (+0x34).
  Console `bot_remote 0` gives new bots the shared one again.
- Watch: the host's guns, the kill feed, the death animation, and whether he
  still shows and moves (a remote player may be treated differently).

## 0.5.134: Bots switch guns when one runs dry

- **Justin, on 0.5.133:** the bot's gun shows and he kills Justin (confirmed).
  He never switches weapons, and on death he still vanishes with no death
  animation.
- **Now:** when the gun he holds has no rounds left (clip + spare), the bot
  asks for his other kit gun (the main one at record +1066, else the pistol
  at +1065) that still has rounds. Layout from the last session's reader
  (work/read_weapon_cycle.py): weapon defs at 0x82BCBF9C, clip at
  +4*(172 + def+1468), spare at +4*(80 + def+1460), owned bits at +1056.
  Log: "bots: slot N is out of rounds for A: asks for B".
- Reloading is left to the game (not traced: no reload button found yet).
  Death animation still open.

## 0.5.133: A bot asks for his last weapon when his hands are empty

- **Justin, on 0.5.132:** still no gun, no death animation. The log: the kit
  given again five times a life, "after the kit: in hand 0, waiting 0" each
  time. The first kit put weapon 19 in his hands; after the session respawn
  the kit gives nothing new (he still owns it) and nothing raises it.
- **Now:** a player asks for the weapon he wants up through his command's
  weapon (+8). A bot with nothing in hand now asks for the last weapon he
  held (19 in the logs) instead of 0. If the gun still doesn't come up, he
  doesn't own it after the respawn and the kit needs its inventory part redone.

## 0.5.132: A bot is given his kit again after his session respawn (his gun)

- **Justin, on 0.5.131:** the bot is named "Bot 9". He shoots and damages,
  but no gun shows in his hands, and on death he freezes until he respawns.
  `bot_setup 0` (the kit menu's callback) zoomed Justin's view again, and the
  new bot still had no weapon: that callback is not the way.
- **Why no gun:** the log shows the class kit (`sub_82206028`) given to each
  bot at his first spawn ("class: kit for handle 00004005 ... class 0",
  "holds weapon 19"), then the session respawn (0.5.130) spawns him again
  after it and he holds weapon 0 from then on.
- **Now:** a living bot (state 3, health > 0) with nothing in hand or waiting
  (+164, +196) is given his kit again by calling `sub_82206028` with his own
  script handle, at most once a second and five times a life. No menu
  callback, nothing of the host's touched. Log: "bots: slot N alive with no
  weapon: kit given again" and "after the kit: in hand X".
- Watch: whether the gun model now shows, and whether the death freeze was
  the empty hands (death animations may be chosen by the weapon held).

## 0.5.131: Bots named "Bot 1" to "Bot 24"

- **Justin, on 0.5.130:** the automatic session player works (log: "slot 4
  given session player 1"), but the bot shows Justin's own name.
- **Now:** the bot's session player gets its own name at +104 (where 0.5.130's
  log shows the host's), "Bot N" with N picked at random from 1 to 24, no two
  bots sharing one. Log: "bots: slot N is named Bot M". If the scoreboard
  still shows the host's name, the name is also kept elsewhere (the client's
  userinfo) and needs the same change there.

## 0.5.130: Bots get session players automatically (the bot is visible)

- **Found (Claude, live test with Justin on 0.5.129):** the empty render record
  at entity +532 was a dead end: it is the local first-person weapon record
  (built by `sub_82468D58` for the local client only). What a host bot lacks
  is a **session player**: 24 entries of 784 bytes at session +48832
  (+0 number <24, +4 a counted object, +8 client slot). Every frame
  `sub_82403840` walks them and runs `sub_82400810` (the player's whole
  visual/animation update) only for filled entries.
- **Proved live:** debug.txt `bot_session 2` (gives each bot a copy of the
  host's entry and respawns him the game's way) in a System Link Merville
  match, then `bot_bring`: Justin SAW the bot for the first time. It fought,
  took damage (health 63), died and respawned. No crash. Its gun is still
  invisible (bot weapon 0, a known separate issue).
- **Now:** each bot is given a session player automatically, half a second
  after `bot_add`'s bots are in, when a session exists (not in a devmap). The
  copied +4 object's count (+4 of it) is raised for the copy, as
  `sub_824046D8` does, so the bot's entry going away can't free the host's.
- Untested in this build: automatic path, map change/restart with bots,
  scoreboard name (the entry is a copy of the host's, so it may show his).

## 0.5.129: Put away the launcher to the previously held weapon

- User confirms128 Y weapon cycling works again; log shows19->15->19 native
  selections and equipped-state transitions. Mouse-wheel shares the edge path,
  but independent live wheel confirmation has not been supplied.
- Launcher previously always returned to class primary. Save the current
  weapon name/id when taking it out and return to it when putting it away.
  Check actual equipped launcher rather than a stale toggle, reset on spawn,
  and fall back to class primary if saved weapon is no longer owned/valid.
- Only launcher return behavior changes. Invisible bot/radar remain unresolved;
  animation and model updates run but do not prove visible scene submission.
- Actual-source return regression PASS: pistol/primary round trips, cycling away from launcher, invalid/unowned saved return. Both MP/SP builds PASS; installed EXEs/PDBs and executable hashes verified. Build/test logs archived in docs/build-logs. Pistol->launcher->pistol still needs live confirmation.

## 0.5.128: Deliver repeated Y/wheel presses to native weapon cycling

-126 trace proved first primary->pistol cycle succeeds and later raw Y edges
  never reach the cycle command.127 user again confirms cannot return primary.
- Count raw Y/wheel edges in shared input, then consume once per host frame
  through native82457A60(1,0), preserving native selection/ammo/animation logic.
  Suppress the parallel native Y report only while the bridge is active to
  prevent a double cycle. No forced weapon state or loadout rewrite.
- Drain/discard menu presses; bridge disabled while paused, F11 open, dead,
  mounted, nonlocal context, or native weapon-use guards fail. Native menu and
  vehicle Y remain available. Campaign input retains its original reporting.
-127 model update now runs, but user STILL confirms invisible bot; visibility
  and radar remain unresolved. No bot behavior change in128.
- Input latch and actual-source bridge guard regression PASS, including repeated cycles, held button, inactive/paused reporting, menu/F11/dead/mounted/nonlocal guards and no deferred presses. Both MP/SP builds PASS; installed after closing127, executable hashes match. Weapon switching still needs live confirmation.

## 0.5.127: Evaluate host-bot animated bodies after the server frame

- Live126 trace found exactly one bot rig update during model composition,
  before its animation component existed, and no animated-model update after
  adding the component. Host bots bypass the network-player visual updater.
- After each server frame, ask the native rig update8251ED08(rig,-1) to evaluate
  registered bots with their own valid model, pose and animation component.
  Reject missing/stale rigs and rigs owned by another entity. Local player
  slots0..3 are excluded; native hidden-state and LOD checks remain intact.
- This changes only bot body evaluation. Weapon cycling is still unresolved:
  126 captured primary->pistol native select success, but later Y input edges
  did not reach native cycle. The continuous-slot-swap suspicion is unproven;
  do not treat it as the established cause. Radar/equipment remain unresolved.
- Actual-source guard regression PASS: local/unregistered/missing/stale rigs excluded; valid bots evaluated once per tick with native mode -1. Both MP/SP builds PASS; installed and EXE hashes verified. Scripted live bot test PASS: native model/animated-body update counts advance together, no crash observed during capture. User live127 check FAILED: bot still invisible after bring; body evaluation alone is insufficient.

## 0.5.126: Trace invisible-bot model updates and blocked weapon cycling

- User confirmed 125 bot still invisible after being brought beside host.
  Native animation allocation/registration passed; this does not establish
  visible rendering. Read-only state shows animation advances, valid model,
  private pose buffer, and no equipped bot weapon. Radar remains unresolved.
- User reproduced Y/Triangle and wheel failing to change weapons; launcher
  out/away restores primary. Input edges reach game; owned MP44/P38 have
  ammunition. Older per-frame slot rearrangement is a suspect, not proven.
- Diagnostic-only wrappers trace bot rig update/animated update and local
  native weapon cycle/selection, including native guard inputs and caller.
  Each wrapper calls the original once with unchanged input context.
  No gameplay fix is claimed and no slot/render behavior is changed.
- Both MP/SP builds PASS (work/build-0.5.126.log). Installed after user closed125; both executable hashes match build outputs. Live trace pending. Existing Kangaroo/settings fixes retained.

## 0.5.125: Complete native animation setup for spawned bots

- Live124 read-only comparison found valid German character geometry and92
  bones, but bot rig+32 animation component was0. Host has its own component;
  both reference valid animation data with the same verified resource type.
- Normal network-player setup calls8243F138(rig,0) after model setup. Add that
  native constructor once after bot spawn finalization if its component is
  absent. It allocates per-entity state and schedules the normal animation task;
  no host animation state or session entry is copied.
- Diagnostic comparison: bot_setup5 skips this new step on subsequent spawns;
  bot_setup1 is the normal default with animation setup enabled.
- Actual-source spawn-guard harness PASS: absent/existing component, comparison switch, and separate bot state. Both MP/SP builds PASS; installed after user closed124. Live native constructor PASS: bot component82700310 and registration flags61 confirmed by read-only inspection. Visual visibility still pending. Radar and weapon0 remain unresolved;
  no radar behavior change in this build. Settings and Kangaroo fixes retained.

## 0.5.124: Correct the live pause-menu sensitivity sliders

-123 live D-pad fix confirmed by user. Sensitivity reset reproduced in logs:
  startup100/100 became35/47, then37/50 after increasing. Audio stayed25/50.
- Read-only, validated process-memory inspection identified the actual controller
  panel820716DC and sliders82070AC4, value+24/min+28/max+32; old maxima37/50.
  These were not the separate menu path patched in120-122.
- Set those two real native sliders to10..200%, step10; native redraw divides
  by the actual maximum, so100% is half the bar and200% fills it. Preserve
  native UI sounds, other controls and default100%. Save after the observed
  native controller apply8250A4D0, independently of audio.
- Actual-source hooks harness passes100->90->100,200->190->200,
  independent axes, bounds, other controls, native effects, save callbacks,
  unchanged audio and initialization from old resource limits before draw.
  Log:work/test-live-slider-0.5.124.log. Both executables built/installed,
  VERSION124 verified; build log:work/build-0.5.124.log (exit0).
- Live user opened pause Controller: read-only memory check confirms both
  actual sliders value100/min10/max200; native preferences100/100/audio25.
  Saved work/live-options-124-controller-open.txt. User lower/raise/apply
  speed test now USER CONFIRMED FIXED (lower/raise returns to same speed).
  Live log confirms90/100 cycles; final chosen90/90 applied and written to INI,
  with volume50% unchanged. Disk saving verified; full relaunch still untested.
  Snapshot:work/CoD3MP-0.5.124-confirmed-sensitivity-fix.log.
- No bot behavior change.123 D-pad fix remains user-confirmed.

## 0.5.123: Block launcher equipment changes while paused

- One functional change: D-pad left no longer equips/puts away the launcher
  while the native pause/Options menu is open. Consume the input edge while
  paused so holding it across menu close cannot trigger a delayed switch.
  Uses the native pause byte already used to block binocular menu input.
- Add bounded read-only native preference-apply caller/value and actual
  gameplay preference-change diagnostics. These identify the unobserved
  settings path; they do not claim to fix the sensitivity/audio reset.
- User confirms122 pause -> Options -> Controller adjustment becomes slower
  and appears to reset audio. Log has no hooked controller-menu adjustments
  or native_options saves. Previous120-122 unit tests did not exercise the
  screen path actually reached by this test; integration remains unresolved.
- Contrary to the missing-bot appearance,122 log confirms slot4 spawned with
  generic_human55B1C9F4, instance68CF56D0, active AI. Visibility/radar unresolved.
  Preserved work/CoD3MP-0.5.122-user-settings-reset.log.
- Actual launcher-handler input-sequence harness passes blocked menu press,
  held input across close, and normal single gameplay press. Log:
  work/test-launcher-pause-0.5.123.log. Both executables built and installed
  after verifying no game process; VERSION123 verified. Build log:
  work/build-0.5.123.log. First attempt found a duplicate existing pad-look
  hook; moved trace into that existing hook and rebuild passed (exit0).
  Startup visibly reached the MP menu. Automated navigation did not reliably
  register, so live pause/slider reproduction remains uncompleted. Startup
  diagnostics showH100/V100/audio25 (100%/50%); the user's200% startup report
  describes perceived speed, not a confirmed persisted native200 value.

## 0.5.122: Native MP controller sensitivity up to level20 (200%)

- Extend both axes of the in-game controller slider from level15/150% to
  level20/200%, still in steps of10%. Default remains10/100%; existing saved
  settings are preserved. The native labels show the selected level and percent.
- Normalize the full bar to200 and allow saving/reloading values through200.
  Raise the free-look yaw ceiling435->580 degrees/sec so the new range can
  affect actual turning. Native ADS scaling and mounted yaw limits are retained.
- User confirms121's menu reaches15(150%). This patch extends that confirmed
  MP screen; campaign range extension remains outside this patch.
- Actual-source native Options harness passes10..200 adjustment bounds,
  independent axes,200->full bar, save callbacks, restore/defaults and yaw cap.
  Actual Settings.cpp disk test passes200 reload and invalid201 fallback.
  Logs: work/test-native-options-0.5.122.log and test-native-settings-0.5.122.log.
- Both executables compiled successfully with the existing configured CMake
  build (exit0). Log: work/build-0.5.122.log. Both installed after verifying
  the game closed; port VERSION.txt is0.5.122.
  Live turning feel and real restart persistence remain unverified.
  No bot, UAV or weapon-switch change in this patch. User confirmed121 bot
  remains invisible after verified teleport beside host, with no radar marker;
  nonzero generic_human instance alone did not fix it.

## 0.5.121: Release mouse capture during window moves/resizes

- One change: suspend clipping/recentering during title-bar/border operations,
  interactive move/resize and window-mode/resolution changes. Require clicks
  inside the client area before gameplay recaptures the mouse; dragging a title
  bar must not be treated as clicking into the game. A client click resumes aim.
- Fit the restored/resized window to its nearest monitor work area when needed,
  keeping the title bar reachable. Preserve valid placements and maximized state.
- Geometry test passes offscreen corner/negative positions, oversized and tiny
  sizes, unchanged valid position, negative-origin monitor and taskbar offsets.
  Actual source mouse capture harness also passes release, blocking warps/
  recapture while suspended, and fresh centre on resume. Tests preserved in
  chat work/test_mouse_capture.cpp and test_window_geometry.cpp.
- Both executables built/installed as0.5.121. MP visibly reached menu with
  title bar reachable. Automated border drag was not completed (bounds retry,
  then user input detected); no live drag-success claim. Captured-game resize
  still requires user test. Build log:work/build-0.5.121-completed.log.
- Includes120 native MP controller1â€“15 and volume/sensitivity persistence.
  Native labels/live feel/relaunch still unverified; bot visibility next priority.
## 0.5.120: Native MP controller levels1â€“15 and remembered Options

- Replace in-match controller screen's old0..50 adjustment clamp with levels
  1..15 (10..150% in steps10), default10/100%. Normalize the native bar to150
  and show the level/percentage beside horizontal and vertical sensitivity.
- Native gameplay consumes68/72*.01, so these change actual stick gain.
  Raise the free-look yaw ceiling290->435degrees/sec: the old ceiling could
  flatten high settings. Only CL_GamepadMove references that constant; mounted
  explicit yaw limits and native ADS scaling remain. WaW/MW2 feel uncalibrated.
- Persist native volume and both axes through CoD3Recomp.ini's native_options
  fields, captured when leaving native controller/audio screens. Restore once
  after older console saves load; subsequent menu changes remain untouched.
  F11 master volume and mouse sensitivity remain separate. Volume defaults50%.
- Scope: extended range, numeric labels and native save callbacks are MP's
  observed controller screen. Campaign receives new constructor defaults;
  campaign slider extension/save callbacks and other frontend paths unverified.
- Tests: actual Settings.cpp passes defaults, disk reload at150/differentaxes,
  independent F11 gain, invalid values. Actual native_options.cpp harness passes
  override of a simulated original50 clamp, independent axes, bounds, bar range,
  non-axis controls, callbacks, reload over old profile, Restore Defaults and
  yaw-cap update. Native label rendering/live feel/relaunch pending.
- Both executables built/installed successfully as0.5.120; completed build log
  preserved at chat work/build-0.5.120-completed.log. Live menu/relaunch still
  needs checking in121 (which includes these changes).
- Window move/resize bug reported: input.cpp keeps old ClipCursor rectangle/
  recenter point during resize. Separate following patch; bot visibility is
  next gameplay priority. No bot/UAV/weapon-switch change in120.
## 0.5.119: Native Options defaults test

- Development build A: hook verified native preferences constructor in SP/MP.
  Controller horizontal/vertical defaults100; audio defaults25 out of50,
  which is50% (native audio applies value*.02). F11 settings unchanged.
- Controller range150 and saved preference migration deferred to120;
  this development build is NOT the completed requested settings update.
  Add bounded controller widget diagnostics to identify its native range API.
- Justin clarified118 restart persistence is NOT tested yet. Full audio slider
  and original controller range in118 are expected:118 only added storage.
- User request:100%=level10,150%=level15, faster controller look like later
  Call of Duty games. Exact matching turn rates require gameplay calibration.
- Development A both executables built/installed as0.5.119; preserved build
  log work/build-0.5.119-A-completed.log. User confirms lower audio; native
  controller bars full, but original range adjustment still capped. Captured
  work/CoD3MP-0.5.119-native-defaults-test.log. Restart persistence untested.
- No bot/UAV/weapon-switch change. Bots are next priority after this patch.
## 0.5.118: Native profile settings storage

- One functional change: replace discarded XamUserWriteProfileSettings writes
  and the all-unset read stub with persistent per-user native profile storage.
  Use the title's verified 40-byte records, including scalar and binary data;
  reconstruct guest payload pointers when reading instead of saving addresses.
- Save to SavesRoot()/profiles/user-N.bin using a temporary file and atomic
  replacement. Bound counts/payloads, reject corrupt files, retain existing
  settings on partial writes, and release locks before completion callbacks.
- Validation: actual profile_store.h codec passes disk save/reload, supported
  types, binary/empty data, every truncated input, corruption/duplicate records,
  payload/count limits, and unchanged output on failed decode. Test source is
  in chat workspace work/test_profile_store.cpp. A second harness compiling
  the actual kernel_xam.cpp profile helpers/imports also passes40-byte guest
  layout, disk reload, size query/short buffer, pointer rebasing, user isolation,
  and failed write preserving prior data. Native menu/relaunch untested.
- Both executables built/installed successfully as0.5.118. MP visibly reached
  its menu; log confirms native15-setting read. Automated menu inputs were
  unreliable; native menu save/relaunch remains untested. Alt+F4 injection did
  not close the window, so the test process was stopped explicitly. This is
  not evidence of a normal-close regression or a successful close test.
- Scope split to respect one change per build: native volume first-run50%,
  controller first-run100%, and native slider150% are NOT implemented here.
  Native sensitivity fields verified at preferences+68/+72 (integer percent);
  preserve ADS sensitivity factor separately. Those defaults/UI changes follow.
- No new bot/UAV/weapon-switch changes. 0.5.117's drawable candidate has no
  live visibility confirmation; continue treating invisible bots as open.
## 0.5.117: Create each bot's native drawable human object

- 0.5.116 live test: class0, health100/state3, ownership allowance logged.
  Bot still invisible beside Justin; entity+536 drawable stays0. Ownership
  allowance was insufficient. UAV also still does not show this bot.
- One functional change: mirror native network player setup at823F818C and
  82400D40: lookup generic_human via824B5688(820653E4), then8257C090(entity,
  entry) creates a separate drawable object and rebuilds that player's rig.
  Run after placement, before the normal spawn finalizer, only if drawable0.
  Verify entry+4 nonzero as native script path824CDFAC does. Log factory
  entry and resulting drawable. No shared instance or copied host model.
- Weapon switching also reported broken with controller Y/Triangle AND mouse
  wheel. Respawn can equip Kar or pistol but switching stays stuck. Add only
  input-edge diagnostics identifying controller vs keyboard/wheel Y presses.
  No speculative weapon repair or UAV change in this build.
- Validation: native setup/factory checked, generic_human text verified with
  CoD3Scan. Both executables built and installed successfully as0.5.117.
  Visible body, camera and equipment need gameplay
  testing. Weapon switching and UAV remain open issues.
## 0.5.116: Allow native model selection for host bots without local sessions

- 0.5.115 live result: valid class0 reaches kit setup, bot alive, but Justin
  still sees no soldier and no UAV compass marker. Weapon remains0. The
  valid-class change alone did not resolve presentation/equipment.
- One change: allow registered, generation-validated bot handles through the
  model selector821FB3E0's local-ownership query824C3490 at return address
  821FB408 only. Native check requires one of four local session entries;
  bots have none. Other ownership/input/camera/network calls stay native.
- This lets the existing side-aware model definition setter824CB9E8 run,
  followed by normal accessory selection. No fake session, cloned model
  pointer, hardcoded soldier asset or blanket ownership override.
- UAV remains a separate issue:825049C8 requires recent ps+132 activity even
  with enemy_radar enabled. Weapon selection may also need server handling.
- Validation: exact query call site, handle generation and model definition
  setter checked against native code. Both executables built and installed
  successfully as0.5.116; model/camera gameplay
  confirmation required. This is a candidate, not a confirmed visibility fix.
## 0.5.115: Give bots a valid default class before spawning

- Live 0.5.114 test confirms native finalizer82211DF8 gives the bot health100,
  state3 and movement type0. Bringing it beside Justin confirms it remains
  invisible. Log: native kit setup receives class -1, no weapon, model pointer0.
- One change: initialize bot class selectors ps+1920/+1922 to0 instead of
  FFFF, the unselected join sentinel. Bots skip the local kit menu, so no one
  replaced that sentinel. Verified native getters820CE3D8/820CE538 and kit
  path8220D1C8 ->82206028. The native spawn finalizer still supplies the kit.
- Keep camera/team isolation and spawn finalization. No host model-pointer
  copying, manual model attachment or local menu callback.
- UAV activation was reported, but bot compass markers are unconfirmed;
  the current implementation enables native enemy_radar. Track separately.
- Validation: native field/kit call chain inspected. Both executables built
  and installed successfully as0.5.115. No gameplay/model confirmation yet; fresh
  match must verify valid class, model/weapon, normal camera and combat.
## 0.5.114: Run the native spawn finalizer for host bots

- Justin confirms 0.5.113 leaves his camera normal. Log confirms native and
  local camera FOV 90, but bot slot 4 remains health 0 / move type 6 / state 0.
- One change: after native bot placement, invoke the missing spawn finalizer
  at callbacks+3312 with the bot's handle and flag 0, matching offline spawn
  82406BB8. Placement 82593608 clears health; the normal game path follows it
  with this callback. Keep the 0.5.113 team/menu bypass and client isolation.
- Native default callback 82211DF8 performs player state/health/loadout setup.
  Log the callback address and resulting state. No blanket host copying or
  manual forced health/state writes.
- Validation: call signature/order checked against native 82406BB8 and
  824050B4. Both executables built/installed successfully; visible multiplayer
  menu startup passed. Automated menu inputs did not advance the menu, so
  living bot, combat and camera still need fresh-match gameplay testing.
## 0.5.113: Keep bot team/kit setup out of the local player's menus

- Justin confirms sniper ADS/L3 steady aim and sounds work, and Kangaroo
  exits work. Bot_add still zooms the host on 0.5.111; context-only fix did
  not solve it. Host FOV cvar90 and ADS/weapon survive the setup calls.
- One camera-focused change: bots set their server team and existing session
  side directly, using the two fields verified in native 823EA4E0, then use
  the existing server respawn path. Do not invoke the local side/kit menu
  callbacks for bots. Keep the default spawn kit. This removes the remaining
  local-menu side effects; gameplay confirmation is still required.
- bot_camera 1 (default) now includes direct team assignment/menu bypass and
  context isolation. 0 restores the legacy menu/context path for comparison.
- Observe actual native FOV calculation 82453810 and local-camera callback
  824674E0 every two seconds: return FOV, host ADS/flags, camera FOV/auto/tween.
  No forced FOV/ADS or broad memory restore. These traces distinguish native
  camera state from a rendering problem if the zoom persists.
- Validation: team fields and camera field accesses checked against generated
  native code. Both executables built and installed successfully. Fresh-match
  bot_add/camera/teams still need gameplay testing.
  User test on 0.5.112: bot_setup 3 then bot_add 1 left the camera normal,
  but the created bot stayed inactive (health 0, move type 6, state 0). This
  narrows suspicion toward team setup without proving the cause.
  Includes 0.5.112 contact-radius trial (18->13 observed); visual height pending.

## 0.5.112: Lower Kangaroo wheel contact height slightly

- Justin confirms right/passenger and rear/gunner dismounts now work well
  on 0.5.111. The vehicle still appears about 4-6 inches above the ground.
- One change: give each Kangaroo a private Horch physics-preset copy before
  native initialization, reducing its contact wheel radius by five game units.
  The original mp_horsch preset remains untouched for other vehicles.
- Field verified with the original physics loader/table: wheel_radius +20,
  preset name +112, final constructed float +200. Copy only 204 verified bytes
  into a 208-byte private allocation. Validate radius and keep it at least 8.
- `kangaroo_drop 0` restores original contact height at next map start;
  default 5, adjustable 0-8. Use map_restart after changing it. No accumulated
  origin offset; model, collision body and passengers follow native physics.
  Handling, tread logic and the confirmed exit placement code are unchanged.
- Validation: both executables built/installed. Visible Merville run confirms
  the private preset initializes with contact radius 18 -> 13. Actual ride
  height and driving still require gameplay confirmation.
  Bot/camera work follows; bot camera status on 0.5.111 requested from Justin.

## 0.5.111: Let scoped L3 reach native hold-breath without a sprint latch

- Justin requested sniper steady aim with L3, and clarified: walking forward
  keeps ADS up; sprint is disabled while scoped; only aim-out leaves the scope.
- Original MP default controls bind L3 to +sprintbreath. The port's
  tap-to-sprint filter cleared L3 whenever ADS was held, preventing native
  hold-breath. Preserve the physical L3 press during ADS, cancel the sprint
  latch, and report ADS forward movement as walking. Release L3 stops breath;
  leaving ADS does not reactivate a previous sprint latch. ADS itself is unchanged.
- Reuse native steadying, breath timing, recovery and sounds rather than
  modifying shared weapon/sway definitions. Applies to multiplayer; campaign
  has different binocular/hold-breath bindings and retains its input mapping.
- `scope_breath 0` restores the old L3 filter; 1 enables the fix (default).
- Validation: compiled input tests pass for scoped standing/walking L3,
  ADS cancellation of a hip sprint, release, aim-out, hold-input mode and
  rollback. Both executable builds passed and installed as 0.5.111. Initial compile
  failure from missing input.h in the console handler was fixed. Live sniper sway and all newly
  changed game behaviors still require gameplay confirmation.

## 0.5.110: Bots seek cover, aim at head height and steer around obstacles

- Justin requested cover, head aim and better directions toward objectives
  and enemy players. Add one optional tactical decision layer, enabled by
  default; `bot_tactics 0` restores the previous brain, 1 enables it.
- Aim near the target's eye/head height, accounting for crouch height. Check
  visibility every 200 ms, require reaction time after visibility returns,
  and require pitch as well as yaw alignment before firing. Blocked enemies
  no longer pull bots away from their Domination objective.
- Full-body collision sweeps and grounded landing checks choose local route
  steps around walls. Reject void and steep/drop paths; stalled bots commit
  to a lateral escape. Apply movement relative to view direction while aiming.
- Injured/low-health bots look for reachable nearby shelter with a blocked
  enemy-to-crouched-body ray, travel there, crouch, and reassess after four
  seconds. Route planning is throttled; this is local collision steering,
  not a map-wide navigation mesh. Existing Domination goals supply objectives.
- Validation: native trace layout reviewed. Compiled planner tests pass for
  wall detour, committed escape, void/steep rejection, reachable shelter and
  refusal to invent cover in open space. Both game builds passed and installed; live bot
  behavior/camera and Kangaroo exits still need playtesting. Sniper L3 steady
  aim requested and queued for its own build.

## 0.5.109: Isolate bot setup from the host's current-player context

- Justin requested the persistent zoom after bot_add be investigated before
  bot improvements. Prior FOV resets and skipping kit selection did not fix it.
- The normal bot ClientThink already selects the bot in engine CurrentClient
  (82BCBE9C) and restores it; creation, side/kit callbacks and respawn did not.
  Scope those setup calls to the bot too, restoring the previous context on
  every return. This removes a host-context leak; camera causality needs
  gameplay confirmation rather than another assumed cg_fov reset.
- Log the stage/slot/restored context and host ADS/weapon before and after.
  `bot_camera 0` restores the old setup context; `bot_camera 1` enables the
  isolation (default). No camera state is continuously forced or overwritten.
- Validation: source paths reviewed; both executables built and installed. Kangaroo normal fix from
  0.5.108 included; bot cover/head aim/routing remains the next separate build.

## 0.5.108: Read the ground normal instead of the ground height

- Justin confirmed 0.5.107 still leaves every passenger/gunner exit in the
  hull or below the vehicle. Log shows the hook and remembered direction
  work, but every candidate fails the grounded-landing check.
- Fix one field: native trace +0 is hit position, +16 is plane normal.
  Normal Z is +24, not +8 (hit height). On Merville the negative hit height
  rejected valid flat ground. Verified native 824A0468 constructs +0 as
  start + travel * fraction and copies the plane separately to +16.
- Add landing trace values to the log. Preserve entry-direction handling,
  collision clearance, native placement fallback, driving and ride height.
- Validation: native producer reviewed; both builds passed and installed. Actual dismount
  gameplay remains to be checked. Bot upgrade requested separately.

## 0.5.107: Finish dismounts through the actual vehicle exit path

- Justin confirmed right-side and rear turret dismounts still end inside
  the hull on 0.5.106. The live log contains unlink caller 8257AD34;
  neither caller intercepted in 0.5.106 occurred. No placement ran.
- Apply grounded placement after the entire native 8257ABF0 dismount,
  after its origin setter, only once the vehicle flag and link clear.
  Remember the entry tag: left returns left, right returns right, rear
  turret entry returns behind the hull. Blocked landing tries an alternate.
- Keep the existing trace clearance and native-origin setter. Console
  kangaroo_exit 0 restores native behavior; 1/2 provide a fallback direction
  if no entry tag was recorded. Ride height and treads are unchanged.
- Validation: native function and live caller reviewed; both executables built
  and installed. Merville startup passed. Background test stopped at the
  user's report of audible audio without a visible window; it did not reach
  the dismount. Right-side and rear seat placement still need live testing.

## 0.5.106: Give Kangaroo dismounts a grounded side landing

- Justin on 0.5.105: driver-side entry and exit no longer dip under the
  map. The exit clips slightly; passenger-side and rear-turret exits put
  him in the middle of the hull. The hull also floats a little too high.
- One change: after the native exit-completion unlink, find a landing
  outside the hull with clearance for the player's full collision box.
  Prefer left, sweep across above the hull and down onto walkable ground;
  try farther points and then right if blocked. Reject solid starts, walls,
  steep landings, missing ground and landing on the Kangaroo itself. Use
  the game's player-origin setter to keep player, entity and network origin
  together. If no candidate is clear, retain the native position.
- The native trace layout was checked against generated code; scratch
  vectors/filter/result and a guest call stack have their own allocation.
  Only the verified exit-completion callers 0x82585120/0x82585374 move the
  player. Other Kangaroo unlinks log their caller/seat for diagnosis.
- Console: `kangaroo_exit 0` restores native exit placement, `1` prefers
  left (default), `2` prefers right. 0.5.104's stable entry rotation stays.
- Validation: real Windows/MP header syntax check and both builds passed. Installed 0.5.106. Scripted Merville startup passed, but teleport/use did not reach the vehicle, so native
  seat exit tests remain pending. Ride height and bullet flicker are separate open
  items; driving, tread movement, wheel posing and ride height are unchanged.

## 0.5.105: Closing the game also ends its console and audio

- Justin reported that closing the game window left its console and music
  running. Reproduced on 0.5.103: CoD3MP remained alive after Alt+F4.
- WM_CLOSE previously stopped only the window thread. The guest and audio
  threads kept running. It now flushes the log and calls ExitProcess(0),
  matching the existing campaign/multiplayer handoff, without the diagnostic
  exit path's "Press Enter" prompt. Applies to both executables.
- Validation: both executables built and installed. Clicked X on campaign
  and multiplayer 0.5.105; each log recorded "window: close requested;
  ending the game process", and process checks confirmed each executable
  exited. Borderless preference was restored after testing.
- 0.5.104's entry-orientation change is included; Kangaroo entry/exit gameplay and bullet flicker remain
  open. No vehicle-driving, tread, exit-placement or renderer change here.

## 0.5.104: Stabilize the Kangaroo's side-entry orientation

- Justin confirms 0.5.103 drives well and its treads work. Entry dips through
  the ground; exit leaves the player inside the hull. Bullet holes still flicker.
- First isolated change: the left/right entry stand-ins are road wheels. The
  player-link matrix follows their rotating orientation. At sub_82571A58,
  retain the entry position but replace only its nine rotation floats with
  the hull's angle-derived orientation. Wheel and tread poses are untouched.
- Console: `kangaroo_entry 0` restores the old link orientation immediately;
  `kangaroo_entry 1` enables the fix (default).
- Validation: initial attempts hit inaccessible cached Ninja/Python paths;
  local caches now use VS Ninja and bundled Python. A duplicate declaration
  compile error was removed. Both executables built and installed; MP startup smoke test passed.
  Entry/exit behavior remains unverified. This build
  does not change exit placement or bullet-hole rendering.

## 0.5.103: The Kangaroo's treads roll going straight

- **Justin, on 0.5.102 (`kangaroo_build 2`):** it drives again. The treads
  move when turning but not going straight.
- **Now:** after the game's tread distance update (`sub_82568600`), the
  distance the Kangaroo moved along its facing that frame is added to both
  treads' distances (record +1308 right, +1312 left), so they roll going
  ahead and back. Untested; if they run too fast or slow, scale `ahead` in
  that hook.
- `kangaroo_build 2` (Horch physics) is the default, as in 0.5.102.

## 0.5.102: The Kangaroo back on the Horch's physics

- **Justin, on 0.5.101:** he can get in, but it still slides back and forth
  on hills and doesn't drive properly; the camera goes under the map for a
  moment before he can drive.
- **Now:** `kangaroo_build 2` is the default: still made a Kangaroo from the
  spawn (its own model, box, six wheels, side doors, tread scroll), but with
  the Horch's physics preset (`mp_horsch`), which drove "really nice" in
  0.5.81 to 0.5.98. The tank preset (`mp_tankdefault`) on a four wheel kind
  is what slid on hills. `kangaroo_build 1` gives the tank preset back.
- **Untested:** the camera dipping under the map when getting in. If it
  stays, compare the cameras (kind +432..+499) with the Horch's.

## 0.5.101: The Kangaroo drives as in 0.5.99 again, with its side doors

- **Justin, on 0.5.100:** with type 3 (tracked physics on a four wheel kind)
  the driving was broken: it rocked back and forth on hills and the treads
  didn't drive properly. With `kangaroo_type 2` (a whole tank kind) he
  couldn't get in at all, not even at the gun.
- **Now:** type 1 is the default again (0.5.99's driving, which worked but is
  slow on hills), keeping 0.5.100's side-door fix and 0.5.99's hull hitbox,
  tank preset and tread scroll. Types 2 and 3 stay in the console for testing.
- **Why 3 broke:** some of the game's tank driving reads the kind's type, not
  only the physics flag, and a tank's spawn fills values (record +0x414/+0x418
  from kind +708/+712, read only for type 2) that a type 1 spawn leaves empty.
  Tracked physics on a type 1 kind is half one, half the other.

## 0.5.100: The Kangaroo's side doors back; it drives as a tracked vehicle; a handoff file

- **Justin, on 0.5.99:** the Kangaroo's hitbox is "nice and fat again" as it
  should be. But he could only get in at the gun on the back (then switch to
  the driver's seat), it drove slowly, and it got stuck climbing out of a
  ravine. Bullet holes still come in slowly and flicker.
- **The doors:** a vehicle's ways in are looked up once, as it is set up. The
  Kangaroo's model has no `tag_enter_left/right`, so 0.5.78 answers those with
  stand-ins, but only for entities known as Kangaroos. 0.5.99 set it up as a
  Kangaroo before it was known as one, so the side ways in found nothing. It is
  now known as a Kangaroo before it is set up.
- **Driving:** the tanks' physics preset was being driven by the four-wheeler
  code (its physics object lacked the tracked flag 0x20, which a type 2 kind
  gets). That flag is now set on the Kangaroo's physics object, so the driving
  (`sub_82423660`) and the posing (`sub_8242B938`) treat it as tracked, like the
  campaign's. Its kind stays a four-wheeler's, so its seats and ways in don't
  change. `kangaroo_type 1` gives the four-wheeler driving again; `2` makes the
  whole kind a tank (untested). Both apply from the next map start.
- **Handoff:** docs/HANDOFF.md says what this project is, how it's built and
  tested, where everything is, what is done and what is open, so another
  assistant can pick up where this one left off.
- **Bullet holes:** still open. 0.5.99's in-frame buffer watch never fired in
  testing (no "written within a frame" line), so the marks aren't being
  rewritten inside a big buffer mid-frame. That fix stays in (harmless); the
  cause is elsewhere. Next suspects are in HANDOFF.md.

## 0.5.99: The Kangaroo made a Kangaroo from the start; bullet holes drawn as they're written

- **Justin, on 0.5.98 with `kangaroo_tread 10`:** no crash. The treads roll,
  nicely when turning, hardly when driving straight. Its collision feels
  smaller than the hull, and bullet holes still flash before they settle.
- **Why the Kangaroo was still a Horch underneath:** a vehicle is set up as it
  spawns (`sub_8257E3A8`): its physics object comes from its kind's
  vehiclePhysics preset (+540: the Horch's `mp_horsch`, the Sherman's
  `mp_tankdefault`) and its body, wheels and size from the model it has then.
  The Kangaroo was made over from the Horch *after* that, so it kept the
  Horch's physics body and car physics.
- **Now:** as the map's vehicles spawn, the Horch that will be the Kangaroo is
  given the Kangaroo's model and kind first. It is set up as a Kangaroo from the
  start, with the tanks' physics preset (the Kangaroo is a Sherman's hull, and
  the campaign's tracked vehicles use `tankdefault`). Its box is its own
  model's (282 x 128 x 124). Log: "kangaroo: the map's mp_horch made a Kangaroo
  as it is set up" and "kangaroo: set up: physics object ..., preset ...".
  `kangaroo_build 2` keeps the Horch's physics; `kangaroo_build 0` goes back to
  0.5.98's way.
- **Treads going ahead:** each tread is moved by the distance driven, scaled by
  the kind's texureScrollScale (+76). The Kangaroo had the Horch's 1.0 instead
  of the tanks' 5.5, so going straight its treads ran at a fifth of a tank's.
  It now has the tanks' scroll. The game's own tread poses (`kangaroo_tread 10`)
  are the default now.
- **Bullet holes:** a vertex buffer over a quarter megabyte was only looked at
  by the first draw of each frame. The game writes its dynamic geometry (the
  marks among it) into one big pool piece by piece through the frame, so later
  draws got what that place held a frame or more before: marks flashing until
  the pool's layout settled. Now such a buffer's pages are watched from the
  frame's first look, and each later draw sends up only the pieces written
  since. This is in each player's own renderer, so everyone on this build sees
  marks as they're made. `COD3_NOLIVEBUFFERS=1` turns it off.

## 0.5.98: The crashes found: 0.5.93's weapon look wrote past the weapon list

- **Reproduced:** Claude started a Merville match on 0.5.97 and the game died
  loading it. The log, right before the garbage reads began: "marks: 91
  weapons looked over, 466 surface impacts given from others".
- **Why:** 0.5.93's bullet-hole helper went over 255 places of the game's
  weapon list, past its end, took whatever lay there for weapons, and wrote
  impact records into it: 466 pointers into the game's memory. On Justin's
  first 0.5.94 run what lay past the list didn't look like weapons, so
  nothing was written and it ran fine; after that it did, and the game's
  memory was wrecked (0.5.95, 0.5.96 and 0.5.97 all died this way, at the
  side menu or the spawn, each time somewhere different). 0.5.96's renderer
  undo and 0.5.97's tread undo were aimed at the wrong things.
- **Fixed:** that look and its filling in are gone. It was never needed:
  every real weapon already has all 22 surfaces' impacts. Only the read-only
  "marks: in hand ..." line stays. 0.5.94's bullet-hole fix (the renderer
  hashing the marks' pool whole) stays.

## 0.5.97: Back to 0.5.94's Kangaroo (the crash at the spawn)

- **Justin, on 0.5.96:** a garbled picture, then the game closed the moment
  he spawned. The log: a null call in the model code's chain (sub_8243E198
  and on), then a read of guest memory that isn't there. So 0.5.95's crash
  wasn't the renderer's buffer looks (0.5.96 undid those and it still came).
  The other thing 0.5.95 changed was giving the Kangaroo's treads and
  sprockets the game's tank poses again.
- **Undone:** the Kangaroo's four tread and sprocket parts are held still
  again by default, exactly as in 0.5.94, which ran without trouble. The
  console's `kangaroo_tread 10` gives the game's poses (with the wheel list
  put right) for trying by hand. Everything now is as 0.5.94 was.

## 0.5.96: 0.5.95's crash undone

- **Justin, on 0.5.95:** the game closed just as he was about to pick a side.
  The log: a read of guest memory at 0x37630000, which isn't there, from the
  renderer. 0.5.95 had every later draw of a frame look over a vertex buffer
  of up to a megabyte again, the whole of the most it had ever been. A buffer
  the game since made smaller, or moved, ran that look off the end of memory.
- **Undone:** buffers over a quarter megabyte are looked at once a frame
  again, as in 0.5.94 (which ran without trouble). 0.5.94's whole-hash of
  them stays, and 0.5.95's Kangaroo treads stay.

## 0.5.95: The Kangaroo's treads run; bullet holes without the flicker

- **Justin, on 0.5.94:** the Kangaroo's treads and wheels sit exactly right
  now (the log: its wheel bones were the Horch's 4 11 9 10, bone 4 being
  tag_tread_left; now its own 7 11 5 9). But they don't move. And bullet
  holes come much sooner, but still flicker a little before they settle.
- **Treads:** the game's own tread and sprocket poses are given again on the
  Kangaroo (the treads' texture runs with how far each side has driven: the
  v_tracked_tread shader reads the tread bone's pose; the sprockets turn).
  They were only ever thrown off by the Horch's wheel at bone 4, now gone.
  `kangaroo_tread 9` holds all four still as 0.5.94 did, if this looks wrong.
- **Bullet holes:** a vertex buffer the game rewrites between the draws of one
  frame is looked at again by each later draw, but only if it was under a
  quarter megabyte. The marks' pool is bigger, so the frame's later draws
  took its first copy while marks were being added. Now up to a megabyte.

## 0.5.94: Bullet holes show at once and stay (campaign and multiplayer)

- **Justin, on 0.5.92 (0.5.93 wasn't built yet):** in multiplayer the bullet
  holes do appear, but slowly, only after a while, and they flicker in and
  out, as in the campaign. So the marks are made; it's how they're drawn.
- **Why:** the renderer keeps a copy of each of the game's vertex buffers and
  only sends one again when it sees it change. A buffer of a quarter to one
  megabyte that had been still for eight looks was then checked by
  sampling sixty four words (or left to the page watch). The game keeps
  every bullet mark's few vertices in one pool of that size, and a new mark
  is a handful of them: the samples missed it, so a mark showed only when a
  sample happened to fall on it ("after a while"), and the draw flicked
  between the old pool and the new.
- **Fixed:** a vertex buffer under a megabyte is always hashed whole (every
  byte, each look) and never left to the page watch. Bigger ones are
  compared whole already. This is shared by the campaign and multiplayer.
- 0.5.93's changes (the Kangaroo's wheel bones, the weapons' impacts) are in
  this build too: it's the first build with them.

## 0.5.93: The Kangaroo's tread was being turned as a wheel; weapons' bullet impacts filled in

From Justin's two dossiers (kangaroo-technical-dossier, bullet-impact-dossier).

- **The spinning left tread, found.** The Kangaroo is the map's Horch made
  over: its model is swapped in and it's posed as a four-wheeler. A
  vehicle's wheel bones are kept as bone numbers at +36 of its physics
  object, found once by name in the order of the game's wheel-name table
  (0x825D5C6C: front left, front right, back left, back right, middle left,
  middle right), and turned and sprung each frame by `sub_8242B938`. They
  were found on the Horch's model. On the Kangaroo's skeleton bone 4 is
  `tag_tread_left`, so the Horch's wheel at 4 turned the whole tread round:
  the spinning tread in every video. Every earlier tread change moved the
  wrong thing.
- **Fixed:** each frame the Kangaroo's wheel list is set from its own
  skeleton by name, so its own road wheels turn and the treads aren't
  touched by it. Log: "kangaroo: wheel bones were ..., now ... (its own by
  name)". The tank tread mover now leaves all four tread and sprocket parts
  alone on the Kangaroo (0.5.90's always-posed left tread is undone).
  `kangaroo_tread 1` still gives the game's own way for comparing.
- **Bullet holes in multiplayer.** Each weapon keeps one impact record for
  each of 23 surfaces (concrete, metal, wood, plaster, brick and so on) at
  +2284, found by "<weapon>_<SURFACE>" when the weapon loads
  (`sub_8249A380`); the record names the impact effect and the mark. A
  weapon with none for a surface never leaves a mark there. Now, once a
  map's weapons are loaded, any weapon missing a surface's record is given
  another weapon's (an original rifle's first), and each weapon's count is
  logged ("marks: weapon ... has N of 22 surfaces' impacts"). Every five
  seconds the log gives the weapon in hand's concrete, metal, wood, plaster
  and brick records, to show whether a missing mark is missing upstream
  (no record) or in the drawing.

## 0.5.92: Hunting the bots' zoom and the Kangaroo's tread

- **Justin, on 0.5.91:** adding bots still zooms his view in; the tread is
  still off. The log shows 0.5.91's view fix never ran: the console's
  bot_add doesn't go through the session path it was put in. The bots are set
  up by two of the game's own menu callbacks instead (the side switch,
  sub_823EA4E0, and the kit menu's, +3328 of 0x82BCBF64), each given the
  bot's handle, and the host's weapon is seen pulled down and raised again
  just then. Those menu callbacks are the host's own menus', and look to
  act on his own view too.
- **Tried first:** the kit callback is left out for bots (they keep the
  kit they're given anyway). The console's `bot_setup N` changes it live:
  1 (the default) kit left out, 2 side switch left out, 3 both, 0 neither.
  The host's view is now logged (and his field of view set again) from a
  bot's first moments, whichever way he's added.
- **The tread:** every five seconds the log gives the Kangaroo's tread,
  sprocket and middle wheel bone matrices ("kangaroo: bone tag_tread_left
  ..."), parked or moving, so the bad tread can be compared with the good one
  number by number.

## 0.5.91: Your view put back after adding bots

- **Justin, on 0.5.90:** adding bots still leaves his own view zoomed in with
  no gun showing, and aiming zooms it *out* while held. So his field of view
  became narrower than his sights' own: the bots' setup resets his view
  settings (each bot's session player is a copy of his).
- **Fixed (first try):** his field of view (the F11 menu's) is set again one
  second and three seconds after the bots are given out.
- **To find the rest:** for twenty seconds after, the log gives his view's
  state each second ("bots: host view Ns after the bots: cg_fov ...,
  cg_fovscale ..., cg_drawgun ..., in hand ..., +172 ..., +220 ...").

## 0.5.90: The Kangaroo's left tread always posed; bots sprint and take a moment to react

- **Justin, on 0.5.89:** the wheels show but don't turn, and one tread still
  spins off as in the video. The log shows the wheeled-vehicle posing never
  ran for it ("posed as a wheeled vehicle" never written): the Kangaroo
  isn't posed through that tank branch at all.
- **What the tests add up to:** the left tread sits right only with the
  game's pose (0.5.85: both posed, left right and right off; 0.5.87's video:
  left posed but only while moving, so parked it lay thrown off; 0.5.88 and
  0.5.89: left never posed, always thrown off). The right tread sits right
  only without one. The game gives that pose only while the vehicle moves
  (`sub_82434918`, the entity's speed at +716 above nought).
- **Fixed:** the left tread is always given the game's pose (a parked
  Kangaroo's speed is made a hair above nought for that one call); the right
  tread and both sprockets are left where the model has them. Log:
  "kangaroo: left tread always posed, right tread and gears kept in place".
- **Bots sprint** (0x4, the button the host's commands carry while running
  flat out) when running to a flag, or toward an enemy over 1200 units off
  while not shooting.
- **Bots take a moment to react:** on seeing a new nearest enemy he waits
  350 to 650 ms before firing, instead of shooting the instant he turns.

## 0.5.89: The Kangaroo's treads stay on its hull, its wheels turn; the bots' real buttons

- **Justin, on 0.5.88:** the left tread is still thrown off exactly as in
  the video, and the wheels show but never turn. So the tread mover wasn't
  the only thing moving it.
- **Why:** each frame `sub_8242B938` poses a vehicle's parts (its physics
  object: the entity at +624, flags at +640). For a tracked vehicle (flag
  0x10, the Sherman's kind the Kangaroo's is copied from) it calls
  `sub_8241E908`, which lays each tread along a line through three of the
  vehicle's wheel points (+800 to +820) and writes the tread's bone outright.
  The Kangaroo's record is built from the Horch's, so those points aren't its
  own, and the tread is laid along the wrong line: off the front and over
  the top. Without 0x10 the same routine poses each wheel instead (turning
  and springs, `sub_8241D168`), as for the jeeps.
- **Fixed:** the Kangaroo is posed as a wheeled vehicle (0x10 cleared just
  for that call): its six road wheels turn, its treads stay where its model
  has them. Log: "kangaroo: posed as a wheeled vehicle ...", with its wheel
  bones. `kangaroo_tread 1` gives the game's own way back, `7` the tank's
  way with only the tread aiming left out.
- **Tested 0.5.88 on Merville:** the Kangaroo's treads and sprockets are no
  longer moved ("kangaroo: treads and gears kept in the model's own place").
  Pressing each key while watching the host's commands in the log gave the
  real buttons: the stance key 0x2000 (a tap crouches, held it tries to go
  prone: "Prone blocked"), the frag grenade 0x80, the smoke grenade 0x2.
  0x4000 is in every command. 0.5.88's guesses (0x200 and 0x4000) were wrong,
  so its bots never crouched and their "grenade" did nothing.
- **Fixed:** a bot taps the stance key for one frame when he wants to go
  down or come back up (held, it would put him prone), and comes back
  standing after a death. His grenade is 0x80, held a third of a second and
  let go.
- **The Kangaroo's hitbox:** its entity's words around the box (+200 to +336)
  match the tanks' in everything but the box and where it is (+224 1, +228
  0x10 in all three), so walking into its hull isn't its box or its contents.
  The tanks' bodies stop players by their models' collision; next is
  whether the Kangaroo's model (from the campaign's archive) has any.

## 0.5.88: Bots crouch, throw grenades and take flags; the Kangaroo's treads stay on

- **Bots crouch.** At range (over 700 units) a bot now and then stops and
  crouches to shoot, about a third of the time, a couple of seconds at a go.
  Holding a flag, he crouches on it. The crouch button is taken as 0x200, as
  in the later games of the same engine family (the host's own commands show
  0x200 among their buttons). Log: "bots: slot N crouches" / "stands".
- **Bots throw grenades.** At an enemy 450 to 1400 units off that he's facing,
  every 18 to 25 seconds (none in his first seconds): the grenade button
  (taken as 0x4000) held for a third of a second, aimed a little up, then let
  go. Log: "bots: slot N throws a grenade at D".
- **Bots play Domination.** On the host, with the flags placed, each bot heads
  for a flag his side doesn't hold (nearest first, the bots leaning toward
  different flags so they spread out). Once every flag is his side's, he
  defends one the other side is taking, or stands at the nearest. He fights
  instead when an enemy comes within 900 units, and on a flag he stays put and
  fights from there. `Domination::BotGoal` in dom.cpp.
- **Each bot a little different:** his slot shifts his strafing, crouching and
  grenade timings, so they don't all move in step.
- **The Kangaroo's treads and sprockets stay on.** Justin's video of 0.5.87:
  with the right tread held, the left tread was thrown off whole (out past
  the front and over the top) and a drive sprocket stuck out of the hull at
  the back. The game's tread mover (`sub_82434918`) moves the tanks' tread
  and gear parts to make them roll, and on the Kangaroo's model it carries
  those parts bodily out of place. Now none of the Kangaroo's four (both
  treads, both sprockets) is moved by it: they stay where the model has
  them. Log: "kangaroo: treads and gears kept in the model's own place".
- **For trying other ways:** the console's `kangaroo_tread N`: 0 the above
  (the default), 1 the game's own for all four, 2 to 5 the right tread moved
  along its bone's -X, +Y, -Y or +Z instead, 6 the left tread's movement
  given to the right.
- **For the hitbox:** the vehicle probe also logs each vehicle's entity
  words +200 to +336 (the Kangaroo's, the Panzer's, the Sherman's) to find
  why players walk into the Kangaroo's hull but not the tanks'. Its box is
  already like theirs (-141 -60 0 to 141 68 124).

## 0.5.87: The campaign starts again; the Kangaroo's right tread kept in place

- **Justin's test of 0.5.86:** the right tread still sticks up and out of
  the hull when the Kangaroo drives. Copying the left side's distance didn't
  help, so that copy is gone again.
- **Why:** while a tracked vehicle drives, `sub_82434918` gives each tread
  and gear tag a moved pose. The Kangaroo's `tag_tread_right` isn't set up
  like the tanks', so the moved pose throws it out of place.
- **Fixed:** on a Kangaroo that one tag keeps the model's own pose, as it
  does when standing still. The left tread, the gears and the wheels move as
  before. Its log line: "kangaroo: right tread kept in the model's own place".

- **The campaign starting again.** Justin found the campaign stuck at its
  opening movies (CoD3.log: drawing stops 8 s in, all threads waiting). Texture
  dumping was on for the menu upscales, and a playing movie makes a new
  640x360 or 1280x720 texture every frame. Writing each one out held up the
  drawing thread until the movie player locked up. Movie frames are never
  dumped now, and a run dumps at most 3000 textures. Dumping is also switched
  off in CoD3Recomp.ini (texture_dump = 0). Tested: with it off, the campaign
  reaches its main menu and a mission starts.
- **The last five menu banners upscaled** (1024x512: 98378C, AD18FF, B9187D,
  D1B1BE, F9EE7F), 4x with Real-ESRGAN like the rest, in textures\replace.

## 0.5.86: The Kangaroo's right tread in line

- **Justin's test of 0.5.85:** the treads now show. The left tread sits
  right, but the right one is out of place.
- **Why:** the game moves each tread bone by how far that side has driven
  (`sub_82434918`, called while the model is drawn: left from the vehicle
  record's +1312, right from +1308, times 0.0045). The Kangaroo uses the
  Horch's record, and a four-wheeler keeps something else at +1308.
- **Fixed:** every frame, the Kangaroo's right-side distance is set to its
  left side's. Its log line: "kangaroo: right tread's distance ... set to the
  left's ...".

- **Killstreaks for the other players.** The host counts everyone's kills, but
  only the host has the killstreak box. Now when another player earns
  artillery or the Typhoon, the host arms it for him at once. He's given the
  spotter's binoculars and they're put in his hands (his other weapons kept).
  He fires them at the spot, as the host does. His UAV still can't show: the
  radar is drawn on each player's own machine.
- **His killstreak box:** while he holds those spotter's binoculars, his own
  machine shows the killstreak box with the strike waiting ("MARK THE SPOT:
  fire the binoculars"). His kill count and picking streaks stay on the
  host's side for now.

## 0.5.85: The Kangaroo's treads and wheels: their shaders found

- **Why the treads and wheels never drew.** The Kangaroo's model draws its
  treads with `v_tracked_tread` and its wheels with `v_tracked_wheel`. Its own
  archive (`sp\mace2\kangaroo.cod`) doesn't have them; in the campaign, Mace 2
  loads them from its `vehicles.cod`. Merville's and Poissons' archives do have
  them (the Sherman and Panzer use them). But an archive only looks for its
  assets in its own list of archives, and the map's archive wasn't in the
  Kangaroo's list. So both shaders were never found, and those parts weren't
  drawn.
- **Fixed:** the map's own archive is now added at the end of those lists. On
  Merville and Poissons, the Kangaroo's archive now loads after the map's (from
  the vehicles' setup) instead of before it. Its log line: "vehicles: mp_merv's
  own archive (8) is in: the Kangaroo's after it...".

- **The spotter's round, written straight in.** Tested 0.5.84:
  - The box now works: d-pad down cycles UAV â†’ Artillery â†’ Typhoon â†’ UAV, and the UAV call works ("UAV ONLINE", 30 seconds).
  - The artillery call brings the spotter's binoculars up and the strike stays ready (no longer spent early). But they still show 0 rounds and won't fire: the script's clip call doesn't take for binoculars.
  - Now, while a strike is armed, one round is written into his clip and his ammunition directly, as the launcher's rockets are.
- **The F11 box at the top left:** that's "Renderer statistics" in F11 â†’
  Graphics. Untick it to keep only "Show FPS". (Setting, no code change.)

## 0.5.84: The strike spent only when it lands; a round in the spotter's binoculars

- **Tested 0.5.83:** the box called the artillery and the dispenser started.
  But the dispenser sets its own first clock as it starts, and the box took
  that for the strike being called. The charge was used up and the rifle came
  back before the binoculars were up. When they did come up (a second call),
  they had 0 rounds and would not fire.
- **Fixed:** a call is now spent only when its barrage actually comes (the
  barrage hook marks it fired). Until then the strike is kept ready, and every
  quarter second the spotter's binoculars (`mp_binoculars`) are given one round
  in the clip and one spare. Before, the round was set once, as the weapon
  switch began, and didn't stick.

## 0.5.83: Strikes fire from the box; the Kangaroo on its own curve

- **Strikes called from the box.** 0.5.82 armed the artillery but the spotter's
  binoculars came out empty and nothing fired. The ready-time variable outlives
  the scout's dispenser thread; the box read a stale value and started no
  dispenser. Now every call from the box starts the dispenser and puts one round
  in the spotter's binoculars (the mark). Artillery and the Typhoon strafe both
  go through this.
- **The Kangaroo's own kind** now also takes the game's own Kangaroo subtype (8)
  and the Kangaroo driving curve (`vehicleCurveFile` = Kangaroo; `Kangaroo.bvce`
  is in `mp\global.cod`). It also loads the campaign Kangaroo vehicle file when
  its archive has one: speed, bounds, seats, tread scroll and cameras.
- **New console command `kangaroo_type 1|2`:** the Kangaroo drives as four
  wheel (1, the default; it works) or as a tank (2: tracked, to test whether the
  treads draw and scroll). Takes effect from the next map start.

## 0.5.82: The killstreak box; the Kangaroo's own kind, speed and box

- 0.5.81's test (Justin): the Kangaroo drives "really nice" and its gun
  works, but it still drives too fast for a tank's hull, parts of it can
  still be walked through, and no treads or wheels are seen. 0.5.81 set its
  box before its model: setting the model sets the box from the model.
- The Kangaroo's box (250 long, 112 wide, 95 high) is now set after its
  model.
- The vehicle file's fields are named by the game's own table of them
  (0x825FE3C8): type +32, subtype +34, numSeats +68, hudIndex +72,
  texureScroll +40 and texureScrollScale +76, maxSpeed +80, accel, rotRate,
  bounds radius, height and length +116 to +124, health +128, the cameras
  +432 to +499, the seats' interaction file +644. The game's subtypes:
  Jeep, Motorcycle, Truck, Wagon, Halftrack, Horch, Sherman and Kangaroo:
  the Kangaroo was meant for multiplayer.
- The Kangaroo's own archive's vehicle files are now read in the map's
  start as Merville's are: the campaign's "Kangaroo" file. The Kangaroo is
  its own kind, mp_kangaroo, a copy of the Horch's file (its seats'
  interaction and animations) given the campaign Kangaroo's speed,
  bounds, health, seats, cameras and treads' scroll (the Sherman's driving
  and bounds and 8 seats when the campaign's file is not there). The real
  Horch keeps its own.
- The log gives, a few seconds in, the Kangaroo's and the tanks' boxes as
  linked, their model objects' first words (to find why the Kangaroo's
  treads and wheels are never drawn) and the tanks' kind files.
- The killstreak box (as in Call of Duty 4 and after), on the host's
  screen: streaks earned are kept, listed at the bottom right with the
  kills in a row, the picked one framed. D-pad down picks the next, d-pad
  right calls it. The UAV goes up at once; the artillery and the Typhoon
  take the spotter's binoculars out (mp_binoculars: the class binoculars,
  mp_binoculars_nospot, cannot mark a spot, which is why a strike earned as
  a rifleman could not be called) and the strike falls where they are
  fired, then back to the main weapon. D-pad up now puts the binoculars
  away as well as taking them out (d-pad down still does when there is no
  streak to pick).

## 0.5.81: The Kangaroo's own box; its seats and whereabouts logged

- 0.5.80's test (Claude, then Justin, System Link War on Merville): the
  Kangaroo (the map's Horch with the Kangaroo's model and 0.5.78's tag
  stand-ins) can be got into at its side, drives "really nice", and its gun
  can be manned and fired. Parts of it could be walked through: its box was
  the Horch's (198 long, 79 wide, 76 high). The driver sits deep in the hull,
  the view nearly black. Still no treads or wheels to be seen.
- Its box is now its hull's: 250 long, 112 wide, 95 high (the entity's mins
  at +240 and maxs at +256, set before the map start links it).
- Each moved vehicle's kind file is logged word by word (with the text its
  words point at): its seats, their tags and animations, to give the
  Kangaroo its six passengers next.
- Where the Kangaroo is goes to the log every five seconds while it moves.
- The Typhoon, as Justin wants it next: D-pad right once earned, a fade to
  black, and the player flies the campaign's Typhoon (v_Typhoon, its rocket
  trails, the P-47 wing guns' strafe hits on each surface, the flyby sounds)
  over the map for the run, steering it and firing freely; his soldier stays
  where he called it and can be killed meanwhile, which ends the run.

## 0.5.80: The Kangaroo on the Horch again, with its ways in; the jeep's move undone

- 0.5.79's test (Claude, System Link War on Merville): a tank's collision is
  a brush of the map's own at the place the map gives it (a tank-sized box
  of the map's data around the Sherman's place) and does not move with the
  tank: on the Sherman the moved Kangaroo could be walked through and not
  got into. Justin: the Kangaroo is to be its own vehicle, not the Sherman.
- The Kangaroo now takes the map's Horch first (a box for collision, which
  moves with it, and a gunner's seat: on the Horch, in 0.5.74, its gun could
  be fired), then a WC51; never a tank. With 0.5.78's stand-ins for the tags
  its model lacks (tag_enter_left/right, tag_steeringwheel, the hatches...)
  the Horch's ways in and its driver's place are the Kangaroo's own.
- Justin's test: the moved jeep could no longer be got into. 0.5.76 had
  carried every position near the old place in its entity and record; that
  is undone, back to 0.5.73's copies only (drivable then).
- Each moved vehicle's entity is logged word by word, to find the box it is
  hit by and make the Kangaroo's the size of its hull next.
- Found: calling a strike works only with the scout's binoculars; a
  rifleman's (mp_binoculars_nospot) do nothing when fired, so a killstreak
  earned in another class cannot be called yet.

## 0.5.79: Where a moved vehicle's collision stays; streak_give for testing

- 0.5.78's test (Claude, System Link War on Merville): the Kangaroo asks
  for its ways in when it is set up (sub_8256CCE8) and now gets its
  stand-ins (tag_enter_left/right its middle wheels, the back ones
  tag_enter_back). Seen in daylight it is textured (olive, the white star,
  the side plates): the "all black" of 0.5.76 was its shadowed side.
- But Use still does nothing, and walking into it goes through it: its
  collision stayed where the Sherman stood (1140 -3899): there something
  unseen blocks the way. The same is likely why the moved jeep clunks.
- This build looks for every copy of a moved vehicle's old place left in
  memory (outside its entity and record, already carried), at the map's
  start and again a few seconds in, and logs each with the words around it:
  where the collision (or the physics body) keeps its place.
- streak_give uav / artillery / typhoon in the console gives the host's
  player that killstreak, to test the Typhoon without seven kills.

## 0.5.78: The Kangaroo's ways in; the Typhoon's strafing run

- 0.5.77's test (Claude, System Link War on Merville): the log listed every
  vehicle's tags. The Kangaroo's model has its tracks' and wheels' tags
  (tag_tread_left/right, tag_gear_left/right, all six wheels), its driver,
  gunner, six passengers, tag_enter_back and its gun's mount; the Sherman's
  turret model is still on it (tag_turret, tag_barrel, tag_flash). What it
  lacks, against the Panzer, the Horch, the BMW and the WC51, were names
  only known by their hashes until now: tag_enter_left, tag_enter_right,
  tag_enter_back_left/right (where a vehicle is got into), tag_hatch_left/
  right, tag_enter_turret and tag_gunhull.
- Every tag lookup on a Kangaroo (sub_82434E60, under all of them) now
  answers a missing one with the nearest it has: the side ways in its
  middle wheels, the back ones tag_enter_back, the hatches the driver's and
  gunner's places, the turret's way in and the hull gun its gun's mount.
  The log says each stand-in used.
- The Typhoon (7 kills in a row): earned as a strike, called like the
  artillery with the binoculars. Its run is five strikes one after another
  along the line from you to the spot you marked, 300 units apart and
  centred on it, 0.9 s between. "TYPHOON INBOUND" on the caller's screen.
  The artillery's strikes earned first are called first.
- The barrage's log line gives its target point (x y z).
- 0.5.77's log also showed that pressing Use next to the Kangaroo asked its
  model for no tag at all, so getting in may stop before any tag. The log
  now lists, for every vehicle on the map (not only the Kangaroo), which
  tags are asked from where, to compare Use on the jeep with Use on the
  Kangaroo. (Its own comment in mp_fixes.cpp.)

## 0.5.77: Test build: what the Kangaroo is missing (logs)

- 0.5.76's test (System Link War on Merville, by Claude on the host): the
  Kangaroo still cannot be got into (Use next to it does nothing), it draws
  all black, hovers a little and shows bare suspension posts, no wheels or
  tracks. The moved jeep: 10 positions and 7 axes carried with it.
- 0.5.76's "getting in" change hooked sub_82570558, which only turns the
  tank's turret, barrel and recoil bones; getting in is elsewhere.
- This build logs, for the Kangaroo only, every tag the game looks up on its
  model (sub_82435668) and whether the model has it, and once a map lists
  every vehicle's tags by name, so the next build can give the Kangaroo the
  ones it lacks.

## 0.5.76: The Kangaroo can be got into; moved vehicles carry all of themselves; a full friend package

- 0.5.75's test: the Kangaroo (the map's Sherman with its model) has a
  tank's collision, no floating turret, but could not be got into or driven,
  and still shows no wheels or tracks. The moved jeep still clunks as it
  drives; the map's own, unmoved, does not.
- Getting in: a tank is got into at its hatches and aims from its turret
  and barrel, found each frame on its model by name (sub_82570558, the
  names hashed once). The Kangaroo's model has no hatches, turret or barrel.
  For the Kangaroo those names are now its own while that runs: the hatches
  its driver's and gunner's places (tag_driver, tag_gunner), the turret and
  barrel its gun's mount and barrel (tag_gunner_turret, tag_gunner_barrel).
- The clunk: moving a vehicle left copies of where it stood behind (its
  wheels' places, its axes). Every position near where it stood, in its
  entity and its vehicle record, is now carried to the new place, turned by
  the change of facing, and every flat axis turned with it; the log says how
  many.
- Friend package: MAKE-FRIEND-PACKAGE.bat now makes a full first-time
  package on the build you are on: the port, the version and patch notes,
  the shaders, the DLC tool, the upscaled textures and your settings made
  theirs (named Friend, System Link pointed at your address, texture dump
  off). START-HERE-FRIEND.txt says what to do.

## 0.5.75: The Kangaroo is the map's Sherman with the Kangaroo's model

- Tested on 0.5.74 (System Link War on Merville): the saved jeep is the
  map's WC51 and drives, but it clunks a lot while driving. The Kangaroo
  (the map's Horch with the Kangaroo's model) fires its gun but could not be
  driven (Y / F changes seat, from the gun to the wheel), it could be walked
  through in places (its collision is the Horch's, much smaller) and had no
  wheels or tracks.
- The Kangaroo is a Sherman's hull, and its model carries tracks, wheels and
  side plates of its own (ng_tread, ng_wheel1, ng_wheel2, ng_sideplate) on
  the Sherman's tags (tag_tread_left/right, tag_gear_left/right). So it now
  takes the map's Sherman first: a tank's size, collision, tracks and
  driving; then a WC51, then a Horch. It no longer takes a motorcycle.
- A devmap with mp_gametype war gives Merville only a Horch and four
  motorcycles; War made from the menus (System Link) gives it the tanks and
  the WC51 too (the map gives its tanks to tdm, war and hq).
- Upscaled textures in place: the button icons, the controller picture, both
  menu fonts, both photos and the first menu background; two backgrounds
  still to come.

## 0.5.74: The Kangaroo borrows a Horch when the WC51 is taken; g_gametype works

- 0.5.73's test: the saved jeep was the map's own WC51 moved, and it drives.
  The Kangaroo could not be manned because Merville has one WC51 and the
  jeep took it, so the Kangaroo was made new (not drivable). Now a saved
  vehicle with none of its kind left borrows the nearest match from the map:
  the Kangaroo takes a WC51, else a Horch (it has a gunner's seat and gun),
  else a motorcycle; a jeep takes a WC51, else a Horch. The one borrowed gets
  the asked for model.
- g_gametype (the PC Call of Duty name) did nothing: this game's setting is
  mp_gametype (dm, tdm, ctf, hq, scf, dom, war, sd). g_gametype is now passed
  on as mp_gametype.
- Upscaled textures (separate from the build, in textures\replace): the
  button icon sheet, the controller picture, both menu fonts, two photos and
  three menu backgrounds, four times the size (Real-ESRGAN).

## 0.5.73: Saved vehicles are the map's own, moved there (drivable)

- 0.5.72's test: vehicles made in the map's start stay put across restarts
  but still cannot be driven, manned or got into. The comparison made at
  the start (both fresh) leaves few differences: the map's own carry what
  the map gives them, their collision brush (+878) among it.
- So a saved vehicle (vehicles.txt) is now one of the map's own vehicles of
  that kind, moved to the saved place in the map's start, before its
  physics begins: every copy of its origin and yaw rewritten (the entity's
  +32, +304, +336, +832, +76, +356; the record's +192, +208, +448, +228,
  +244, +468). It keeps everything that makes it drivable. The Kangaroo is
  one of the map's WC51s with the Kangaroo's model.
- When the map has no vehicle of that kind left, a new one is made as
  before (not drivable yet), and the console says which kinds the map has.
  On Merville in a devmap (deathmatch) those are its Horches and BMWs;
  g_gametype war (then devmap) brings out its tanks and trucks too.

## 0.5.72: Saved vehicles made with the map's own, at its start

- 0.5.71's comparison (a Sherman put down against Merville's Horch): ours
  have no physics filled in (the wheels' places, the body's orientation,
  all nothing in the vehicle record from +1300 on, its physics slot -1
  where the map's has one), which is why they hover with no wheels on the
  ground and cannot be got into. The map's vehicles are made during its
  start, before the game begins.
- So the saved vehicles (vehicles.txt, vehicle_mark) are now made inside
  the map's start too, in its last pass over the vehicles (sub_82564520),
  right beside the map's own and treated the same; the script's setup of
  the vehicles then runs for them as for the map's. To try one: put it
  down with vehicle_mark <kind>, then map_restart (or devmap the map again).
- spawnvehicle still puts one down at once, as before, to see where it
  would go.
- Split screen: picking Split Screen on the multiplayer menu (A) and then
  typing devmap starts the map for four local players. Type devmap from
  the main multiplayer menu without choosing anything first.

## 0.5.71: Vehicles: what ours lack, read from the map's; vehicle names without mp_

- 0.5.70's test: the pause menu guard works (the game's own pause byte);
  the Kangaroo appears (the WC51's vehicle file, the Kangaroo's model) but
  hovers above the ground without its wheels, and neither it, its .50 cal
  nor any vehicle put down can be used; the map's own can be driven.
- Each vehicle put down is now compared, word by word, with one of the
  map's own (the entity's first 1000 bytes and the vehicle record), and
  the differences written to the log: what ours are missing.
- The version number back on track: BUILD-MULTIPLAYER now takes it from
  the newest entry of these notes (this one: 0.5.71), so the window's
  title, the log, VERSION.txt and the notes always agree. It used to count
  up from the last build, which fell behind whenever a build failed. The
  notes are also copied beside the game as PATCH-NOTES.txt at each build.
- spawnvehicle takes a vehicle file's name without its mp_ too
  (motorcycle_harley, wc51).
- The ini (by hand, this once): texture_dump = 1, and the artillery at 5
  kills (killstreak_kills became killstreak_artillery_kills).

## 0.5.70: Killstreak ladder (UAV, longer artillery); texture packs (menus upscaling, step one)

- Killstreaks (host), kills in a row without dying, one of each a life:
  - 3: UAV, every enemy on your compass for 30 seconds (the compass's own
    enemy_radar drawing, its record at 0x82B2CF90, switched on and off).
    "UAV ONLINE" across the screen.
  - 5: artillery barrage, called with the binoculars; a killstreak's
    barrage is started a second time once the first has fallen (8 s on),
    so it lasts twice as long.
  - 7: the Typhoon: announced, the strafing run itself comes later.
  - F11: Killstreaks, the kills for each, the UAV's time.
  - New on-screen notices for these (Overlay::Notice).
  - For now the UAV and the notices show on the host's own screen only.
- Texture packs (texture_pack.cpp): texture_dump = 1 in the ini saves every
  texture the game shows, once, as textures\dump\<key>.png beside
  CoD3MP.exe. A picture saved as textures\replace\<key>.png (or .dds), at
  any size, is then used in the game's place: upscale the menu art and
  fonts, drop them in, and they are drawn sharp. On by default
  (texture_replace = 1); nothing changes until files are there.

## 0.5.69: Pause menu read from the game itself; the Kangaroo on by default; no invisible Harley

- The binoculars and the pause menu, fixed at the source: Start's command
  (togglepaused, sub_82527280) flips a byte per local player at
  0x82C73024, the same one that stops the player's moves while a menu is
  up. The binoculars now read it: while it is set the d-pad is the
  menu's. (0.5.63 to 0.5.68 tried to guess the menu from the buttons.)
- The Kangaroo is on by default on maps with a WC51 to copy (Merville,
  Poissons, or any map with vehicles_everywhere 1): no kangaroo 1 and map
  reload needed. kangaroo 0 turns it off from the next map.
- vehicles_everywhere alone loads Merville's archive now (the Kangaroo no
  longer pulls all 133 MB of it in).
- spawnvehicle harley on a map without the Harley's model (all but
  Poissons; Merville has only its wreck) says so instead of putting down
  an invisible one.

## 0.5.68: Start and the d-pad counted as they are pressed (pause menu guard, fourth try)

- 0.5.66's log: the pause menu was never seen opening. The binoculars look
  at the buttons in the game's frame, twenty times a second, and Escape's
  Start lasts only a few of the pad's reads, so the press fell between two
  frames. The input layer now counts presses of Start, d-pad up and d-pad
  down as they happen, and the binoculars take the counts each frame
  (presses older than a second, from the menus or a load, are dropped).
- 0.5.66 confirmed: vehicles now appear where you look (the state's +208
  is the view's yaw), and the script's vehicle setup runs for each.

## 0.5.67: The Kangaroo APC (first try); Merville's vehicle files read at the right time

- New console command: kangaroo 1, typed before loading a map. The
  Kangaroo's campaign archive (sp\mace2\kangaroo.cod, about 5 MB) is then
  loaded beside the map, and a new vehicle kind "mp_kangaroo" is made as a
  copy of the WC51 truck's vehicle file. spawnvehicle kangaroo (or apc)
  puts one down with the Kangaroo's model (v_Kangaroo). On a map without
  vehicles of its own, Merville's archive comes too (the WC51 is in it).
  It drives as the WC51 does for now; its own handling and seats come next.
- vehicles_everywhere: Merville's vehicle files are now read where the map
  start makes its vehicle records (sub_82561838); 0.5.62 waited for
  sub_8256CE08, which the map start never calls.

## 0.5.66: Spawned vehicles get the script's setup and face your way; pause menu guard redone

- 0.5.64's test: the jeep set up (its own vehicle record) but could not be
  driven or its gun manned, and the Harley was "put down" out of sight.
  - The multiplayer script sets up each map vehicle at the map's start
    (sub_821F2B40 in _mp_tankdrive.bro, run over every script_vehicle):
    damage effects, blowing up when flipped or under the world, and what
    lets players get in. A vehicle put down later never had it; it is now
    run for each one.
  - Where "in front of you" is: the view's yaw is the player state's +208;
    the entity's +356 (0.5.64) stayed 0 in a devmap, so every vehicle went
    east of you. The log says both, to be sure.
- Binoculars in the pause menu, third try: Start (Escape) was looked for
  only after the "in a level" test, which the pause menu fails, so it was
  never marked. Now Start is seen first and marks the menu open; it counts
  as closed on anything only play does (right stick, a trigger, the view
  turning, the mouse taken back). The log says each change.

## 0.5.65: Vehicle placement (vehicle_mark, vehicle_clear, vehicle_list)

- Vehicles put down for good: vehicle_mark <kind> puts one in front of you
  and saves it in vehicles.txt (beside CoD3MP.exe: map, kind, x y z, yaw).
  At the start of every game on that map the host puts all of the map's
  saved vehicles down (a few seconds in, after the map's own). vehicle_list
  says the map's, vehicle_clear forgets them. The file can be edited by
  hand, and shared with a friend who hosts.
- Only the machine running the game (the host) makes vehicles; anyone
  else's spawnvehicle says so.

## 0.5.64: Binoculars not deaf after a pause; vehicle command spellings

- 0.5.63's pause guard read the wrong number for the view (the player
  state's +208, which never turned), so after the first Start, or Enter
  (which is Start too), the binoculars ignored the d-pad for good. It now
  reads the player entity's yaw (+356, the one spawnvehicle uses), and
  Enter typed into the console no longer counts as Start.
- The vehicle command also answers to vehiclespawn, vechiclespawn and
  vehicle (spawnvehicle jeep, vehiclespawn jeep, vehicle jeep).
- 0.5.63 confirmed: each map now has room for 16 more vehicles (Merville:
  21 in a devmap).

## 0.5.63: Room for more vehicles; the pause menu's d-pad leaves the binoculars alone

- spawnvehicle said "maximum (5) reached" on Merville: the map makes only
  as many vehicle records as it asks for (5 in a devmap), and its own
  vehicles take them all. Each map now gets 16 more (sub_82561838 hooked:
  1872 bytes each, 30 KB), for vehicles put down by hand.
- The d-pad in the pause menu took the binoculars out or put them away.
  Start (or Escape) now marks the menu open, and the binoculars ignore the
  d-pad until the view turns again (only play turns it).

## 0.5.62: Vehicles on any map (vehicles_everywhere)

- New console command: vehicles_everywhere 1, typed before loading a map
  (devmap rouen, ...). On a map without vehicles of its own, Merville's
  archive is then loaded beside it (as the campaign weapons archives are),
  its asset look-ups added to every list, and its VEHICLEFILE and
  VEHICLEPHYSICSFILE entries read after the map's own (sub_8256CE08 hooked:
  sub_8254B318 over Merville's archive). spawnvehicle then works there.
  Merville's archive is big (133 MB): watch for memory trouble.
- Fixed: the campaign sound banks' loop read past its one-entry list for
  the second campaign archive.
- spawnvehicle, 0.5.61's test: the models appeared, but every vehicle took
  the same vehicle record, so only a model each and no vehicle to drive.
  The record is only kept when the kind is found among the map's vehicle
  files, so that look-up is now checked first: when it fails the console
  says which kinds the map has. And when all the map's vehicle records
  (20) are in use, the console says the maximum is reached instead of
  silently failing. Each spawn says on the console whether it set up.
- The log lists how many vehicle kinds each map reads at its start.

## 0.5.61: Vehicles get their models; console bot_add back to adding only

- 0.5.60's binoculars: confirmed working (light zoom, x3 on the left
  trigger, d-pad up/down).
- spawnvehicle: the vehicles were made (the log showed each one set up)
  but had no model, so there was nothing to see. The map's own vehicle
  entities carry one; it is now set the script's way (SetModel,
  sub_824CE130) before the vehicle is set up: v_shermanm4a3, v_panzer_iv,
  v_horch1a, v_wc51, v_bmw, v_harley.
- bot_add from the console no longer gives the bots their sessions: in a
  devmap game that crashed the game a little after (a read of unmapped
  memory). The bots' turn comes with the bot work (0.7); until then they
  stand.

## 0.5.60: Binoculars fixed (light zoom, follow what is in hand); bots spawn from the console

- 0.5.59's binoculars went wrong: they kept their own idea of "out" and
  took the wrong weapon kind (7) for the binoculars', so a moment after
  coming out they lost track: aim stayed held on whatever was in your hands
  (the zoomed, gunless view at spawn), and d-pad down would not put them
  away. Now whether they are out is read from the weapon in your hands
  every frame, nothing kept apart, and aim is only ever held while the
  binoculars themselves are in hand.
- The view through them was the game's own, far too close. Now d-pad up
  gives a light zoom (1.5x your field of view), the left trigger 3x (twice
  that), and again back to 1.5x. D-pad down: back to your gun.
- bot_add from the console now also gives the bots their session and
  spawns them the game's way (what debug.txt's bot_session 2 did), so they
  play instead of standing dead. Use it, then kill 3 for the killstreak.

## 0.5.59: Binoculars out/away on the d-pad; artillery killstreak; spawnvehicle

- Binoculars: d-pad up brings them straight to your eyes, d-pad down puts
  them away. On the keyboard: B out, N away (Binoculars out and Binoculars
  away in the F11 keys list). While they are out the left trigger (aim, the
  right mouse button) zooms x2, and again back to normal (the binoculars'
  field of view narrowed to show things twice as big). The right trigger
  does nothing with them unless you have an artillery strike waiting.
- Binoculars for everyone: the host now gives them to every player and bot
  whose kit it gives, not only to his own player.
- Artillery killstreak (host): every 3 kills in a row without dying earns a
  strike, called in with the binoculars like the scout's. Keep the streak
  going for more (6, 9...). Earned strikes wait through a death until used.
  It uses the scout's own ArtilleryDispenser script, started for the player
  (or, for a scout, his own made ready at once). F11: "Artillery killstreak"
  and "Kills for artillery" (killstreak_artillery, killstreak_kills).
  The log says each kill in a row, each strike earned and called.


- New console command: bot_add <n> adds bots on the host until there are n
  (as debug.txt's bot_add does), for testing the killstreak.
- New console command: spawnvehicle <kind> puts a vehicle down about 220
  units in front of you, facing your way. Kinds: sherman (or tank), panzer,
  horch, wc51 (or jeep, truck), bmw, harley (or motorcycle), or a vehicle
  file's own name (mp_shermantank, ...). Host only.
- It makes a "script_vehicle" entity the way the map's own are made (G_Spawn,
  the class name, origin and angles, then G_CallSpawnEntity with
  "vehicletype" in the spawn's key/values). For now it works on maps that
  carry vehicle files: Merville (mp_merv) and Poissons. Next: loading those
  vehicles' files on other maps, then the Kangaroo.
- The log says what happened with each one ("vehicles: ...").

## 0.5.58: Binoculars for every class; the campaign's binoculars no longer pop up mid-run

- Multiplayer: every class now spawns with binoculars besides its own kit
  (only the scout had them: "mp_binoculars_nospot", his special). D-pad up
  takes them out; hold the right bumper to keep them at your eyes (while
  they are out, the right bumper aims instead of throwing a grenade); let
  go of the bumper, or press d-pad up again, and you are back on your main
  weapon.
  Your other slots and the class's own special (mines, ammo, revives) stay
  as they were. Setting: "Binoculars on d-pad up" in the F11 menu
  (binoculars in the ini), on by default.
- Campaign: the binoculars could come up in the middle of a run and stop
  it. The +binoculars command (sub_8250CDB0) is now ignored while running
  (sprint held, including the tap-to-sprint hold, or the stick pushed most
  of the way). Standing or walking, they come up as before. The log says
  which buttons were down each time, to pin the cause down for sure.
- Build: multiplayer-only blocks (#ifdef COD3_MP) in shared files keep
  their own addresses (make_mp_host.py had been checking and translating
  them as if they were the campaign's).

## 0.5.57: The console works in multiplayer (devmap, god, noclip, ...)

- The console (the key left of 1, ` or ~) sent its commands to the
  campaign's command buffer addresses, which are something else in the
  multiplayer program. It now uses multiplayer's own (cmd_text at
  0x82B93444, Cbuf_AddText sub_8247FA38).
- The developer commands are all still in the multiplayer program, with
  their code: devmap <map> (a map with cheats on), god, invinc, noclip, ufo,
  notarget, give, take, kill, where, setviewpos, jumptonode, dropweapon,
  and the render views wireframe, fullbright, fogswitch. "help" lists them.

## 0.5.56: The Mills bomb in the hand (try); melee lunge 5% shorter

- The melee lunge reaches 5% less far: it takes its target from 122 units
  (was 128). Both players need the same build (movement is predicted).

- 0.5.55 works: the Mills bomb explodes with the frag's effects and sounds,
  and its own model is thrown and lies on the ground.
- In the hand: when the frag's first-person model is its world model (the
  grenade itself in the hands), the Mills bomb's world model is put there
  too. The log says what the frag's is (weapons: mills_bomb's copy's view
  model ...).

## 0.5.55: The Mills bomb explodes

- 0.5.54 works on a map: Mace 2 and Fuel Plant load, all six campaign
  weapons are made, the FG42 Scoped shows its scope on the ground.
- The Mills bomb's explosion (and its pin and throw sounds) came as
  events under its own name, which the maps' effect tables do not have:
  no sound, no explosion (the damage was done). A campaign weapon with no
  sounds of its own now has those events as its copy's (the frag's).

## 0.5.54: Mace 2 for St. Lambert; the campaign's scoped FG42; the Mills bomb

- The campaign weapons archives are now Mace 2's (the Polish mission:
  the Bren, the Lee-Enfield and the scoped FG42) and Fuel Plant's (the
  Sten, the FG42), in place of St. Lambert's; one campaign sound bank,
  Mace 2's, has every campaign gun's sounds (STEN, BREN, LEE_ENFIELD, fg42,
  fg42sniper, the GR_ reloads).
- FG42 Scoped: the campaign's own scoped world model (w_fg42_WORLD, the
  scope on the gun others hold and the one on the ground), its scoped icon
  (hud_fg_42_scoped) and its own gunshots (fg42sniper_FIRE). The scope on
  the gun in your own hands is a separate campaign model (w_FG42_scope) the
  campaign attached in its own code: not yet.
- The Mills bomb, the British grenade: a custom class's grenade kind
  "MILLS BOMB (BRITISH)" (the in-game class editor; ini grenade_type 3).
  The frag's handling, the Mills bomb's model when thrown and lying.
- Found while looking: Mace 2's machine gun (w_geLMG) is the MG34, which
  multiplayer has; the MG42 is only mounted and AI models (no first-person
  animations). The G43 is complete in Forest (a third archive, later).
- Both players need this build: the weapon list grew (mills_bomb).

## 0.5.53: FG42 Scoped: visible, and its own name

- 0.5.52's scope view works (the German scope when aiming). Its world
  model (w_fg42_world) is a model file holding only the noscope FG42, so the
  dropped gun drew nothing: it uses the FG42's model now (the campaign has
  no scoped FG42 model to show the scope on).
- Its own name, "FG42 Scoped" (WEAPON_FG42_SCOPED, given by the text
  lookup sub_8254D180), in the pick-up prompt and elsewhere.

## 0.5.52: FG42 (scoped)

- A fifth campaign gun: the FG42 (scoped), beside the FG42 in the class
  menus (F11, the in-game class editor, the class screen) as its scope
  attachment. The same gun (BAR copy, FG42 animations, 20 rounds, its own
  gunshots, reload and icon), with the scoped Kar98's German scope view (its
  overlay, reticle and zoom: the campaign FG42's scope is the same German
  kind) and the campaign's scoped world model (w_fg42_world).
- The plain FG42 is the campaign's iron-sight one (Fuel Plant's, the
  "noscope" model): its sight picture is the campaign's own, only its zoom
  is the BAR's (fg42_ads_fov in the ini changes it).

## 0.5.51: Sights zoom per campaign gun (ini)

- 0.5.50 works: the campaign guns fire with their own campaign gunshots,
  keep their muzzle flashes, and show their own pick-up icons.
- The campaign guns' sights zoom (adsZoomFov) is their copy's (the FG42's
  and Bren's the BAR's, the Sten's the Thompson's, the Lee-Enfield's the
  Kar98's). New ini settings, no build needed to change them:
  fg42_ads_fov, sten_ads_fov, bren_ads_fov, enfield_ads_fov (degrees; a
  bigger number zooms less; 0: the copy's). The log gives each copy's.

## 0.5.49/0.5.50: Pick-up icons done; gunshots by the shooter

- 0.5.48 (shown as 0.5.49): all four guns show their own pick-up icons.
- Gunshots: a shot event under the campaign name never reaches the sound
  code (it fails before, so 0.5.48's lookup as the copy was never asked).
  Now every shot keeps the copy's name again (campaign_shot_events = 0:
  flashes and the copy's sound found), and the sound code knows the
  shooter: each shot event records the shooter's sound handle (his entity's
  +484, which the message carries at +496) with the campaign gun he fired.
  When that shooter's copy event (thompson, bar, kar98) is made, its
  gunshot cues become the campaign gun's (STEN_FIRE_1ST, ...).

## 0.5.48: Campaign gunshots through the map's event table; icon copies

- What a shot sounds like comes from the map archive's "event" table
  (sub_82554BA0 / sub_82490220), by the event's name: it has only the
  multiplayer guns, so a campaign gun's shot found nothing (silent) but its
  last shot's ring-off (the "sniper" sound). A campaign gun's event is now
  looked up as its copy's (sten as thompson), and while it is made the
  copy's gunshot cues are swapped for the campaign gun's own
  (THOMPSON_FIRE_1ST: STEN_FIRE_1ST, from the campaign banks).
- 0.5.47's spare menu images do not exist: all three got the list's
  default (the hand) object. Now each gun without an icon object gets its
  own copy of the Sten's, with its own image. The Sten's showed (0.5.47).

## 0.5.47: Icons through spare menu images; sound lookup logging

- 0.5.46: the guns' own shots played by name found no sound (that name
  path plays the game's sound aliases, not bank cues), and the Sten stayed
  a white box (the menus' image entries are temporary: the image is kept on
  the image object).
- The pick-up icons: each campaign gun's HUD icon object gets its own
  image on its parts (+100, as the object's own image setting does). The
  Sten has an object (hud_sten_smg); the others take menu image objects no
  multiplayer gun uses (M1 Carbine, G43, MG34 (ge_), Thompson full auto).
- The sound engine's cue lookups are logged (cues: lines), to see what a
  shot asks for.

## 0.5.46: Campaign gunshots played; the Sten's icon

- 0.5.45's test: with shot events 3, 7 and 53 under the campaign names the
  guns went silent (no sound is found by those names) and kept their muzzle
  flash (event 1). Now each shot of a campaign gun plays its own campaign
  gunshot by name at the shooter (STEN_FIRE, fg42_FIRE, BREN_FIRE,
  LEE_ENFIELD_FIRE; _1ST for you, _3RD for the others), with event 1, and
  events 3, 7, 53 keep the campaign names so the copy's shot stays silent
  (campaign_shot_events = 14, now the default).
- The pick-up icons are the menus' images, made at the front end before
  the campaign archives load: the Sten's (hud_sten_smg, in the menus' list)
  got the default image, the white box. It is given its own image once the
  campaign archives are in. The others are not in the menus' list (a hand):
  the log lists the list's images to find them a place.

## 0.5.45: Campaign pick-up icons; gunshots test 2

- The pick-up prompt's gun icons are images looked up in one archive only,
  so the campaign guns' (in the campaign weapons archives) came back as the
  default: the hand or the white box. Now an image not found there is looked
  for in the campaign archives too, and the four guns get their own icons
  (campaign_icons, on).
- 0.5.44's test: shot event 1 under the campaign name lost the muzzle flash,
  so event 1 carries it. This test keeps event 1 the copy's and gives the
  other three (3, 7, 53) the campaign name (campaign_shot_events = 14), to
  hear whether the campaign gunshot comes through with the flash kept.

## 0.5.44: Test build for the icons and gunshots (logs)

- The pick-up icons come from the menus' own image list (sub_824EE308,
  searched by name), which only has the multiplayer guns: so the campaign
  icons fell back to a hand or a white box. This build logs that list's
  images (hud images: lines) to make the campaign ones the same way.
- Every asset looked up by name is logged once (assets: lines), to find
  what a shot looks up (its muzzle flash) and how its sound is chosen.
- campaign_shot_events (ini): which shot events (1, 3, 7, 53 as 1, 2, 4,
  8) keep the campaign gun's own name. This test: 1 (event 1 only).

## 0.5.42: Catch your breath

- Running out of stamina: it now comes back to half in a third of a
  second, so sprinting again straight away is a real sprint instead of
  short stumbling bursts (with quick_sprint, on by default).
- Pick-up icons back to the copies' (campaign_icons off): the campaign
  icons are found, but still draw as a white box (the Sten's, DXT5) or a
  hand (DXT1). The log shows them the same shape as the multiplayer ones,
  except their archive has no bank (-1); that is the lead for next time.
- The campaign gunshot sounds did not take: the shots' sound names are not
  in the weapon's fields (the log says so for each gun). Still the copies'.
- Sten reload back to 3.4 s (3.6 was worse).

## 0.5.41: Aim in the air, campaign gunshots, campaign icons, reload tuning

- Aim down the sights while jumping (quickscopes in the air). The game
  only let the sights come up on the ground. ini: ads_in_air (1 on).
  Both players need this build for it to behave the same on both sides.
- The Sten, FG42, Bren and Lee-Enfield fire with their own campaign
  gunshot sounds (STEN_FIRE_1ST, fg42_FIRE_1ST, BREN_FIRE_1ST,
  LEE_ENFIELD_FIRE_1ST and the 3rd-person ones), and keep their muzzle
  flashes. ini: campaign_shot_sounds (1 on).
- The campaign guns' own pick-up icons are back on (campaign_icons, now
  on by default): 0.5.36 drew them before 16-bit textures were supported.
  The log describes each icon, to compare with the multiplayer ones if
  they still draw wrong (set campaign_icons = 0 to go back to the copies').
- Reload times can be tuned in the ini without a build: sten_reload,
  fg42_reload, bren_reload (seconds; 0 = the built-in 3.4, 2.8, 3.8).
  Your ini tries the Sten at 3.6 s.

## 0.5.40: Dying drops the gun in your hands

- Dying dropped the pistol instead of the gun you held (the gun in hand is
  kept in the slot pick-ups replace, and death dropped the other slot).
  Now the slots are put back the right way the moment you die, so the gun
  in your hands (FG42, Sten, Bren, Lee-Enfield or any other) falls.

## 0.5.39: Swap the gun in your hands (third try)

- 0.5.37/0.5.38 never reached the players: their states are in the game's
  own data (0x82...), which the check took for a bad address and skipped.
  Now the gun in your hands is kept in the slot a pick-up replaces.
- Pick-up icons: the copies' (Thompson for the Sten, BAR for the Bren and
  FG42, Kar98 for the Lee-Enfield), as in 0.5.37.

## 0.5.37: Swap the gun in your hands (second try); icons (test build)

- Confirmed (0.5.36): every campaign gun's model now lies on the ground.
- The weapon in your hands is read from where the game keeps it for the
  host's player (the entity's own byte), so picking up swaps the gun
  you're holding. 0.5.36 read another field and never swapped; the log now
  shows what is in hand.
- The pick-up prompt shows the copy's icon (the Thompson's for the Sten,
  the BAR's for the Bren and FG42, the Kar98's for the Lee-Enfield) instead
  of a white box or a hand. campaign_icons = 1 tries the campaign's own.

## 0.5.36: Pick-ups swap the gun in your hands; FG42 and icons on the ground (test build)

- **Picking up swaps the weapon you're holding**, not always the pistol.
  The game swaps whatever is in your second slot; the weapon in your hands
  is now kept in that slot (for every player on the host).
- **The FG42 shows on the ground** (and in other players' hands): its world
  model is w_fg42_WORLD_noscope; the name used before does not exist.
- **The campaign guns' pick-up icons** (the white box): they were found,
  but drawn from a 16-bit texture format the renderer skipped (and said
  nothing about). 16-bit textures (5:6:5, 1:5:5:5, 4:4:4:4, 8:8) are now
  drawn; any other skipped format is named in the log.

## 0.5.35: Dropped campaign guns' models and pick-up icons (test build)

- The pick-up prompt's icon (the white box, or a glove) and the dropped
  gun on the ground both come from names only found in the campaign
  archive (hud_sten_smg, w_fg42_world, ...). The game looks those up in
  the map's and the global archives only. Both are now looked for in the
  campaign archives too.
- Picking up: the game swaps a picked-up main weapon for your second slot's
  weapon (the pistol in a custom class), as it does with its own guns.
- The log lists the campaign guns' items, models and icons as found.

## 0.5.34: Sten timing, dropped gun items, pick-up tracing (test build)

- Sandbags and the darker textures: confirmed fixed (0.5.33).
- Sten reload 3.4 s (was 3.2). From 0.5.33's video: the magazine sounds
  were within 0.25 s, the bolt 0.5 s after the animation's bolt.
- Each campaign gun's dropped item has its own name ("weapon_sten" and so
  on). It was the copy's ("weapon_thompson", "weapon_bar"), shared with the
  copy and, for the Bren and FG42, with each other.
- The log records the weapon slots when they change, and every HUD icon
  looked up: for picking up a Sten putting down the pistol instead of the
  FG42, and the white box in the pick-up prompt.

## 0.5.33: Dark textures with big memory fixed (test build)

- Found with your tests: the dark and black patches (Merville's sandbags)
  came from big_memory, not the campaign guns. The graphics library has 11
  more places that turn a memory address into a graphics address, written a
  different way from the 225 handled in 0.5.19. Those cut addresses in the
  extra memory down to the first 512 MB, so the GPU read the wrong data for
  some surfaces. They now keep the extra memory's addresses too.

## 0.5.32: Dropped campaign guns look right (test build)

- A dropped gun lying on the ground uses its own model slot (+1436, set for
  the BAR and the Kar98, empty for the Thompson). The FG42 and Bren showed
  a BAR there and the Lee-Enfield a Kar98; only the Sten looked right. All
  four now show their own model on the ground.
- The Sten's reload is 3.2 s (was 2.6) and the FG42's 2.8 s (was 2.4).
  From the 0.5.31 video: the Sten's sound has the magazine out at 0.9 s, in
  at 1.5 s and the bolt at 2.6 s, but at 2.6 s the animation's bolt came
  0.7 s early. 3.2 s puts each moment within about 0.3 s.
- The log times each reload's sound events, to fine-tune them further.

## 0.5.31: Reloads in sync with their sounds; campaign gunshots (test build)

- The campaign reload sounds were measured from the sound banks. The game's
  own reloads last as long as their sounds (BAR 4.0 s to 3.9, Thompson 2.3
  to 2.33), so the campaign guns now do too:
  - Sten 2.6 s (GR_STEN 2.53 s; was 3.7)
  - FG42 2.4 s (GR_fg42 2.33 s; was 3.27)
  - Bren 3.8 s (GR_BREN 3.73 s; was 3.53)
  The animations play faster or slower to fit, as in the campaign.
- Gunshots ask for the campaign guns' own sounds too (STEN_FIRE_1ST,
  fg42_FIRE_1ST, BREN_FIRE_1ST, LEE_ENFIELD_FIRE_1ST). Tested: the shots
  lost their muzzle flashes and played the wrong sounds, so
  campaign_gunshots is now off by default (the copy's shots and flashes).

## 0.5.29: Campaign sound banks (test build)

- The St Lambert and Fuel Plant sound banks (stbert.wbk, fuelplnt.wbk) load
  beside every map's own bank. They hold the campaign guns' real sounds
  (GR_STEN, GR_fg42, GR_BREN, GR_enfield_reload, ...).
- The campaign guns' reload sounds now ask for their own names again, to
  play those real sounds. Gunshots still use the copy's for now.
- New setting campaign_sounds (on by default): 0 goes back to the borrowed
  reload sounds of 0.5.26.

## 0.5.27: Sound bank tracing (test build)

- Reload sounds confirmed working for all four campaign guns (0.5.26). The
  Sten's borrowed Thompson reload is shorter than its own animation.
- The log now shows which code opens each sound bank (.wbk), the first step
  to loading the campaign levels' banks for the guns' own sounds.

## 0.5.26: Reload sounds for the campaign guns (test build)

- Sounds asked for by name that only exist in the campaign levels' sound
  banks (GR_STEN, GR_fg42, GR_BREN, GR_enfield_reload, ...) now play the
  matching multiplayer sound of the gun each is a copy of: Thompson's for the
  Sten, the BAR's for the FG42 and Bren, the Kar98's for the Lee-Enfield.
- The log lists every sound name the game asks for once, to check the
  mapping.

## 0.5.25: Weapon event logging (test build)

- Every weapon event is logged, and five more event senders map the campaign
  guns to their copies.

## 0.5.24: Poissons whole with all four campaign guns (test build)

- **The campaign weapons files no longer use archive banks.** They now load
  into the big map memory (big_memory) instead of taking 19 of the 96 banks.
  The map and its zones keep every bank, so Poissons should load whole with
  the Lee-Enfield, Bren, Sten and FG42.
- The log lists the sound events each campaign gun asks for, to find why the
  Sten's and FG42's reloads are silent.

## 0.5.23: The Sten and FG42; big memory on by default

- **big_memory is on by default** (new installs and your friend's too).
- **Sten** (British SMG, 32 rounds, copy of the Thompson's handling) and
  **FG42** (German paratrooper automatic rifle, 20 rounds, the BAR's
  handling), with their own models, arm and gun animations, HUD icons and
  reload times. In Create-a-Class's lists. They come from the Fuel Plant
  weapons file, loaded beside St Lambert's (big_memory only).
- They use the Thompson's and BAR's sounds for now.
- The log counts the archive banks in use, to see how full maps get with
  two campaign files.

## 0.5.22: Poissons' missing half found (works)

- The 0.5.21 log showed it: every archive takes "banks" of 3 MB from a
  pool of 88. Poissons, its zones and the weapons nearly fill it, and the
  campaign guns' file (9 banks) left no room for zone 1 (10 banks). It was
  never about raw memory.
- With `big_memory = 1` the pool is made at its most, 96 banks, from the
  bigger map block, and Poissons gets the campaign guns again.

## 0.5.21: Memory past 512 MB works; Poissons digging

- **big_memory works:** with it on, the game's map block (480 MB) lives
  past the Xbox's 512 MB, and mp_fuel played perfectly. The 512 MB of
  graphics memory went from 492 MB used to 123 MB.
- Poissons' zone 1 still didn't load after mp_fuel even with 110 MB more,
  so that isn't memory at all. The log now lists every archive the game
  opens and every streaming allocation that fails, to find what blocks it.

## 0.5.20: Memory past 512 MB, step 2 (off by default)

- Step 1's count: on two maps, every one of the graphics library's
  address cuts that ran was handed nothing but addresses. So they now
  keep a new **extended window**: processor addresses 0x50000000 to
  0x6F000000 (496 MB above the game's own virtual memory), which the GPU
  sees at 0x20000000 to 0x3F000000. Its 15 conversions back are patched
  to match.
- The port's renderer, its resource caches and its memory-write watch
  know the window too.
- **`big_memory = 1`** in CoD3Recomp.ini puts the game's map block there:
  480 MB instead of 370, and the 370 MB it held in the Xbox's 512 MB left
  free for textures and buffers. Off by default until tested.

## 0.5.19: Memory past 512 MB, step 1 (measuring)

- The build now routes each of the game's ~210 "cut this address to
  512 MB" spots in its graphics library through a counter. Nothing changes
  in play: each spot does exactly what it did. The log's memory report
  lists what every spot was given, to tell which ones only ever see
  addresses and can be taught about memory past 512 MB.

## 0.5.18: The Bren, and smoother switching

- **Switching:** the 0.5.17 video showed the Lee-Enfield still leaving
  0.3 to 0.5 s of empty hands, both putting it away and taking it out: its
  campaign animations hide the rifle early when putting it away and show
  it late when taking it out. Put-away is now 30% of the Kar98's time
  (400 to 120 ms) and take-out 70% (500 to 350 ms). The Bren gets the same.

- The campaign's **Bren** light machine gun joins the Lee-Enfield: a copy
  of the BAR's handling (automatic) with the Bren's own model, arm and gun
  animations, HUD icon and a 30-round magazine, reloaded at its own
  animation's speed. In Create-a-Class's main and second weapon lists.
- It uses the BAR's sounds for now. On Poissons a Bren class gets the BAR.

## 0.5.17: Lee-Enfield switches faster

- Switching away from the Lee-Enfield left empty hands on screen for about
  half a second: its put-away animation hides the rifle early, but it ran
  on the Kar98's longer timer. The timer is now 60% of that.

## 0.5.16: Campaign guns' file loaded once

- 0.5.15's unloading freed the room (Poissons' zone 1 loaded after
  mp_fuel), but the game still looked up the unloaded file afterwards and
  crashed. Taken back out.
- The file is now loaded once and reused by the maps after, instead of
  possibly being loaded again on every map.
- Known: after a map with the campaign guns, Poissons is short of memory
  until the game is restarted. Play Poissons first, or restart before it.
  (Fixed in 0.5.22 with big_memory = 1.)

## 0.5.15: Campaign guns' file let go between maps (crashed, taken out)

- The campaign guns' file (27 MB) stayed in memory after its map ended, so
  the next map started short: Poissons after mp_fuel lost its zone 1 even
  with the guns skipped. It's now unloaded when the next map's weapons load.

## 0.5.14: Campaign guns skip Poissons (fixed)

- 0.5.13 didn't recognise Poissons (it looked for the map's name in the
  wrong place), so the guns' file still loaded there. The map's name now
  comes from the map file the game opens.

## 0.5.13: Campaign guns skip Poissons

- 16 MB more for the map wasn't enough: Poissons' zone 1 still never
  loaded. So on Poissons the campaign weapons file isn't loaded at all,
  and a class with the Lee-Enfield gets the Kar98 there. Every other map
  keeps the campaign guns.
- `extra_map_memory` is back to 0 by default (it didn't help, and it takes
  room the graphics may need).

## 0.5.12: More room for maps with the campaign guns (test)

- The memory report showed what fills the Xbox's 512 MB: the game's own
  370 MB block for maps, and about 112 MB of graphics (textures, model
  buffers) made by the Xbox's graphics library. All of it is read by the
  graphics side, so none of it can move to the spare memory.
- About 18 MB is never handed out, though. With `campaign_guns` on, the
  map block now takes 16 MB more of it (`extra_map_memory` in
  CoD3Recomp.ini, 0 to 18).

## 0.5.11: Memory report

- The log's memory report now names the part of the game that asked for
  each piece of memory, past the system's own helpers, to find what can
  move out of the full graphics memory.

## 0.5.10: Lee-Enfield bolt, second try (works)

- The game looks up a weapon's animations in two lists: one by its name,
  and a second by its name plus "_arms". The Lee-Enfield only had the
  first. It now gets the second as well, taken from the Kar98's and swapped
  to the Lee-Enfield's own moves.

## 0.5.9: Lee-Enfield bolt (test)

- Working the bolt: the arms were doing the Lee-Enfield's moves while the
  rifle itself stayed still, so the hands waved in the air where the rifle
  should have been. The rifle's own moves are now swapped in as well.
- More detail in the log about the gun's animations.

## 0.5.8: Lee-Enfield magazine

- The Lee-Enfield holds **10 rounds**, its own magazine instead of the Kar98's 5.
- Its reload is the campaign's, in three parts: the bolt opens, a stripper
  clip of 5 goes in (twice from empty), the bolt closes. Each part plays at
  its real speed instead of being stretched to the Kar98's reload time.
- The shot sound plays.

## 0.5.7: Lee-Enfield arms

- The Lee-Enfield now plays its own arm animations (aim, fire, bolt, reload,
  raise, drop) instead of the Kar98's, so the left hand stays on the gun when
  you fire aimed.
- The memory report in the log now names who asked for the memory one level
  further up, to find what could move out of the full pool.

## 0.5.0: Campaign weapons groundwork (experimental, off by default)

- The campaign's guns are being brought into multiplayer. Their files sit in
  the single-player levels: St. Lambert has the **Lee-Enfield** and the
  **Bren**, Mace has those two plus the **FG42**.
- The **Lee-Enfield** is set up as a real multiplayer weapon: a copy of the
  Kar98's settings (damage, clip, handling) with the Lee-Enfield's models,
  animations and HUD icon put in. It's in the Create-a-Class main weapon and
  second weapon lists. It fires and reloads.
- The game can now load a campaign weapons file during a match and find the
  models inside it. The F8 model test shows the Lee-Enfield and Bren models
  in the world.
- `campaign_guns` and `campaign_test` in CoD3Recomp.ini, both off by
  default.
- **Not finished:** the Lee-Enfield in your hands is still invisible, and its
  name reads "null". Loading the whole St. Lambert weapons file also takes
  about 29 MB, which leaves big maps like Poissons short of memory: part of
  the map doesn't load. The plan is a small file of our own with just the
  campaign guns. Until then, keep `campaign_guns = 0` for normal play.
- **New: version numbers.** Every build gets its own number (see above).

## 0.4.10: Loadout extras

- **Second weapon:** a custom class's sidearm slot can hold any main weapon,
  for a sniper and a bazooka, or two rifles. The default is still your side's
  pistol.
- **Rocket launcher as equipment:** everyone spawns with their side's launcher
  (Bazooka for the Americans, Panzerschreck for the Germans) on top of their
  class's weapons. **D-pad left** takes it out, and again puts it away. It
  comes with 1 rocket loaded and 4 spare. Anti-Armor doesn't get a second
  one. You can turn it off in F11.
- The launcher no longer pushes out your main weapon or your pistol.

## 0.4.9: Domination flags fixed

- Captured flags now hang from the pole in the holder's colours, American or
  German. They show for both players, and the old flag disappears when a
  point changes hands.
- **Joiners now get the host's Domination updates:** flag owners, capture
  progress and score. The message had been too big for the receiving side, so
  it was dropped.
- Flag spots for more maps, placed by the host with F7: mp_merv, Poissons and
  Ederdam, saved in dom_flags.txt.
- Flags hang at the right height on the pole, and the pole no longer pokes
  through Rouen C's ceiling.
- Known: the flags don't wave, though you can hear them.

## 0.4.8: Aiming and abilities

- **Faster walk while aiming down sights**, with an F11 slider for how fast:
  40% of normal walking is the game's own, and 80% is the default.
- **Ability recharge:** the Scout's artillery, the Medic's health packs and
  Support's ammo come back sooner. The F11 slider defaults to half the wait,
  so artillery returns in about 90 seconds instead of 3 minutes.

## 0.4.7: Custom classes

- Five saved custom classes, each with a name, main weapon, sidearm, base
  class, grenade type and grenade count.
- **Create-a-Class editor on the kit screen: press R1.** Change it with the
  d-pad, A to use and B to close.
- The base class decides your ability: a Scout's artillery, a Medic's
  healing, and so on.
- Everyone starts with at least two grenades.
- Four of the second grenade slot (smoke) and of mines, instead of one.
- The class screen shows your custom class under its title.

## 0.4.6: Graphics options and movement

- **Graphics options in F11:** fog, draw distance, effects and dynamic
  lights.
- **Quick landing:** a heavy landing no longer slows you for up to two
  seconds.
- **Quick sprint recovery:** stamina comes back soon after a sprint.

## 0.4.5: Domination (the cut game mode, restored)

- Domination brought back, run on top of Team Battle: five flags (A to E),
  capture by standing on them, and score over time while you hold them.
- Rouen's own flag spots are built in. On other maps the host presses **F7**
  to place five flags where he stands, and the spots are saved.
- A **Game Modes** page in the lobby's Custom Rules turns it on or off.
- On-screen flag bar, capture progress, and flag markers in the world with
  letter, colour and distance.
- The host runs the match and sends flags and score to everyone else.

## 0.4.4: Custom rules for everyone

- The host's Custom Rules (health, gravity, speed, respawn time, round limit,
  last man standing) now reach everyone in the lobby. They're sent over a
  side channel on UDP port 28964.
- The host's own low gravity no longer gets reset by the map.

## 0.4.3: DLC maps and bots (experimental)

- **DLC maps:** dlc-tool\UNPACK-DLC.bat finds the Xbox 360 map packs (Champs,
  Valor and Bravo) and installs them. The maps show up in System Link's map
  list.
- **Bots (experimental, host only):** players that spawn, walk, aim and shoot
  on the host's machine. They're still a debug feature, not real opponents
  yet.

## 0.4.2: Multiplayer polish

- Sharp aiming by default, aim blur off, and a Multiplayer tab in F11.
- Multiplayer runs at your PC's frame rate, with an optional cap.
- No more glows showing through walls.
- Direct mouse aim, and CoD4-style bounces off slopes.
- **Custom Rules page** on System Link's Advanced Options screen.
- Tap to sprint.
- The shell shock's black, blurred screen can be turned off in F11.
- A melee lunge, like World at War's bayonet.
- Faster drawing (vertex buffers, waits), and a CPU profiler.
- System Link fix: replies now reach the right address.
- MAKE-FRIEND-PACKAGE / MAKE-FRIEND-UPDATE scripts to send the port to a
  friend.

## 0.4.1: Multiplayer arrives

- **CoD3MP.exe:** the multiplayer game, recompiled from the Xbox 360's
  multiplayer executable.
- System Link across every network adapter, including Tailscale for playing
  over the internet, with a peers list in the settings.
- Your own player name, and a separate identity per PC.
- Controller and audio volume fixes, and build scripts.

---

## Known issues (0.5.10)

- Lee-Enfield: after a reload or coming out of aim, the right hand can sit
  slightly inside the stock until the next shot.

## Known issues (0.5.0)

- Campaign guns: invisible in your hands, name reads "null", and big maps
  run out of memory with them on. Off by default.
- Domination flags don't wave.
- Frame rate can drop on very big maps like mp_merv.
- Bots are a debug feature only.





