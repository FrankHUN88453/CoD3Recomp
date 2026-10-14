# CoD3 Recomp: patch notes since 0.4.0

**Where we are: version 0.5.0** (the build of October 11, 2026, evening).
From now on, every rebuild numbers itself one higher: 0.5.1, 0.5.2 and so on.
The number shows in the window's title, at the top of CoD3MP.log, in the F11
menu's title bar, and in VERSION.txt in the game folder.

The numbers between 0.4.0 and 0.5.0 below are given after the fact, grouping
the work by what it added. Only 0.4.0 and 0.5.0 were real builds with those
names.

---

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
  - The box now works: d-pad down cycles UAV → Artillery → Typhoon → UAV, and the UAV call works ("UAV ONLINE", 30 seconds).
  - The artillery call brings the spotter's binoculars up and the strike stays ready (no longer spent early). But they still show 0 rounds and won't fire: the script's clip call doesn't take for binoculars.
  - Now, while a strike is armed, one round is written into his clip and his ammunition directly, as the launcher's rockets are.
- **The F11 box at the top left:** that's "Renderer statistics" in F11 →
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
