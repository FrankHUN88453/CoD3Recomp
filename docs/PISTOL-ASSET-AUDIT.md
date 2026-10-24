# CoD3 handgun asset audit (Xbox 360 retail data)

*October 17, 2026, by Claude for Team Justin. Read-only investigation. No game
file, build or weapon registration was changed.*

## Answer in one paragraph

Treyarch shipped **two** real handguns in Call of Duty 3 for Xbox 360, both
multiplayer only: the **Colt M1911 (`colt`)** and the **Walther P38 (`p38`)**.
The campaign has **no handgun at all**: no weapon file, no model, no animation,
no sound. Inside the retail multiplayer executable, though, there is **cut dual
pistol code**. A loadout-script function gives the player **`coltdual`**
(Allies) or **`p38dual`** (Axis), with ammo scaled ×2 or ×4 by level. The weapons
it names have **no definition and no assets anywhere** on the disc or in the DLC.
The text bank in both `global.cod` files also lists **Luger**, **Beretta
M1934**, **MAS1935 Pistol**, **Colt .45 Dual**, **MAS1935 Dual Pistols** and
**Walther P-38 Dual**, but none of these has a weapon key, file, model,
animation or sound. The **Webley** and the **Browning Hi-Power** do not appear
anywhere, in any form.

| Handgun | Classification | What exists |
|---|---|---|
| Colt M1911 (`colt`, `WEAPON_COLT45`) | **CONFIRMED** (shipped, MP) | Full weapon: definition, view + world models, skeleton, textures, 10+10 viewmodel anims, 3rd-person reload anims, sounds, HUD icon, ID 8 |
| Walther P38 (`p38`, `WEAPON_P38`) | **CONFIRMED** (shipped, MP) | Full weapon, as above, ID 15 |
| Dual Colts (`coltdual`) | **PARTIAL** (code only) | Give/ammo logic in the MP exe, name text "Colt .45 Dual"; no weapon file, model, anims or sounds |
| Dual P38s (`p38dual`) | **PARTIAL** (code only) | Same function (Axis branch), name text "Walther P-38 Dual"; no assets |
| Luger P08 | **REFERENCE ONLY** | One display name in the text bank, plus a mention in the P38's armory description; nothing else |
| Beretta M1934 | **REFERENCE ONLY** | Display name in the text bank only |
| MAS 1935 (and "Dual Pistols") | **REFERENCE ONLY** | Two display names in the text bank only |
| Webley revolver | **Not present** | Zero hits in every archive, both executables and the DLC |
| Browning Hi-Power | **Not present** | Zero hits. "Browning" only ever means the M1919/M2 machine guns or the BAR |
| Campaign Colt / P38 | **REFERENCE ONLY** | A HUD-icon definition name and the bonus-menu armory text; no campaign weapon |

Nothing reached SPECULATIVE status worth listing. Every row above rests on bytes
read from the retail files, cited below.

---

## How this was done

**Source data.** The full disc extraction in `CoD3-PC-Port-0.4.0\game`
(installed from the ISO on 2026-10-09), the three DLC map packs, and both
executables from `CoD3RecompLib\private`.

**Integrity.** Every file cited here was SHA-256 checked against its ISO extent,
and all match:

| File | SHA-256 |
|---|---|
| `mp/global.cod` | `8137dfea…d2713c` |
| `sp/global.cod` | `b71fdf09…9c67dc` |
| `sp/frontend.cod` | `d01cca9e…fce156a` |
| `mp/weapusge.cod` | `dce003b1…bff459` |
| `mp/anim.cod` | `058f4501…76e6d` |
| `sp/anim.cod` | `29f28472…f1e` |
| `default.xex` | `2944eec7…23692` |
| `codmp_xenonf.xex` | `bd57d0df…82f6` |

So the strings below are Treyarch's, not ours.

**Tools and method.**
1. **Archive asset names.** I used the Oct 13 archive index (451 archives, 68,115
   entries, read from the uncompressed KAPF metadata) to search every asset name
   in all 15 campaign levels, 9 MP maps, `global`/`frontend`/`anim`, and the DLC.
   This covers all 14 campaign weapon archives (3,757 entries; the credits level
   has none).
2. **Raw strings.** I searched 1.48 million raw strings from every archive and
   sound bank.
3. **Executables.** Both XEX files are retail-encrypted and compressed, so their
   code is invisible to a plain string scan. I decrypted them locally (standard
   retail key, basic compression) into memory images at base `0x82000000`, then
   searched them. Code uses were traced through our recompiled `ppc_mp` sources.
