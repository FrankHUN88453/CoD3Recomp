# Raising the 24-player limit: feasibility study

## 0.5.214 implementation checkpoint (client date October 17)

The audit-mapping blocker is resolved using exact addresses from the original executable through CoD3Scan. The prior 20261018 audit is superseded on that point. A new bounded join scan and explicit 32-entry roster mapper are prepared; active cap remains 24. Two of the three counted-loop candidates were indirect calls. Session state also uses 25 near the known 24 sentinel, so audit 23/25/derived sizes as well as 24. See `docs/player-limit/RESUME-214.md` and `docs/build-logs/validation-214.txt` for complete continuation instructions and actual validation state.


## 2026-10-18 status

The goal remains **32 players (16v16), technically plausible but not yet enabled**. Session bit masks are 32-bit, and 32-entry backing is already present for server-client records, game-player records and session-player records. The 0.5.213 solo log validates clean spare storage during one 20-bot run; it does not validate slots 24-31.

The working catalog has 1,107 rows; 67 are marked done. `BotsAsk` currently caps bot requests at 20, `MaxClients` remains 24, and entity numbering, UI capacities and some session paths remain unresolved. A trial source mapper also showed that generated-code edits can shift simple address-to-comment matching. Use exact PPC instruction context and alignment validation before classifying the catalog. See `docs/player-limit/AUDIT-20261018.md` for the count breakdown and next steps. No cap or game code changed in this audit.


*October 17, 2026, by Claude for Team Justin. The original feasibility analysis was read-only. Updated after 0.5.213 solo validation. Source: the retail `codmp_xenonf.xex` (decrypted image,
SHA-256 matches the disc) and our recompiled `ppc_mp` code (23,726 functions,
1.35 million instructions).*

> **0.5.213 solo validation:** the user's match log confirms the session's
> 32 x 784-byte roster allocation; 30 repeated checks copied all 24 active
> records with spare entries and guard clean. Existing game-player and server-
> client reserve checks also stayed clean. The match reached 20 bots and changed
> maps from mp_merv to mp_fuel. The active cap remains 24; this validates storage
> integrity for one run, not gameplay in slots 24-31. Two-PC testing and explicit
> respawn confirmation remain outstanding.
> **0.5.210 progress:** player-record storage has a 32-record heap allocation
> (0.5.208), and the server-client list now allocates and clears 32 x 4,992
> bytes (0.5.210). Slots 24-31 are marked and checked every 10 seconds, but
> no player loops use them. Active player limit remains 24. The server-client
> spare scan has not yet been checked in a match. Both full-match and
> System Link validation remain pending.
> **Correction:** sub_82594FD0 addresses a separate array at 0x82C03A50,
> stride 6224; it is not an end pointer for the 2992-byte player array.
> The two stride users listed below follow pointers; they were not changed.

> **Correction, 0.5.205 (October 17, 2026):** section 2 below is wrong. The
> 6,352-byte array at `0x82C2ECF0` has **4** entries (the console's local
> players), not 24: `0x82C2ECF0 + 4 × 6,352 = 0x82C35030` is the host's input
> state, and the gun-view pointers (`0x82C37070`) sit inside "record 5".
> That is why the "free gap" after it is in use (0.5.197 probe: written from
> the first server frame on both PCs). The 24 server players' records
> (entity +512) are elsewhere; 0.5.205's probe logs their addresses and
> spacing. Re-plan Phase 1 from that log. The constant sites of the 4-entry
> array are in `docs/player-limit/player-state-sites.csv` (503 sites, 174
> functions) for reference.
>
> **Found, 0.5.206:** the 24 server players' records are at `0x82BF21D0`,
> **2,992 bytes** each, contiguous to `0x82C03A50`, which is referenced
> straight after (no room to grow). Only about 6 functions address the array
> directly (`sub_82593FA8`, `sub_82595118`, `sub_82506540`, `sub_82594FD0`,
> and the stride users `sub_825611D8`, `sub_8256CF80`); everything else goes
> through entity +512. So Phase 1 is to move it to a new 32-entry block and
> patch those sites, which is much smaller than section 2 assumed.

## Verdict

| Target | Verdict | Why |
|---|---|---|
| **16 v 16 (32 players)** | **Possible, but a big multi-build project.** Recommended goal. | Every per-player structure can be enlarged. The game's player bit masks are 32-bit, so 32 is the most that fits the network messages unchanged. |
| **18 v 18 (36 players)** | **Possible only with extra network work.** | Everything 32 needs, **plus** widening the 32-bit player masks in session messages on both PCs. (The gap below turned out to be in use, see the update: the player states must move either way.) |
| 30 v 30 (60) | Not practical | Past the free memory gap and the 32-bit masks. Most structures would have to move. |

Rough effort for 32 players: **6–10 builds**, each needing a test, and a
two-PC test at the end. Spread over a few weeks of sessions, *if* the first
probe build (Phase 0) confirms the assumptions below. Thirty-six adds roughly
half again.

**Can't do it as a quick hack:** there is no max-players setting. 24 is
hard-coded at every level (size, loops, "no player" value).

