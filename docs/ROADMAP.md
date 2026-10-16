# Road to 1.0

| Phase | Main objective |
|---|---|
| 0.5.x — Restoration | Finish campaign weapons, HUD icons, gunshot sounds, weapon pickups, and DLC compatibility |
| 0.6.x — Stability | Consistent frame times, memory validation, stress tests, three or more players |
| 0.7.x — Multiplayer | Improved bots, networking, game modes, and lobby reliability |
| 0.8.x — PC experience | Server browser, graphics settings, controller polish, launcher |
| 0.9.x — Release candidate | Comprehensive testing, bug fixes, documentation, distribution planning |
| 1.0 — Public release goal | Stable, reproducible build with a legally reviewed distribution approach |

Each build keeps a version number and an entry in PATCH-NOTES.md.

## 0.5.x — Restoration (current: 0.5.57)

Done:
- Campaign guns in MP: Lee-Enfield, Bren, Sten, FG42, FG42 Scoped, Mills bomb
- Pickups, death drops and two-primary swaps
- Campaign gunshot sounds, muzzle flashes and HUD/pickup icons
- FG42 Scoped name and scoped ground model
- ADS while jumping, stamina recovery, lunge range trim
- Friend update package with the DLC tool
- Two-player System Link check: weapons, Mills bomb and gunshots stay in sync between both players
- MP console works for both players (devmap, noclip, ...)

Waiting on a test:
- Mills bomb in-hand model (0.5.56)
- Lunge range 5% shorter (0.5.56)
- Picking up the other player's dropped weapon online

Still to do:
- FG42 Scoped: scope on the in-hand model
- Sten reload timing against the campaign sound
- G43 (Forest archive)
- DLC compatibility check with the restored weapons
- Mounted MG42 placement on MP maps (needs the placement system below)
- Vehicle placement system and Kangaroo APC restoration
- Killstreaks, earned by kills in a row without dying, called in with the binoculars (a ladder like Call of Duty 4's):
  - 3 kills: UAV, every enemy shown on the compass for 30 seconds (the scout's spotting, for the whole team)
  - 5 kills: artillery barrage, twice as long as the scout's
  - 7 kills: Typhoon strafing run, the campaign's Typhoon (v_Typhoon in kangaroo.cod) rocketing the spot marked with the binoculars
  - The kill counts adjustable in the F11 menu
- Binoculars for every class on d-pad up, held with the right bumper (0.5.58, to test)
- Campaign: binoculars no longer pop up while running (0.5.58, to test)
- Panzerfaust (on hold)

Exit criteria: every restored weapon has its model, icon, sound and pickup working in a two-player System Link match, with DLC maps loaded.

## 0.6.x — Stability

- Frame-time capture during long matches
- Memory checks with every campaign archive loaded (weapons, Kangaroo)
- Long-session and map-rotation stress tests
- Three or more players over System Link / Tailscale

## 0.7.x — Multiplayer

- Better bots
- Networking and lobby reliability
- Game modes, including unused ones from the content audit (Last Man Standing)
- Third-person switch
- Cheat commands (noclip, god, give) host-only or behind a lobby rule, so a public lobby can't use them

## 0.8.x — PC experience

- Server browser
- Graphics settings
- Controller polish
- Launcher
- Texture replacement: dump the game's textures by name, load replacements from a folder (menus, fonts, HUD)
- Our own clean button prompts (Xbox-style and keyboard/mouse), drawn by us
- Prompts that follow the device in use, from the first screen:
  - At launch, the connected controller sets the prompts (keyboard and mouse when none is plugged in), so the menus show the right buttons straight away
  - While playing, the device used last decides: mouse or keys show key names from the player's bindings, the pad shows its buttons
  - The kind of controller told apart by its maker ID: Xbox, PlayStation, Steam Controller / Steam Deck, Switch Pro
  - One high-resolution icon sheet per style, swapped in by the texture replacer
  - Input moved from XInput to SDL, so PlayStation, Switch and Steam controllers are read directly, with no Steam or DS4Windows in between
- Optional local upscale pass for menu art and fonts, run on the player's own files

## 0.9.x — Release candidate

- Full test pass and bug fixing
- Player and build documentation
- Distribution planning

## 1.0 — Public release

- Stable, reproducible build
- Distribution reviewed by a lawyer: the release ships only the recompiled code and tools, and players supply their own game files. No game assets go out with it.

## After 1.0 — Stretch goal: campaign co-op

A second player joining the campaign. The campaign program has no
networking; the multiplayer program has networking but not the campaign's
enemy AI or mission scripts. So the work is either running campaign levels
inside multiplayer, or adding networking to the campaign. Months of work,
with no guarantee.

First step, a feasibility test: can the multiplayer program load one
campaign level with its enemies alive? That test decides whether co-op goes
ahead.
