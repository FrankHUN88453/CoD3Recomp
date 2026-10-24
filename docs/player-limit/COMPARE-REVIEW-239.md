# compare/store/loop 24 review (0.5.239, Claude, 2026-10-18)

All 212 compare_24 / store_24 / loop_24 rows of sites.csv classified from the ORIGINAL code
(llvm-objdump of the decrypted image mp.bin, not generated C). Full table with 7-instruction
context per site: compare-review-239.csv (category_auto column; "?" = best guess).

| group | sites |
|---|---|
| A session roster (784 B records, storage already 32) | 62 |
| B server clients (4992 B, storage already 32) | 6 |
| C lobby/party arrays inside the session object (storage still 24) | 14 |
| D entity list / local places / client-number checks | 10 |
| E menu/lobby UI lists (24 rows) | 12 |
| F value 24 in a state field (likely an enum, not a count) | 14 |
| G unrelated (sizes, formats, registry of 24 objects, clamps, audio, files) | 93 |
| H still unknown | 1 |

Findings:
- A and B: loops/checks over the 24 session players (784 B) and server clients (4992 B); their storage
  is already 32 (0.5.210/0.5.213), so these bounds can move to 32 together.
- C is NEW: the session object also holds per-member arrays sized 24 that were never relocated
  (strides 160 B at +632/+700, 200 B at +7536, 80 B at +4443, 48 B at +6336, 8 B XUID lists).
  Raising C needs storage first (relocate or extend those arrays), like the earlier three layers.
- D: EntityList walks (+4..+96) and the local-place sentinel 24 (byte +22929, +0x10841) - entity
  slots for players 24..31 must exist first (0.5.218 moved the level slot to +132; still to drop +100).
- E: lobby/scoreboard menus list 24 rows; extra players would simply not be listed until widened.
- F: 24 compared against a state field at session+0x3C6A0 / global+0x27BC4: an enum value, NOT a
  player count - leave alone.
- G: 0x82C77850 is a registry of 24 singletons (14 constructors register into it), bit depths
  16/24/32, memcpy sizes, clamps, audio (fmod), journal files - leave alone.

Next: (1) relocate group C arrays (find the session object size/allocation, give 32 entries);
(2) entity slots 24..31; (3) patch A+B+C+D bounds together behind one switch, cap still 24 until
all are done; (4) UI rows.