---

## What holds the limit (evidence)

### 1. Server client list: easy

- Allocated once at server startup: `sub_825336F0` calls the allocator with
  `0x1D400` bytes = **24 × 4,992**, then stores the pointer at
  `svs+8` (`0x82B3FDE8`). The same constant appears in **6 functions**.
- It is heap memory, so growing it means changing one size (`32 × 4,992 =
  0x27000`) plus the loops that walk it. For example, the join function
  `sub_824046D8` scans for a free slot with a loop unrolled by 6 that ends at 24.
- **51 functions** use the 4,992-byte stride.

### 2. Player states (game clients): promising

- A **fixed array in the program's data** at `0x82C2ECF0`, 24 × 6,352 bytes,
  ending at `0x82C54070`. **174 functions** reference it, and **72** use its
  6,352 stride.
- **Directly after it is an 80,972-byte gap** (`0x82C54070`–`0x82C67CBC`):
  - all zero in the image
  - **no code references any address in it**
  - the next referenced address is `0x82C67CBC`
- That's room for **12.7 more records**. So 32 players (+8) fits, and 36 (+12,
  76,224 bytes) just fits, **without moving the array**.
- **Caveat:** the gap may still be reached through a pointer computed at run
  time. A live write-watch on the gap during a full match must confirm it is
  unused before relying on it (Phase 0).

### 3. Session players: hardest part

- 24 records of 784 bytes inside the session object, at `object + 0x1010`.
  The array ends at `object + 0x5990`, and the very next bytes are live fields:
  - this machine's 4 local-player slots (`+0x5991`)
  - the "match started" flag (`+0x5995`)
  - (both are used by our 0.5.194 bot code)
- So **it cannot grow in place**. It must move to a new, larger array. That
  means retargeting every access:
  - **88 functions** use the session object's player offsets (`4112`, `4116`,
    `4120`, `4124`)
  - **55** use the 784 stride
- These are ordinary code edits, applied when the game code is generated
  (`scripts/patch_recomp.py` already patches generated code this way). But
  they are many, and they must all be correct.

### 4. The "no player" value is 24

- Session player numbers 24 and up mean "nobody". Empty local-player slots are
  filled with 24 (for example `li r11,24` then `stb` in the join path).
- **17 functions store 24 as that value directly**, and many more test
  `number >= 24`. Raising the limit to 32 needs a new "nobody" value (for
  example 63), changed consistently everywhere. One miss turns "nobody" into
  real player 24.

### 5. Network formats

- The session layer builds player bit masks as **32-bit words**, one bit per
  session player:
  - `sub_823D8630`
  - `sub_823DC870` (`slw 1,n` / `or` / loop to 24 over the 784-byte records)
- One message handler (`sub_823DB378`) reads a packed per-player field from a
  message and walks it bit by bit up to 24.
- **Up to 32 players fits these formats as they are. Thirty-six needs wider
  masks in every message that carries one**, and both PCs must run the same
  exe.
- Player numbers in messages are bytes (fine up to 255).
- **No Xbox Live session slot limit applies:** our System Link emulation has no
  XSession slot code, so the 24 is purely the game's.

### 6. Loops and compares against 24

- **128 functions** compare against 24 (160 compares):

| Area | Functions |
|---|---|
| Session and party layer | 67 |
| Game and cgame | 36 |
| Engine, UI and other | 17 |
| Server | 8 |

- About **20 look unrelated** (asserts, allocator tables, the sound engine) and
  must be left alone.
- Each of the other ~108 needs reading to decide if it is a player loop.
- There are also 2 counted loops (`mtctr 24`) and loops written other ways
  (unrolled, counting down, limits held in registers). So the real list is
  longer than the compare count.

### 7. Entity numbering: unknown

- Our bot code reads a player's entity as `EntityList[slot + 1]`, so players
  occupy the first entity numbers. If non-player entities start right after
  player 24, more players means shifting where world entities start.
- I could not pin down that start point from the code alone.
- Because every machine runs its own server, entity numbers are local to each
  PC, and players are matched by session number. That probably keeps this a
  local change, not a network one, but it needs the Phase 0 probe.

### 8. Things that also need checking

- **Scoreboard and HUD:** UI lists sized for 12 per team, the compass, the kill
  feed.
- **Per-player arrays in rendering and animation:** model and animation caches
  per client. The recurring record sizes inside the player loops (30,932 and
  30,800 bytes in 21 functions; 5,092; 7,520; 15,000) need identifying.
- **Bots:** `bots.cpp` itself assumes 24 (`MaxClients`, slot 4–23, session
  numbers < 24). That's easy since it's our code, but it must change in step.

---

## Update: Phase 0 probe results (0.5.197 test, October 17, 2026)

Justin's 17-minute Merville match with 20 bots answered two of the three
questions:

- **The space after the player states is NOT free.** The probe found a byte
  set after the very first server frame (`0x82C54082`; the words at
  `0x82C54080` are `000008EB 08ED0000`). For the whole match, 6,215 of the
  80,972 bytes stayed set at the same count. So some other data lives there,
  reached through a pointer the code builds at run time (which is why no code
  names those addresses). **The player-state array cannot grow in place.** It
  has to move to new memory, like the session list. That means retargeting the
  174 functions that use its address and the 72 that use its 6,352 stride.
