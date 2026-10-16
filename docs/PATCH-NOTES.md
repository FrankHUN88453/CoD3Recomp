# CoD3 Recomp: patch notes since 0.4.0

**Current source/installed version: 0.5.183.** See the newest entry below.

Historical versioning note: 0.5.0 was the build of October 11, 2026, evening.
From now on, every rebuild numbers itself one higher: 0.5.1, 0.5.2 and so on.
The number shows in the window's title, at the top of CoD3MP.log, in the F11
menu's title bar, and in VERSION.txt in the game folder.

The numbers between 0.4.0 and 0.5.0 below are given after the fact, grouping
the work by what it added. Only 0.4.0 and 0.5.0 were real builds with those
names.

---

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
- Includes120 native MP controller1–15 and volume/sensitivity persistence.
  Native labels/live feel/relaunch still unverified; bot visibility next priority.
## 0.5.120: Native MP controller levels1–15 and remembered Options

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
