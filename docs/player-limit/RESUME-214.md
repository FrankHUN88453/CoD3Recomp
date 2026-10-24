# 0.5.214 player-cap work: resume here

Client date: October 17, 2026. Source version is now 0.5.214 (PREPARED); local installed game remains 0.5.213 until an explicit later install record. Active capacity is STILL 24, requested bots STILL 20. Do not tell the user 32-player gameplay is enabled.

## What was resolved

The project already has `tools/build-scan/CoD3Scan.exe`. It can read the original XEX and print exact instruction addresses:

    CoD3Scan.exe CoD3RecompLib/private/codmp_xenonf.xex --disasm 0x823F4AF0 24

A local full .text dump was produced with `--disasm 0x820B0000 1296501`. The original .text is 0x820B0000..0x825A21D4, image base 0x82000000. Dump: chat workspace `work/mp-original-disassembly.txt` (61,383,682 bytes). DO NOT upload this or bundle it into shared source.

`work/recover_original_sites.py` reads that dump by EXACT instruction address, not by counting generated C comments. It recovered all 1,107 catalog addresses. It explicitly validated the expected opcode/immediate shape for all 160 compare_24, 49 store_24, 3 loop_24, and 66 session_player_offset rows. Other categories were looked up but their semantics were not validated. Evidence: `docs/build-logs/player-cap-original-sites-214.csv` (local only).

The catalog itself was not corrupt. Previous index-based attempts failed to handle no-operand instructions such as bctrl/blr/nop, and generated replacements also make counting comments fragile. Do not continue those failed index-only methods. The old 20261018 audit's inability to map sites has now been resolved using the original executable. The fourth-column instruction/status schema difference still exists, but the original instruction text is recovered in the local audit CSV. patch_recomp uses only kind/function/address.

## New concrete findings

1. Join function sub_824046D8 has a six-at-a-time free-server-client scan at 0x82404850..0x824048E0. It increments index by six at 0x824048AC; the limit comparison is 0x824048B4. With a bare change from 24 to 32, a full scan would read slots 32-35 and exit with 36. This MUST be rewritten before raising the cap. The existing final full-list rejection at 0x824048E4 and subsequent setup/connect path remain necessary.
2. The 0.5.213 session helper only redirected effective addresses inside the original 24-entry roster. A caller pre-indexed to slot 24 would otherwise resolve into the inline local-place area instead of the new allocation. Original capacity and reserved capacity must be distinguished.
3. Two catalog `loop_24` rows are false counted-loop classifications: 0x82217724 is li r4,24 before mtctr r11/bctrl; 0x8221EFBC is li r3,24 before mtctr r11/bctrl. The 24 is NOT the counter in either. Do not rewrite as player loops. Investigate the argument independently if needed. The third at 0x824B1BE0 really loads r10,24 and mtctr r10, clearing 24 words; whether that array is a player array is STILL UNREVIEWED.
4. 0x823F4B1C really is li r11,24 followed by stb r11,22929(r10), confirming the local-place sentinel. Nearby code also compares a local-place byte to 25 at 0x823F4B00. Therefore auditing only immediate 24 is incomplete: transitional sentinels 25 and upper-valid index 23, plus derived byte sizes, also need inspection before activation.

## Prepared source changes (0.5.214)

- `CoD3Host/player_capacity.h`: central ActiveClients=24, StorageClients=32, ReservedLocalClients=4, MaxBots=20, server stride 4992. This is NOT a supported single-toggle 32-player switch; all guest paths still need coordinated changes.
- `CoD3Host/server_client_scan.h`: bounded one-at-a-time search, returns count if full; safe for both 24 and 32.
- `mp_fixes.cpp`: `MpFindFreeServerClient(PPCContext&, uint8_t*)` uses that search and updates r11=index, r31=selected client/one-past if full, r10=last state loaded, preserving the original scan's outputs.
- `scripts/patch_recomp.py`: validates six reads/six strides/one six-index increment and absence of outside jumps into the removed block; replaces only scan 82404850..824048E0. Preserves final rejection/setup/connect code. Idempotent and rejects changed shapes before writing.
- `session_player_storage.h`: adds explicit `RedirectRosterAddress` for reserved 32-entry roster accesses, with integer-overflow checks. The generic `RedirectAddress` stays limited to the original 24 entries, protecting adjacent inline fields. Only known roster helper calls use the new API.
- `mp_fixes.cpp`: `MpSessionPlayerAddress` uses explicit roster mapping so slots 24..31 can be routed correctly in a future capacity build. Existing active loops remain 24, so reserves should stay untouched.
- `bots.cpp`: MaxClients/BotsAsk draw the SAME existing 24/20 limits from the central header; no bot-cap change.
- `version_number.h`: 0.5.214.
- Tests added/extended: `tests/server_client_scan_test.cpp`, `tests/server_client_scan_patch_test.py`, `tests/session_player_storage_test.cpp`.

