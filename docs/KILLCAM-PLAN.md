# Killcam for Call of Duty 3 (MP) — design draft

Draft 1, 2026-10-20 (Justin + Claude). Nothing built yet.

## What Justin wants

After you are killed, a few seconds replay from the killer's eyes: you see
him aim, fire and drop you, then you respawn as normal. The same as Call of
Duty 4 and later.

## How CoD4 did it (for reference)

The server keeps the last few seconds of snapshots. On a death, the client
is sent "killcam mode": the camera follows the killer's recorded view, the
world is drawn from those snapshots, then the client goes back to the
spawn flow. Our case differs: System Link here is peer to peer (each
machine runs its own server), so each machine must record and play its own
copy, as the UAV plane and the mortar already do.

## Plan, in stages

### Stage 1: record (every machine, always on)

- A ring buffer of the last 6 seconds, sampled each server frame (20 Hz):
  for every client slot (32): origin (state +0..+8), view angles (+204
  pitch, +208 yaw), stance, weapon in hand (+164), firing (shot events from
  the existing sub_82554BA0 / G_Damage hooks), alive or dead, health.
- Plus the projectiles and effects already tracked by our own code (the
  mortar rounds, plane), and the game's own grenades/rockets if cheap.
- Size: 32 players x 120 samples x ~48 bytes = ~180 KB. Cheap.
- The killer: the G_Damage hook already sees attacker and victim at death
  (BotsLearnKill, the killstreak count): store "this machine's player was
  killed by slot N at time T, with weapon W, means of death M".

### Stage 2: the camera

- Find the client view set-up (the refdef: view origin + angles the
  renderer uses each frame; CG_CalcViewValues equivalent). Hook it: while
  the killcam plays, the view origin/angles come from the killer's recorded
  samples (interpolated between 20 Hz samples to the frame rate).
- Hide this machine's own first-person weapon; show the killer's weapon as
  first person if possible, else a slightly-behind third-person shoulder
  view first (much easier: no view model swap) and refine later.
- Input locked during playback (Input::SetMortar style lock); Start/A skip.

### Stage 3: the world during playback

- Version A (first): the live world as it is now, only the camera replays.
  Other players are where they are now, not where they were. Quick to build,
  shows the killer's view of the moment, but bodies/players are "live".
- Version B (proper): every player entity drawn at its recorded position
  and angles while the replay runs (client-side entity override: the
  client's interpolated entity origin, cent-lerp equivalent), and the
  shots' tracers/flashes replayed from the recorded events. This is the
  real CoD4 effect.

### Stage 4: flow and HUD

- On death: a short pause (as now), then "KILLCAM" text, killer's name,
  his weapon and distance, a 4-5 second replay ending on the killing shot,
  then back to the respawn/menu flow. The respawn timer must not run out
  during it (or the replay ends when it does).
- Lobby rules setting: KILLCAM ON/OFF (lobby_rules.cpp page, sent to the
  joiner like the other rules).
- Final killcam (end of round) later, from the same recorder.

### Two PCs

- Each machine records its own world (both have every player, via the
  game's own network), so each plays its own killcam locally. Nothing new
  has to be sent, except possibly the killer/weapon for a joiner if his
  machine does not see the kill itself (the host's G_Damage does); the
  C3K? side channel can carry "you were killed by N with W at T".

## Unknowns to find first (in the xex)

1. The client's view set-up function and where the final view origin and
   angles are written (to hook for the camera).
2. How the client hides/shows the first-person weapon and the HUD.
3. The client-side entity interpolation (for Version B).
4. The death/respawn flow timing (when the player can respawn), to fit the
   replay in.

## Order of work

1. Finish the campaign mortar (mortar-only archive), then
2. killcam stage 1 (recorder) + stage 2 (camera, third-person) as Version A,
3. weapon limit (owned-weapon mask),
4. killcam Version B (replayed world), lobby setting, final killcam.