4. **Live registration.** I read the weapon registration table captured from the
   running MP game in 0.5.190 (`docs/build-logs/mp-weapon-catalog-190.json`).
5. **Verification.** Every offset in this report was re-read from the raw file.
   All 44 matched.

**Limits.**
- Asset payloads (meshes, animations, audio) were not decompressed or rendered.
  "Model exists" means the asset entry and its source path exist in the
  archive's directory, not that the mesh was viewed.
- `WEAPONFILE:` lists could only be read in 9 campaign levels. In credits, forest,
  hostage, mayenne, saint_lo and stbert that list sits in compressed data. Those
  levels are still covered by the asset-name search (point 1), which finds no
  handgun asset in any of them.
- Compiled script callers of the dual-pistol function were not traced (see F3).

---

## Findings

### F1: Colt M1911, `colt`: CONFIRMED (shipped, multiplayer)

- **Registration:** live MP weapon ID **8**: `colt`, key `WEAPON_COLT45`, class
  key `WEAPON_SUPPORTGUNNER`, viewmodel `w_us_pist_VM`. It is `WEAPONFILE:colt`
  in all 9 MP maps whose weapon lists are readable (ederdam, forest, mp_fuel,
  mp_hosta, mp_merv, mpmaynne, poissons, rouen, verdun), and in **0** campaign
  levels.