- **The session object is on the heap** (`0x67070970` in that run), not in
  the executable's data. Giving it a bigger player list is therefore the same
  kind of job as the server's list: allocate a larger one and point the code
  at it.
- Entity numbering is still unknown.

**Revised verdict:** 16 v 16 is still possible, but it's now a bigger job.
Both the player states and the session players must be relocated, and the
server list must be enlarged. Estimate about 10–14 builds instead of 6–10.
18 v 18 costs about the same on top of that, plus widening the network masks.
Nothing here rules it out, but it should start only once the bots are
settled, and it will need a debug build with bounds checks.

## Phase 1 started: the site catalogue (0.5.202, October 17, 2026)

Every place in the multiplayer executable that has to change is now listed
in `docs/player-limit/sites.csv`: one row per instruction, with its kind,
function and address, and a status column (todo / done / not a player site).
It was made by `scripts/player_limit_sites.py` from the decrypted executable
(the instructions themselves are not in the repo: they are the game's code).

| Kind | Sites | Functions | What changes |
|---|---|---|---|
| player_states_address | 503 | 174 | the player-state array's address, to the new array |
| svs_field | 146 | 61 | the server's fields (the client list pointer among them) |
| player_state_stride | 86 | 64 | indexing by 6,352 (stays; the base moves) |
| session_player_offset | 66 | 53 | the session list's offset in its object, to the new list |
| server_client_stride | 52 | 44 | indexing the server's 4,992-byte clients (stays) |
| store_24 | 49 | 44 | 24 written as "no player": the new no-player value |
| session_player_stride | 41 | 34 | indexing the 784-byte session players (stays) |
| compare_24 | 160 | 128 | loops and checks up to 24: to 32 where they are player loops |
| loop_24 | 3 | 3 | counted loops of 24 |
| server_alloc_0x1D400 | 1 | 1 | the server's client list size (sub_825336F0) |
| **total** | **1,107** | **421** | |

Not every compare is a player loop (about 20 functions are asserts or table
sizes); each row gets read and marked before it is patched.

**Next builds:**
1. Review and mark the remaining player-related sites in `sites.csv`: 49
   no-player stores, 160 compares, three counted loops, plus UI and bot limits.
   Confirm which compares are player loops before patching. Keep the cap at 24.
2. Patch the reviewed session/client loops and sentinel value together, retaining
   the 24-player cap for an integration build. Add tests for untouched sites and
   ensure spare slots 24-31 remain dormant.
3. Only after the whole 32-slot path is covered, raise player/session/bot limits
   together and validate solo first, then two PCs. Both must run the same build.

## Plan if we go ahead

**Phase 0: probe build, no behaviour change (1 build, solo test)**
- Write-watch the player-state gap (`0x82C54070`–`0x82C67CBC`) during a full
  20-bot match. It must stay untouched.
- Log the highest entity number in use, and where non-player entities start.
- Find the session object's allocation size and the call that makes it.
- Log every reach of a 24-limit site in a match, to separate live loops from
  dead code.

**Phase 1: server + game (2–3 builds)**
- Server client list allocation to `32 × 4,992`.
- Player states grown into the gap.
- Server and game loops 24 → 32.
- `bots.cpp` `MaxClients` 32, so bots can use slots 4–31.
- Test: solo with 28 bots.

**Phase 2: session layer (2–3 builds)**
- Move the session player array to a 32-entry block.
- Retarget the 88 / 55 access functions.
- New "nobody" value.
- Session loops 24 → 32.
- Test: solo, then two PCs.

**Phase 3: UI and the rest (1–2 builds)**
- Scoreboard rows, compass, kill feed.
- Any per-player render or animation arrays found in Phase 0.

**Phase 4: 36 players (optional, 2+ builds)**
- Only if Phase 0 confirmed the gap holds 36.
- Widen the 32-bit player masks in session messages (both PCs).

**How the edits would be made:** as rules in `scripts/patch_recomp.py`
(rewriting the generated C before it compiles), each one listed by function
address in `docs/HANDOFF.md`. That keeps every change reviewable and
reversible, and the original executable is never touched.

## Risks

- **A missed site corrupts memory.** We have seen this before: in 0.5.159, bots
  overflowed the 4-entry local-player list. Mitigation: Phase 0 instrumentation,
  plus a debug build with bounds checks on the moved arrays.
- **Both PCs need the same build**, and old builds can't join new ones.
- **DLC and map scripts may assume 24** (spawn point counts per map, team
  balance). Maps with only about 12 spawns per side will make players spawn
  close together.

## Recommendation

Aim for **16 v 16** first. Start with the **Phase 0 probe build**: it is
low-risk (logging only), and it settles the three unknowns that decide the
real cost: the free gap, entity numbering, and session object size. Decide
after reading its log. Do the 12 v 12 bot lobby setting and the vehicle bots
alongside, since none of this blocks them.