Backup of all changed originals, including generated join code: chat workspace `work/before-214/`.

## Validation checkpoint at first write

PASS: Python scan-patch test (idempotence, retained downstream code, refusal on malformed scan/external entry); existing session patch tests (2) and player-storage patch tests (3). Actual generated MP patch applied successfully, including all existing 66 roster sites and the new bounded scan. A second idempotence run was started; check final session/output.
PENDING: C++ boundary tests, Release build, install/package, runtime test. No new binary installed at this checkpoint. See validation-214.txt for updates that supersede this paragraph.

## Next work

1. Finish C++ boundary tests and Release CoD3MP/CoD3 build. Record hashes and precise installed state. Do not overwrite a running game.
2. No extra-player test is valid yet: active cap is 24. If installing 214, test normal host/bot join/full-server behavior, spare/guard checks, respawn/map transition. Two PCs are not available now; network behavior remains unvalidated.
3. Classify the original-code candidates, including additional compares against 23/25 and sizes based on 24. Catalog currently still mostly todo; six sites now carry explicit reviewed/replaced statuses in sites.csv and reviewed-sites-214.csv. Recovered addresses are NOT completed integration.
4. Audit game/client/entity reservation and ordering, every session sentinel and lookup, session message packing, bot local-place isolation, UI/scoreboard/per-client animation arrays. 32-bit masks can encode 32 indices but that alone does not prove every network message supports it.
5. Only activate >24 when storage, session loops/sentinels, server/game/entity/bot/UI paths agree. Both PCs must use identical builds for the later System Link test.

## Additional activation requirement

Allocation/rollback paths still fall back to original 24-entry storage. Before enabling >24, extra-player admission must refuse slots beyond the actually allocated capacity if an extended allocation fails or either relocation rollback environment variable is enabled. The new explicit roster mapper alone does not make a 32-slot configuration safe under fallback.

## Final review before finishing the build

The roster mapper now takes a record-count parameter (default StorageCount for explicit tests), and the runtime MpSessionPlayerAddress passes PlayerCapacity::ActiveClients (24). This preserves the existing no-player-24 behavior until coordinated sentinel activation; the mapper is tested for 32 but the runtime MUST NOT reinterpret a legacy sentinel as active slot24. Session boundary tests also cover count0, count33 and the runtime24 boundary. An initial 214 binary was installed/package-verified before this final guard; a final rebuild/refresh is required and final hashes must supersede the initial ones.

## 0.5.215 addendum (Claude, 2026-10-18)

- The third loop_24, 0x824B1BE0, IS a player loop: sub_824B1B28 clears
  EntityList (*0x82BCC7FC) words +4..+96 (player entities 1..24), then +100.
  +100 (entity index 25) is a level/world entity read directly at 41 sites
  (`lwz rX,100(EntityList)`). Raising the cap must give players 24..31 new
  entity indices or relocate that entity; also check what follows +100 and
  how the entity list is sized/allocated before any change.

## 0.5.218 addendum (Claude, 2026-10-18)

- EntityList's level slot moved: object 104 -> 136 bytes, level read at +132
  (40 sites), written at +100 and +132 (4 sites) - patch_entity_list_level in
  scripts/patch_recomp.py; lists in entitylist-level-sites.csv and
  entitylist-uses-218.csv. Before activation: drop the +100 copy; raise the
  37 pointer-walk loops, sub_8249B1E0 (spawns 24) and sub_824B1B28 (clears
  24) to 32; review the 402 indexed uses for slot < 24 assumptions.