- **Models** (`mp/weapusge.cod`):
  - `w_us_pist_VM.xemesh` (source path at `0xaf98`, `...\weapons\pistols\colt\`)
  - `w_us_pist_WORLD.xemesh` (`0xafdc`), with world sub-meshes `w_usPSTL_WM1..3`
  - `colt.xeskel` (`0xbdf8`), bones `colt`, `colt_hammer`, `colt_mag`,
    `colt_slide` (`0x24b5c`…)
- **Textures:** `W_us_colt_c_ng`, `W_us_colt_n`, `W_us_colt_s_ng`, `w_us_pist_TB`.
- **Animations:** the viewmodel set `w_usCOLT_{ads_down, ads_up, drop, fire,
  fireADS, idle_a, idle_ads_a, melee, raise, reload}` plus the matching
  `w_arms_usCOLT_*` set (10 + 10).
- **Sounds:** `COLT_FIRE_1ST` and `COLT_FIRE_3RD` in each MP map's `.wbk` (for
  example `mp/forest/forest.wbk@0x64a2`). Events `colt_DEPLOY`,
  `colt_RELOAD_START/END`, `colt_RECHAMBER`, `colt_ALT_SWITCH`,
  `colt_AMMO_PICKUP`, `colt_BREAKDOWN` and `GR_COLT` in the map archives.
- **HUD:** `hud_colt45.xetex` in `mp/weapusge.cod` (`0x7df0`) and an NG copy in
  `mp/global.cod` (`0x4440`).
- **Behaviour:** yes. It is the shipped Allied sidearm. The loading-screen tip
  in every map says "All Allied soldiers carry the Colt .45 as a backup sidearm."
- **Restore to MP:** already in MP.

### F2: Walther P38, `p38`: CONFIRMED (shipped, multiplayer)

- **Registration:** live MP weapon ID **15**: `p38`, key `WEAPON_P38`, viewmodel
  `w_ge_pist_VM`. `WEAPONFILE:p38` in all 9 readable MP maps, and in 0 campaign
  levels.
- **Models** (`mp/weapusge.cod`):
  - `w_ge_pist_VM.xemesh` (`0xb38c`, `...\weapons\pistols\p38\`)
  - `w_ge_pist_world.xemesh` (`0xb3d0`), with world parts `p38w`, `p38w01`,
    `p38w02`
  - `p38.xeskel` (`0xbf5c`), bones `p38`, `p38_hammer`, `p38_mag`, `p38_slide`
- **Textures:** `W_P38_c_ng`, `W_P38_n`, `W_P38_s_ng`, `p38_world`.
- **Animations:** `w_p38_*` and `w_arms_p38_*` (10 + 10, same set as the Colt).
- **Sounds:** `P38_FIRE_1ST` and `P38_FIRE_3RD` in each MP map's `.wbk`. Events
  include four `p38_NOTE_TRACK_SOUND_A..D` entries.
- **HUD:** `hud_p38.xetex` (`mp/weapusge.cod@0x8018`, `mp/global.cod@0x46cc`).
- **Behaviour:** yes, the shipped Axis sidearm.
- **Restore to MP:** already in MP.

**Shared third-person pistol animations** (`mp/anim.cod`):
`MP_Pistol_Reload` (`0x7628`), `MP_Pistol_Prone_Reload` (`0x75e0`), and
**`MP_Horch_Pistol_Pass_Idle`** (`0x6e68`). The last one is a Horch passenger
holding a pistol. The executables never name it (it only appears in
`anim.cod`'s own tables), so I could not tell whether it plays in the shipped
game. It hints that passengers firing pistols was considered.

### F3: Dual pistols, `coltdual` / `p38dual`: PARTIAL (cut feature, code only)

This is the one real hidden handgun feature on the disc.

**Code: implemented, in `codmp_xenonf.xex`.** Function `sub_821EE270(player,
level)`, compiled from `c:\cod\code\script\_mp_loadout.bro` (path at
`0x820396B4`), does this:
1. If `level == 0`, return.
2. Name = `"coltdual"` (`0x8203973C`). If the player's team string equals
   `"axis"` (`0x8205FCD0`), name = `"p38dual"` (`0x82039734`).
3. Give that weapon to the player (engine callback `+2664`).
4. On slot **`"pistol"`** (`0x82074DA4`): set clip and stock to 0 (`+2864`,
   `+2872`). Read the clip size (`+2876`). Then set ammo to clip × **2 at level
   1, ×4 at level 2** (×2 otherwise), and put the excess in the clip.

It sits in the loadout script's native function table at `0x82094578`
(pointer `0x821EE270`, flags `0x40007503`). Its neighbours in that table are the
**shipped** kit helpers:

| Function | Strings it uses |
|---|---|
| `sub_821EE110` | `american` |
| `sub_821EE1C0` | `german` |
| `sub_821EE448` | `bazooka` / `panzerschreck` (`primaryb`) |
| `sub_821EE5B8` | `mp_satchel` (`special`) |
| `sub_821EE6C0` | `mp_mine` (`special`) |

So dual pistols were built the same way as the bazooka, satchel and mine kit
items, with level-based upgrades like the shipped binocular ranks
(`mp_binoculars_nospot_rank2/3`).

**Names in the text bank:** "Colt .45 Dual" (`mp/global.cod@0x120aac`,
`sp/global.cod@0x7ba0f8`) and "Walther P-38 Dual" (`0x120aec` / `0x7ba138`).

**What is missing:**
- **Weapon definition:** none. No `WEAPONFILE:coltdual` or `WEAPONFILE:p38dual`
  in any MP map, campaign level or DLC pack. Neither is registered in the live
  MP weapon table (IDs 1–41 are retail, 42–47 are our campaign guns).
- **Model:** none. No left-hand or dual viewmodel exists. The only
  "left hand" items are the generic `left_hand_align_dummy` weapon bones.
- **Animations:** none (no dual/akimbo anim names anywhere).
- **Sounds:** only the single-pistol Colt and P38 sounds exist (reusable).
- **Behaviour:** the give/ammo logic is implemented. Akimbo firing is not
  evidenced anywhere.
- **Caller:** not traced. The table entry is reachable by the loadout script,
  but I found no proof the shipped script ever calls it. If it did, giving an
  unregistered weapon would fail, which may be why it was cut.

**Restore to MP: possible, but this is a build, not a recovery.** Two routes:
- **Easy:** register `coltdual`/`p38dual` as copies of `colt`/`p38` with a larger
  clip, using our existing campaign-gun copy mechanism (`mp_fixes.cpp`,
  `CampaignGun`). They reuse the single-pistol model, animations and sounds.
  The native function then works as written.
- **Real dual wield:** needs a second hand model and akimbo fire logic. None of
  that exists on the disc.

Either way, test both PCs in System Link.

### F4: Luger P08: REFERENCE ONLY

- **Text bank:** display name "Luger" at `mp/global.cod@0x1204bc` (copy at
  `0x1126cbc`) and `sp/global.cod@0x7b9aec`, inside the weapon-name list with no
  matching weapon.
- **Armory:** the campaign bonus menu's P38 description says "...easier to
  manufacture than the Luger it replaced" (`sp/frontend.cod`, P38 entry at
  `0x35f4f80`). That is prose, not an asset.
- **Missing:** no `WEAPON_LUGER` key, no `WEAPONFILE:luger`, no mesh,
  skeleton, texture, animation, sound or HUD icon in any archive, executable or
  DLC.
- **False positives ruled out:** "P08" byte runs in both executables are inside
  code and data, not strings.
- **Restore to MP:** not from Treyarch assets. It would need a new model.

### F5: Beretta M1934: REFERENCE ONLY

- **Text bank:** display name "Beretta M1934" at `mp/global.cod@0x1203dc` and
  `sp/global.cod@0x7b9a0c`, next to "Beretta M1938A" (an SMG).
- **Missing:** no key, weapon file or asset of any kind.
- **Restore to MP:** not from Treyarch assets.

### F6: MAS 1935: REFERENCE ONLY

- **Text bank:** "MAS1935 Pistol" (`mp/global.cod@0x120548`,
  `sp/global.cod@0x7b9b78`) and "MAS1935 Dual Pistols" (`0x120ac8` /
  `0x7ba114`).
- **Missing:** no key, weapon file or asset of any kind.
- **Restore to MP:** not from Treyarch assets.

### F7: Webley revolver, Browning Hi-Power: NOT PRESENT

Zero matches for webley, revolver, hipower, hi-power, Hi Power or "Browning HP"
in all 451 archive indexes, all raw archive and sound-bank strings, both
decrypted executables, and the DLC packs. Every "Browning" hit is the
M1919A6/M2 machine guns, the BAR description, or `w_us_browning_T.xetex` (a
machine-gun texture). British troops in the campaign have no sidearm data at
all.

### F8: Campaign handgun traces: REFERENCE ONLY

- **No campaign handgun weapon:**
  - None of the 14 campaign weapon archives contains a handgun asset.
  - No campaign level registers a handgun `WEAPONFILE`.
  - `default.xex` contains no handgun weapon name. It only has the shared engine
    strings: the generic `"pistol"` weapon slot ("Valid weaponslots are
    'primary', 'primaryb', 'pistol', 'grenade', and 'smokegrenade'" at
    `0x82073990`), `MOD_PISTOL_BULLET`, and `GAME_CANT_GET_PISTOL_WEAP_MESSAGE`.
  - The campaign's AI weapon tuning table (`0x825F2200`…) lists every campaign
    gun and no pistol.
- **`hud_colt45.tga`** at `sp/global.cod@0x17e33`: a HUD image definition by
  name, but there is no `hud_colt45` texture in any campaign archive. It is a
  leftover reference.
- **Bonus menu armory** (`sp/frontend.cod`): entries "COLT M1911" (`0x35f3f48`)
  and "WALTHER P38" (`0x35f4f80`) with history text. The same armory also lists
  the 105mm howitzer, Flak 88 and Nebelwerfer, so it describes the war's
  equipment, not the player's weapons.
- **"This P38 that I took off this officer…"** (`sp/global.cod@0x7b7788`): a
  subtitle from a veteran interview in the bonus documentary. Not a weapon.
- **False friends:** `VEHICLEFILE:P38`, `rocket_explode_p38` and
  `fire_trail_rocket_p38` are the **P-38 Lightning aircraft** (Black Baron,
  Laison). Every `bullethit_pistol_*` and `colt_*`/`p38_*` surface entry is
  impact-effect tables, not weapons.

---

## Context: the shared text bank (why the Luger/Beretta/MAS names exist)

The weapon-name list containing the Luger, Beretta and MAS strings is identical
in the campaign and MP `global.cod`. It also contains things CoD3 for Xbox 360
never had:
- a Carcano M1938, Breda M1930, Chatellerault, MAS36 rifle and MAS38 SMG
- a **"Maquis Fighter"** class
- "[HOLD_USE] to use the BM37 / Hotchkiss"
- **PlayStation 2 DUALSHOCK controller messages** (`mp/global.cod@0x10fdb0`)

That points to a text bank shared across Treyarch's console builds, not to
cut Xbox 360 content.

The MP executable has related leftovers too:
- a `"vichy"` team string next to the loadout script (`0x82039670`)
- `shotgun_maquis`, `m1garand_medic` and `m1carbine` in both executables'
  weapon tables

Those are outside this pistol audit but worth a separate look.

---

## Restoration recommendations

1. **Dual pistols are the only Treyarch handgun content worth recovering**
   (F3). The give/ammo logic is retail and working. Recommended route: register
   `coltdual` and `p38dual` as Colt/P38 copies with a bigger magazine, through
   the existing `CampaignGun` copy mechanism. Hook them up as an optional class
   or perk item. Label it honestly as "restored cut feature, single-pistol
   model". True akimbo would be new work, not recovery.
2. **Do not advertise the Luger, Beretta, MAS 1935, Webley or Hi-Power as
   restorations.** For the first three, only a display name exists. For the
   last two, nothing exists. Any of them would be a brand new weapon, which
   also raises the "players supply their own game files" question for a
   public release.
3. **The Colt and P38 are complete and already in MP.** They are also the
   donors for anything above, with models, skeletons, animations, sounds and
   HUD icons all present.
4. **Before any build:**
   - Trace whether the compiled `_mp_loadout` script ever reaches table entry
     `0x82094578`. Log calls to `sub_821EE270` in a debug build, without
     changing behaviour.
   - Decide how dual pistols get handed out (class, perk or lobby rule).
   - Check that an extra weapon ID stays network-identical on both PCs (the same
     check our campaign guns needed).

## Evidence index

| Ref | File | Offset | Bytes read (verified) |
|---|---|---|---|
| E1 | `mp/weapusge.cod` | `0xaf98` | `...\weapons\pistols\colt\w_us_pist_VM.xemesh` |
| E2 | `mp/weapusge.cod` | `0xafdc` | `...\weapons\pistols\colt\w_us_pist_WORLD.xemesh` |
| E3 | `mp/weapusge.cod` | `0xb38c` | `...\weapons\pistols\p38\w_ge_pist_VM.xemesh` |
| E4 | `mp/weapusge.cod` | `0xb3d0` | `...\weapons\pistols\p38\w_ge_pist_world.xemesh` |
| E5 | `mp/weapusge.cod` | `0xbdf8` / `0xbf5c` | `animdefs\colt.xeskel` / `animdefs\p38.xeskel` |
| E6 | `mp/weapusge.cod` | `0x7df0` / `0x8018` | `HUD_COD3\hud_colt45.xetex` / `hud_p38.xetex` |
| E7 | `mp/global.cod` | `0x4440` / `0x46cc` | `HUD_COD3\NG\hud_colt45.xetex` / `hud_p38.xetex` |
| E8 | `mp/anim.cod` | `0x6e68`, `0x75e0`, `0x7628` | `MP_Horch_Pistol_Pass_Idle`, `MP_Pistol_Prone_Reload`, `MP_Pistol_Reload` |
| E9 | `codmp_xenonf.xex` (decrypted) | `0x82039734` / `0x8203973C` | `p38dual` / `coltdual` |
| E10 | `codmp_xenonf.xex` | `0x820396B4` | `c:\cod\code\script\_mp_loadout.bro` |
| E11 | `codmp_xenonf.xex` | `0x82094578` | pointer `0x821EE270` (native table) |
| E12 | `codmp_xenonf.xex` | `0x8205FCD0` / `0x82074DA4` | `axis` / `pistol` |
| E13 | `codmp_xenonf.xex` | `0x82081974` / `0x820819C8` | `WEAPON_COLT45_ALLCAPS` / `WEAPON_P38_ALLCAPS` |
| E14 | `mp/global.cod` | `0x1203dc`, `0x1204bc`, `0x120548` | `Beretta M1934`, `Luger`, `MAS1935 Pistol` |
| E15 | `mp/global.cod` | `0x120464`, `0x1205f4` | `Colt .45`, `P-38` |
| E16 | `mp/global.cod` | `0x120aac`, `0x120ac8`, `0x120aec` | `Colt .45 Dual`, `MAS1935 Dual Pistols`, `Walther P-38 Dual` |
| E17 | `sp/global.cod` | `0x7b9a0c`, `0x7b9aec`, `0x7b9b78` | same names, campaign copy |
| E18 | `sp/global.cod` | `0x7ba0f8`, `0x7ba114`, `0x7ba138` | same dual names, campaign copy |
| E19 | `sp/global.cod` | `0x17e33` | `hud_colt45.tga` (no campaign texture) |
| E20 | `sp/frontend.cod` | `0x35f3f48` / `0x35f4f80` | armory `COLT M1911` / `WALTHER P38` |
| E21 | `sp/global.cod` | `0x7b7788` | veteran interview subtitle (P38 souvenir) |
| E22 | `default.xex` (decrypted) | `0x82073990` | engine weapon-slot list incl. `"pistol"` |
| E23 | `mp/global.cod` | `0x120a24` | `Maquis Fighter` (shared text bank) |

Working copies used: read-only copies of the archives and executables in the
assistant's workspace. Decrypted images are kept only there, never in the game
folder or the repo, and never shared.
