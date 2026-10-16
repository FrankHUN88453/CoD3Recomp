# CoD3 Recomp: handoff for the next assistant

Last updated **October 16, 2026**. Installed/source **0.5.183**.
Read HANDOFF first, then latest PATCH-NOTES. Never upload game files.

## GitHub source checkpoint � October 16, 2026

User authorized updating https://github.com/hovg-N8/CoD3Recomp through183.
Source ZIP folder has no .git. Dedicated checkout for future pushes:
C:/Users/hovgt/Documents/Codex/2026-10-14/i-m-working-on-a-pc/work/github-CoD3Recomp
Base remote commit a0dca2e (0.5.86), branch main. Source/docs update prepared;
push verification recorded in local work/github-publication-0.5.183.txt.
Only handwritten host source, supporting scripts, portable helper tests and
docs included. No game files, generated game code, EXE/PDB, raw logs, textures,
archives or dumps. Preserve fork README credits and expanded .gitignore.
Build/match test status unchanged by publication; see VALIDATION-0.5.183.md.

## CURRENT CHECKPOINT - this supersedes historical build/test claims below

### 183 INSTALLED - stable balanced bot teams; live test pending

User explicitly chose balance across both teams. Source now uses per-slot
team cache, assigned on new bot creation from smaller team counts including
connected humans; tie opposite host. All six BotSide call sites now supply
slot; setup/session correction/Think/death respawn preserve that choice.
No live mid-match auto-shuffling on human join/leave/team change. Cache
cleared when a bot slot is newly created. Removed old bot_buttons0x4000
team override from BotSide; do not use that bit to change teams now.
Tests and both builds PASS; death/render hooks byte-identical182. Logs
archived. User closed182; no game process verified. All4EXE/PDB installed
and SHA256matched; VERSION183 and port patch notes updated. Startup183
and live match test pending.
Next live test: bot_add4 should populate both teams and survive respawns;
paired friend on each team must independently confirm body/gun visibility.
Remote visibility unresolved; this is assignment fix, not proven net fix.
Weapon recovery182 remains visually unconfirmed. Teleport unchanged.

### 182 - owned bot weapon recovery; live test pending

181 WAS installed/running: portVERSION181, sourceheader181, runtimeheader181.
Historical181 "notyetbuilt" heading below is stale. Original181buildlog notfound;
do not invent its test/build results. USER NOW confirms setting/returning works
perfectly. Current181controls: L3hold1sec save, Xhold2sec return; retain them.
Friendpackage request cancelled: userupdatedit. No package/archive created.

Bot weapons now USER UNCERTAIN (maybeback/forgets). Do not mark visiblefixed.
Concrete latest181log evidence supports one selection correction: emptyheld0,
pending0, commandweapon17/18/29 but ownedbits00801300 (8,9,12,23); kit12/8.
lastHeldcache persisted acrosskit changes and was reused withoutownershipcheck.
182 recovers onlyempty/nopending bot: rememberedifowned, else ownedmain/pistol,
else0. No directpsweapon/model writes. Other ammo-selection anddeath/watch/event
code unchanged (sourcecheckPASS). Teleport input/mp_fixes notedited182.
Regressiontests actualhelper PASS; observedlogcases included. Evidence
work/bot-stale-weapon-evidence-181.json +CoD3MP-0.5.181-before-weapon-fix.log.
8280 is repeatedloggedobservations, NOT distinctbots. Livecorrectrequest,
heldgun/rigpart anddroppedmodel stillneedverification after182install.

NEW MULTIPLAYER BUG: hostaddedbots tofriendteam; friendcouldn'tseethem until
switchingtohostteam. User says couldsee friend/bot scores whenthey killedhim.
This confirms somescoring, NOT friendroster/bodyreplication. Friendremote
same-team botbodyvisibility isseparateissue; don'tconfusewithmissingguns.
Needpairedclienttest; no fixclaim. Originalcombatcrash andunwantedmovement,
UAV, artillery unresolved. Latest181log had manykills over2055seconds withno
capturedfatal, but that doesNOT establish crashfixed. Pointerwatcharmed;
no clearingwriterreport found. Retain diagnostic hooks pendingrepro.

182MP/SP builds PASS; installed with no processrunning. All4artifacts
SHA256match; VERSION182/portnotesupdated, build/testlogsarchived.
Startup182 PASS: visible multiplayer menu at144FPS; left open for testing.
Nextlive testspawn+respawnbots, inspect
ownedcommand8/12 vs heldweapon and secondrigpart, drops. Friendteamtestlater.

### Next task: bots on either team and remote visibility

User wants bots visible to both humans and able to play alongside either
human on either team. BotSide(base), bots.cpp currently returns opposite
host team globally. All setup, respawn, SessionBots and Think correction
reuse it, so a bot cannot retain an independent team assignment. This was
confirmed source behavior; changed in183. Need per-bot team ownership
through spawn/respawn; do not repeatedly reassign based on host team.
User chose automatic balance across both teams; implemented in183. Separate remote visibility path remains unproven:
GiveSessionPlayer copies host session identity; host's sub_82400810 and
sub_823D9B50 hooks generate local body state. Must trace outbound session
state and remote consumption; no claim a team change alone fixes visibility.
183 built as a separate team-assignment patch;182 weapon live test pending.

### 181 - RETURN = HOLD X (controller) from anywhere; landing-trace fallback (NOT yet built/tested)
- Justin then asked for held X instead of R3. input.cpp (COD3_MP block
  after the pad read): back = (fromPad & PadX 0x4000), no stationary/ADS
  `ready` requirement, and X is NOT masked from the game (reload/use still
  work on the press); the R3 mask line was removed (R3 = plain melee).
  mp_fixes: notices say X; "RETURNING" only after 500 ms held (so reloads
  show nothing); the reject notice/log only for the L3 save now.
- USER on 180: R3 shows "RETURN POINT BLOCKED"; wants return to wherever he
  saved, even on odd geometry. 180 log (CoD3MP.log): many "tactical point:
  hold rejected; local state 2 pause 0 current 0" = the strict eligibility
  (alive/state3, ps+240 & 0x100000, host+900, ADS ps+172, |vel| x/y<5 z<40)
  failed while he held R3; BLOCKED = TacticalLanding (sub_82534200 box trace
  +8/-16 around the saved point, needs fraction<1, normal z>=0.6, not
  startsolid) found no floor.
- mp_fixes.cpp TacticalInsertionFrame (COD3_MP, end of file): new canReturn =
  host/ps valid, current client 0, not paused (0x82C73024), overlay closed,
  local state (0x82C7A270+0xB8)==2, health>0, state 3, not mounted
  (ps+240 & 0x100000) and not in a vehicle (host+900). The R3 2000 ms hold
  timer uses canReturn; the strict `eligible` still gates the L3 save and
  Input::SetTacticalPointInput (L3/R3 masking), so sprint/melee unaffected
  while moving (R3 may melee as the hold starts).
- If TacticalLanding fails, position (scratch+192, always set unless the
  allocation failed) gets the saved point with z+1 and sub_8255E118
  (SetOrigin, r1 = position-448, r3 host entity, r4 position) runs anyway.
  Log: "tactical point: landing trace found no floor; returning to the exact
  saved spot". Velocity zeroed after as before.
- Check: clang syntax pass with scratchpad stubs (32 stub/ImGui errors, all
  before line 2855, identical in the unmodified file; none in the change).

### 180 INSTALLED - R3return hold2sec; L3save unchanged1sec

USER confirms179 teleportworkedperfectly.180 changes ONLY R3duration2000ms
and displayedholdmessages; Savehelper accepts duration default1000ms soL3
unchanged. TestsPASS thresholds1000/1999/2000, release/cancel/no-repeat/wrap.
MP/SP buildsPASS. Existing botissues/deathcrash/watch unchanged/unresolved.
No gameprocess seen after userClosedreply. All4artifacts installed, SHA256match, VERSION180
+portnotes updated. Startup180 PASS: visible multiplayer menu leftopen. Next liveR3duration test.179confirmedruntime archived.

LATEST178 log update while user still playing: saved2070.7,-24.1,-48.5,
three returned lines (29570/31878/31929 in live log). Thus178 native path DID
activate later; initial no-success snapshot was earlier. Userperception not
confirmed; do not claim original neverworked. Laterruntime archived separately.
179 is requestedR3hold+feedback/eligibility change; actual original blockerunknown.

### 179 INSTALLED - L3 save anywhere stationary / R3 hold1sec

User178: cannot tell ifL3 saved, R3 no teleport; asksR3hold1sec andsavewherever
standing. No tacticalpoint success logs. Read-only178 host20704: validstate3,
health100, pause0, current0, ADS0, velocity0, nativeLocalState2.
179 direct finiteposition save (no save landingtrace), both L3/R3 independent
one-second hold timers. Return stillnativefullboxgroundedtrace/SetOrigin.
Render-triangle gating replaced by nativeLocalState2; allowsstationaryother
stances; toleratesgravityZ<40 while horizontalvelocity<5. Rawheld input visible
evenwhenineligible forrejectlog; notices SAVING/SAVED/RETURNING/NO POINT.
Inputroutingandtimertests PASS, bothbuilds PASS. Game20704 exited; no process verified. All4artifacts installed, SHA256match,
VERSION179 and portnotes updated; final178runtime archived. Startup179 PASS: visible MP menu leftopen. Next
then testL3notice/walkaway/R3holdreturn. Native teleport stillunverified.
LATEST botvisualreport: weapons missing in hands AND as droppedweapons.
No bot/crashfix179. Inspect both attachment and droppedentity model paths later;
do not lose this droppedweapon detail. Crash/unwantedmovement/UAV/artillery
remain unresolved. All tests/build/runtime snapshots archived in docs/logs.

### 178 INSTALLED - experimental return point (user authorized)

User clarified controls: hold L3 one second to SAVE/REPLACE position; R3 tap
while stationary returns. No automatic respawn redirection. Feature separate
from crash work;177bots.cpp/death/watch/UAV unchanged. User also reports some
bot guns still invisible177.177runtime watchtarget82A87860 + helperarmed seen;
no clearing-writer report yet in saved runtime snapshot. Other bugs unresolved.

178 touches input.cpp,input.h,mp_fixes.cpp,new tactical_point_input.h +version.
Raw PHYSICAL controller L3/R3 sampled before sprintfilter. Savehold1sec cancel
on movement/death/menus/ADS/vehicle/nonstanding. Releasing re-arms; no repeat
while held. R3return one perpress onlywithmarker+eligible. Moving L3/R3 normal
sprint/melee; scopedL3 steady aim; keyboard actions unchanged. Saveholdreplaces
marker, persists until host/map-clock change. Physicsvelocity<5 eachaxis,
sticks<8000, hostpmflags lowstancebits0. Pause82C73024/Overlay/menu render gate.
TacticalLanding uses private0x21000 stack + fullbox82534200 downtrace with
normalZ>=.6 and solid/fraction checks.8255E118 is verified nativeSetOrigin
updatesps/entity/interpolationorigin and toggles teleportbit; velocityzeroafter.
Not yet lived-tested; trace or eligibility may need correction basedlogs.

Both buildstages PASS (work/build-0.5.178-initial.log, finalbuild-0.5.178.log).
Actual extractedinput +holdhelper regression PASS; checkscope logarchived.
Userclosed177; no process at install. All4artifacts copied, SHA256match,
VERSION178 + portnotes updated.177finalruntime archived. Startup178 PASS:
visible multiplayer menu, leftopen. Next startmatch
and testL3hold notice, walkaway, standstillR3return, replace
point, ADS/sprint/melee unaffected. Retain crashwatch and capturewriterifcrash.
Save runtime before restart. Do not claim botissues or teleport livefixed.

### 177 INSTALLED - 176 FAILED extended combat; capture actual clearing writer

User: game crashed aftershooting/killing more; still felt unwanted movement.
176 firstkill passed ONLY. Then kills15 and7 credited (three total); each
82587268 BEFORE/AFTER queue82685ED0 count0 entries50113440. Afterthirdkill:
invalid calls001C0C01/00000D00, many globals82B3/82BD/82C7/82C2/82A9 now0.
176 drain: queueOBJECT00000000 countFFFFFFFF entriesFFFFFFFF. Thus it read
host guest0, NOT a corrupt real queue count. No error producer logged.
176 then wrote0 toguest0 via quarantine, an additional diagnostic bug nowfixed.
Broader corruption happens AFTER native playerdie returns, perhaps deferred
scripts/render/animation. Do not assert memset responsible from call-chain
membership alone. native_crt.cpp MP source is address-translated: build mp_src
already overrides8231E0E0, so no missingMPmemsetfix. Earlier weapon-request live
read aftercorruption had invalid ps pointers; work/weapon-requests-176.log is
INVALID EVIDENCE, do not infer weapon inventory from it.
Runtime preserved work/CoD3MP-0.5.176-third-kill-corruption.log; copieddocs/logs.

177 instruments existing watch: target82A87860 errorqueuePOINTER, beforefirstbot.
Old AddBot synchronousArmWatchpoints skipped caller(main), so main writes were
not watched. Now a CreateThread helper callsArmWatchpoints (main included),
then logs armed; closeshelperHANDLE. Existing watcher/handler reused. Runtime
proof watch actuallyfires is pending; no selftestsuccessclaimed. First watch
wins: ensure no COD3_WATCH environment or other earlier watch steals target.
Null queue guard now returns0 without touching guest0. All other gameplay incl
175stance-eventfilter unchanged. Residual movement still unresolved.
MP/SP builds PASS; actualwrapper tests PASS null-no-write, valid and oversized
counts. Installed no-gameprocess, all4hashes match, VERSION177. Startup177 PASS: visible multiplayer menu, leftopen for user.
Next launch/startmatch/addbots; verify watchtarget82A87860/helper armed log;
killbots to reproduce; capture FIRST watchpoint report/host+guest call chain.
No crashfixed claim. Keepuser informed and preserve runtime before restarting.
UAV, inconsistentweapons/weapon0, artilleryfire deferred until stability.

### Latest176 live test - ONE successful bot kill, guard NOT exercised

User completed requested kill test. Process13980 responding. Runtime archived
as build-logs/CoD3MP-0.5.176-death-test.log (snapshot). Bot8 killedbyhost means2;
82584188,8220B008,821E5028 and kill credit logged; slot8 subsequently respawned.
Queue BEFORE/AFTER both object82685ED0 count0 entries50113440. No enqueue,
nonzero drain, quarantine, or fault in captured log. This is one successful
kill WITHOUT containment intervening, NOT proof crashfixed. Waiting for user
2-3more kills and hands-off15sec prone test. No new build warranted yet.

### 176 INSTALLED - repeated death crash, error-queue diagnostic containment

175 USER: killing bot crashed. Not stable; stance gameplay success unconfirmed.
Crash work/CoD3MP-0.5.175-kill-crash.log, copied to docs/build-logs.175 PDB resolves
RVAa84002=>824309F0, callers824483C0/82448A18/82463978/8246C748, same172chain.
Fault guest13E10088 after2GB demandcommits. Log: bot7 died tohost, means2,
82584188 +8220B008 +821E5028 ran and host killcreditlogged, then runawaydrain.
175 logs ALSO prove bot165 stance events reached client0, caller8246BBF0,
and175guard intercepted them. Keep it; do not blame this narrow change for
same172crash without evidence.

CRITICAL corrected identification:824309F0 drains native ERROR MESSAGE queue,
not renderer drawlist.8210DCF8 producer caps count8, entries260bytes;
8210DD70 constructor allocates8*260. Fresh175menu read-only: global82A87860
points82685ED0, count0, buffer50113440. Need capture corruption origin.
Native evidence work/error-queue-175.txt, crash-list-users-175.txt,
player-die-native-175.txt; initial snapshot from work/read_error_queue_175.py.
176 logs producer8210DCF8 format/args/caller, before/after82587268 header,
and824309F0 count/header. count>8 is logged and reset without walking arbitrary
memory. DIAGNOSTIC CONTAINMENT ONLY: corruption source notfixed/proven.
If valid count still crashes, inspect callee/register changes in native drain.
If header changes during playerdie, inspect its weapon drop/script/local-tail;
record+2804 holds server slot4..23 and some native death tail uses it for local
arrays (check820B0D10 local-entity gate), but no speculative remap implemented.

176 MP/SP builds PASS; actual wrapper test PASS valid0..8delegate and oversized
counts quarantine. All4artifacts installed with matchingSHA256, VERSION176.
Menu-only175session closed through nativeAltF4 beforeinstall; startup176 PASS, visible menu left open for user.
Next: startmatch/addbots/killONE and leaveopen ifpossible; inspect
native error enqueue/death queue/drain/CORRUPT lines. Do not declare crashfixed
merely because quarantine avoids the late fault. Original debug unchanged,
AI active. Keep all newbuild/tests/crash logs archived and version notes current.
UAV, partial weapon models/weapon0, artilleryfire remain unresolved/queued.

### 175 INSTALLED - native bot stance events affect host input

174 user STILL forced standing. No success claim. Read-only previous evidence
and174 log preserved in work/CoD3MP-0.5.174-stance-failed.log.
Live174 isolation: added bot_sticks1,0,0,0 (manual neutral commands, AI skipped)
-> USER stays prone. Added bot_buttons1 (fire only) -> USER stays prone again.
Bots/session ticks kept active in both tests. This narrows to AI/non-fire actions.
Current port debug.txt has these temporary overrides! MUST restore saved original
work/debug-before-stance-isolation-174.txt before175 match test (otherwise a
false positive). Original contains move_watch1 and bot_bring63 only.

Native event handler8246A7D8(r3entity,r4event,r5param) dispatches event-162 switch:
cases3/4/5 => events165/166/167 =>8246AEB4/AEF4/AF34. Each only writes0/1/2
(stand/crouch/prone) to82B57804[CurrentClient], then returns. It can be reached
by session events823FEEA0 outside original bot scopes, with CurrentClient0.
175 wrapper skips ONLY those events when IsBotEntity validates source; other
events/host still native. Logs first60 event/slot/currentclient/caller. Intended
to fix input-source error, not force host stance. AI body movement unchanged.
Native evidence work/script-local-actions-174.txt and generated72.cpp switch.
Build MP/SP PASS (existing kernel format warning); source review log in work.
User closed174; all four175artifacts installed, matchingSHA256, VERSION175.
Original debug restored and verified (no bot_sticks/bot_buttons overrides).
Actual extracted wrapper regression PASS768cases; test log archived alongside
build log, native-case evidence and final174runtime log in docs/build-logs.
Startup175 PASS: visible multiplayer menu, left open for user.
Next ACTIVE-AI prone test. Live175 fix not yet confirmed.
Other issues remain: UAV markers, bot weapon0, artillery notfiring,
172 crash stability notproven. Temporary test statements above are historical;
debug overrides have now been removed.

### 174 INSTALLED - bot context and host stance isolation

173 user: forced standing after1-2sec, UAV absent, only some bot weapons visible.
Also reports artillery not firing. Log: artillery called from box, binoculars out,
then ready520B5264; no successful fire confirmed. Crash stability still unproven.
Game PID9692 was open during diagnosis/build. Preserve log before any relaunch.
Read-only evidence work/stance-live-173.log/.json, live-173-summary.txt,
marker-weapons-173.log; runtime copy CoD3MP-0.5.173-user-stance-uav-weapons.log.
Many living bots have weapon/pending0, inventory valid, rig only1part; visible
weapons IDs19 with2parts. Do not treat this solely as a shader/model failure.
Bot marker team+72 mostly0; snapshot radar0. Need native marker output/alpha
verification in active UAV. ps+1932 is NOT team; use entity+520 -> +4.

174 adds host stance latch82B57804 and CL slot0 hold fields Players+1608..1619
preservation to existing KeepFov bot-step scope, logs changes. Native82525338
reads that latch to emit stancebits;82571930 resets all4latches. This memory was
outside171's key/input restore. Normal82400810 bot player update previously ran
with host current context0: now scoped KeepFov + BotGunViewScope + g_thinkSlot.
173 supplemental selected-player filter retained. ReKitBots lacked bot context:
now BotClientScope around82206028. These are isolation changes; actual culprit
and live fix NOT yet proven. No stance clamp or forced-prone behavior added.
Death/obituary and UAV routines byte-identical173. MP/SP build PASS; static checks
PASS. work/build-0.5.174.log and check-input-context-0.5.174.log archived below.
User closed173; all four174 EXE/PDBs installed with matching SHA256; VERSION174
and port patchnotes updated. Final173 runtime log archived in docs/build-logs.
Startup smoke test PASS:174 visible at multiplayer menu; left open for user.
Next: test prone hands-off15sec, bot guns, multi-kill stability.
Then separate UAV fade/marker update, weapon selection if still0, artilleryfire.

### 173 INSTALLED � recover 172 weapon regression; isolate supplemental session ticks

LATEST USER TEST on172: killed one bot, crashed shooting second; bot weapons
missing. Kill-feed credit AND death animations confirmed by user.172 is failed;
do not describe it as a successful stance fix. Host stance/UAV still unresolved.
172 crash preserved at work/CoD3MP-0.5.172-crash.log. Matching172 PDB symbols:
RVA a83152 -> __imp__sub_824309F0; callers824483C0/82448A18/82463978/8246C748.
Fault read guest165000FC after2GB demand commits, inside a renderer list drain
(260-byte entries). Failure site known, corruption source NOT established.
Native evidence work/crash-native-172.txt. Do not hide fault with a pointer skip.

173 restores171's supplemental82403840 calls (weapon/session setup still needed)
with narrower isolation: g_supplementalBotPlayer scopes each call;82400810 skips
all other session players during that call only, so host is not replayed under
bot scratch context. Lock MoveLocked[3] explicitly before native call and restore
it after; require a private bot gun view rather than fall back to shared memory.
Retains171 key/input restore.171 death/obituary and UAV code unchanged.
Source snapshots work/bots-0.5.171-before-input-fix.cpp and
work/bots-0.5.172-before-regression-fix.cpp;173 check log and MP/SP build log in
work and docs/build-logs. Build PASS; both EXEs/PDBs installed with matching
SHA256, VERSION173. Startup smoke test PASS:173 visible at multiplayer menu. Game left open for user.
Live match test pending; crash fix NOT yet confirmed.
Next test: bot weapon models, kill several bots through respawns, host prone
without touching stance/jump. Retain confirmed death animation/kill-feed.
If crash repeats, resolve against173 PDB (172 RVAs no longer apply), preserve log,
consider renderer work-list producer / second death effects; avoid assuming
session removal caused renderer corruption merely because timing correlates.
UAV separate follow-up after stability; no new AI features until stable.

### UAV follow-up: marker fade check not covered by169 diagnostics

Native825049C8 reads entity+520 -> +72 into marker timestamp. Its tail computes
alpha from levelTime minus that timestamp; old ps+132 firing checks alone do
NOT prove visibility. Native evidence work/uav-marker-path-171.txt; concise
plan work/UAV-next-after-172.txt. Old128 host team+72 valid, bot0, but current171
must be checked because session behavior changed since then. Next separate
patch after173 stability test: inspect team+72 and actual native output alpha,
then refresh correct bot marker age while UAV active if confirmed stale.
172 UAV function byte-identical. Do not mark UAV fixed or runtime stance172
confirmed before user result. Latest user confirmed kill feed only; death
animation not separately asserted in that message.

### 171 - BOT DEATHS ROUTED THE NO-SESSION WAY (sub_823FD118) + host input guard
- STATUS FOR THE NEXT SESSION: 171 not yet tested. Open asks: bot death anim
  + kill feed (171 should fix), prone stand-up (171 guard), bazooka/scout
  artillery/vehicles/scorestreaks for bots, bunching/objective dedication
  (War + Domination), custom classes + campaign guns, lobby fill setting,
  friend update incl. textures (later), UAV (169 log: compass gate passes
  for all bots; Justin hasn't re-checked).
- HOW IT WAS FOUND (method worth reusing): 170 added DEATH_TRACE hooks
  (macro at the end of bots.cpp: PPC_FUNC wrapper printing r3-r5 named by
  WhoIs(): host/bot entity, record +512, team +520, handle index slot+1).
  Strings "PLAYER_DEATH_FLYING", "Player_Dying", "DeathState" were found by
  scanning mp.bin for death-related strings, then xref.py for users. 170's
  log: host death -> sub_82584188 (PLAYER_DEATH_FLYING, r3 victim entity,
  r4 attacker entity, r5 means 0x26) -> sub_8220B008 (script DeathState) ->
  sub_821E5028 (Player_Dying notify). Bot deaths: NONE of them.
  (sub_8258C778 is NOT the death: called every frame for every player.)
- Chain: sub_82584188 is called only by sub_82587268 = PLAYER_DIE(self r3,
  inflictor r4, attacker r5, damage r6, means r7, ...: sets health +0x308
  -> record +0x130, drops gun sub_82583310, death anim, scripts).
  sub_82587268's callers: sub_823FD118 (direct) and sub_823FBF78 (a session
  message handler, no direct callers). sub_823FD118 (called by
  sub_8258BA98, the damage code): lwz r3,0(r3); if 0 (no session) ->
  player_die(r4, r6, r6, r7, r8, r9, r10, ...) then the obituary callback
  *(0x82BCBF64)+0xce4 (entity +0x1e4 handles = kill feed); else ->
  sub_823ED658 (builds message type 0x2E, queued via sub_823D8F08 on session
  +0x5998) which is played by the victim's machine. Bots have no machine.
- 171 FIX: wrapper on sub_823FD118: if r4 is a bot entity (and bots remote)
  r3 is pointed at a zeroed 16-byte AllocatePhysical block, so the original
  takes the no-session branch on the host. Logs "lethal hit ... played here"
  and "player dies (sub_82587268): ..." (first 40-60). IF IT FAILS: check
  the log lines; if sub_823FD118 is never called for bots, the lethal
  damage path is elsewhere (trace sub_8258BA98 / sub_825862F8). If the
  death plays but the session gets confused (respawn), consider also
  sending the 0x2E message, or calling sub_823FBF78's tail.
- Host input guard (prone stand-up): sub_82404580 wrapper (167's per-bot
  sub_82403840 calls) saves kbuttons 0x82B53E20 (0x300 bytes; CL_FinishMove
  sub_82522B10 reads AND clears their wasPressed words) and the host's CL
  input 0x82C35030 (0x804) before the bot loop, restores after if changed,
  logs "a bot's session update changed the host's keys/input". If the log
  never says so and prone still breaks, look at 170's jump (+26 up move)
  or the 0x2000 stance taps instead.
- Earlier this session (168-170): 168 broke the respawn else-if chain
  (every live bot respawned every frame: 34k "respawns"), fixed in 169.
  Lesson: insert new tests as separate ifs, never inside if/else chains.
  Death state +1924 3->4 is set by the game itself (168's write unneeded).
  Menu textures: used.txt (texture_pack.cpp) showed all pre-match menu art
  replaced except 3 sepia photos (256x128_f18_17B182E0B3901241,
  67521EDF83599FBC, 2E277FD3C3C3E062) -> ESRGAN x4 (tools/esrgan.py +
  RealESRGAN_x4plus.pth in scratchpad) -> game textures\replace. Never
  GitHub. Committing: PATCH-NOTES once landed STALE (size mismatch):
  always verify sizes with device_list_dir and re-commit from a fresh dir.

### 170 - bot melee/smoke/reload/jump; death-step tracer
- 169 tested: bots fine again. Still no death anim. Log: dead bot record ==
  dead host record (move type 6, +1924 state 4, health <0): server side OK.
  168's "state 3 -> 4" never needed (game sets 4 itself).
- Button bits from CL_FinishMove (sub_82522B10): loop over kbuttons at
  0x82B53E20+0x138+0x18*n -> bit n. Handlers from the Cmd_AddCommand list
  (sub_8252C0F8): attack 0x1, smokegrenade 0x2, sprint 0x4, melee 0x10,
  activate 0x20, activatereload 0x40, class (grenade) 0x80, reload 0x400,
  leanleft 0x800, leanright 0x1000; 0x2000 stance, 0x8000 extra set by
  CL_FinishMove; 0x200 set when the local client's +0x65c (0x82C2DCF0+
  0x18d0*lc) is non-zero. Jump = command +26 (upmove) 127.
- BotExtras (after Think in FeedBots): melee when g_seeDist < 72 (Think
  sets g_seeDist = flat distance to a visible target); smoke at health<=50
  with a visible enemy <1500, 30-45 s apart; reload when clip 0 and spare
  >0; jump when forward>60 but moved <24 units in 1 s, plus random hops.
- Session player diff host vs bot at death (165 dumps): +0x50 is weapon
  anim flags (sub_823DDB08: 0x100 = weapon in slot +0x454, 0x4000 = fired
  recently), +0x54 = move anim from velocity (sub_823F2620), +0xD0 = the
  weapon. None is a death flag. sub_82400810's anim switch (+0x64 time,
  +0x54 anim via sub_823D42C8) is for vehicles/mounts.
- DEATH_TRACE hooks (first 40 calls each, args named host/bot entity/
  record/team/handle): sub_8258C778 (server death), sub_8220B008 (script
  DeathState), sub_821E5028 (Player_Dying notify, gated by a vtable+0x30c
  check), sub_821E4660 (PLAYER_DYING), sub_82463B78 (playerDeathHitGround),
  sub_82584188 (PLAYER_DEATH_FLYING). Compare a bot death with the host's.
- Not done: bazooka / scout artillery for bots (needs a weapon switch to the
  launcher, or binoculars + fire).

### 169 - fix 168's respawn-every-frame; UAV compass diagnostic; 3 menu photos
- 168 inserted the state-4 `if` between `else if (deadSince==0)` and
  `else if (now-deadSince>4000)`, so the respawn branch ran for live bots
  (deadSince 0) every frame: 34k "respawns" lines, bots flashing/falling.
  Now separate: `if (health<=0 && deadSince && now-deadSince>4000 && ...)`.
  168's state-4 change itself never ran (no "state 3 -> 4" lines) - still
  untested.
- sub_825049C8 (compass enemies): loops entity list [1..24] (slots 0-23);
  needs entity+0x208 -> +4 == enemy of host's side (table {0,2,1}) and
  record(+0x200)+0x84 != 0 and +0x84+15000 >= *(0x82C281D0+0x9c); position
  from *(entity+0x208)+0x150/0x154. UavShowsBots now logs "UAV check" each
  2 s while on.
- used.txt (168): all menu pictures >=128 before the match were replaced
  except 256x128_f18_{17B182E0B3901241,67521EDF83599FBC,2E277FD3C3C3E062}
  (sepia photos); ESRGAN x4 -> 1024x512, committed to the game's
  textures\replace. In-match NOT-REPLACED (409) are world/HUD, not menus.

### 168 - dead bots state 4; side at creation; bot roles + random classes; textures\used.txt for menu upscales
- Roles (g_role, at creation; printed "slot N is a <role>, class K"):
  objective 40% (only they take War's flag goal; replaces 165's slot%3),
  rusher 25% (sprints at >300 units; idles with long sprinting runs),
  flanker 20% (idle: long straight runs, heading changed every 6-11 s),
  camper 15% (idle: crouched, looking about; roams 8 s every ~40-60 s).
  With a target in sight all roles fight as before.
- Class: record +1920/+1922 (getters sub_820CE3D8 / sub_820CE538 -> the
  kit, sub_82206028) set to a random 0..6 per bot (was 0 for all). Custom
  classes / campaign guns for bots NOT done yet (mp_fixes' custom-class code
  applies to one's own spawn only).
- texture_pack.cpp: every texture >= 128 across, once, to textures\used.txt
  ("secs key replaced|NOT-REPLACED"): visit Create Lobby / System Link
  screens, then list NOT-REPLACED keys, stage their textures\dump PNGs,
  upscale (ESRGAN x4 in scratchpad: RealESRGAN_x4plus.pth), commit to
  textures\replace (game data stays on his PC, never GitHub).
- Death state: record +1924 (host goes 3 -> 4 shortly after death per 165's
  dumps; sub_82400810 at 0x82400ba4 tests == 3 = alive). ThinkBots' dead
  block now writes 4 for a dead bot still at 3 ("dead: state 3 -> 4");
  Respawn writes 3 back if alive and still 4. If anims still missing, next:
  find who writes 4 for the host (watch hook on writes to +1924 via the
  functions calling sub_82400810's caller), and kill feed = obituary path.
- AddBot: team+4 (entity+520 -> team object) = BotSide(base) at creation
  (was 0 -> friendly green compass arrow until the side was set).

### 167 - per-bot session local-player update (death data) + UAV shows bots
- Wrapper on sub_82404580 (session frame; r3 = Session(), f1 = dt): after
  it, for each bot with a session player: places[3] (Session+0xAEB0+0x5991
  +3) = his number (only if place 3 is empty or already his), KeepFov,
  BotGunViewScope(slot, current client 3), call sub_82403840(Session+0xAEB0,
  f1 = same dt), restore place 3 and the timer words +0x85B0/+0x861C.
  Log "the session's local player update run for each bot" (3x).
  Expect bot deaths to fill session +0x100..+0x138 -> death anim, kill feed.
- UAV: compass enemy markers (sub_825049C8) need ps+0x84 (last fired time)
  within 15000 of level time 0x82C2826C (= 0x82C281D0+0x9c). UavShowsBots
  sets every bot's ps+0x84 = level time each frame while 0x82B2CF90+12 == 1.
  (Justin: the UAV never showed bots; wants all enemies always shown while
  it runs.) Note this marks bots on the compass only while UAV is on.
- Justin's next asks (queued for 168+): bot roles (rushers, flankers
  wandering the outskirts, objective players, campers), every bot a random
  class/weapon, incl. custom classes with campaign guns.

### 166 - bot brain: prone aim, grenades at hidden targets, cover on reload
- Made while 165 was being tested (165 result unknown when made).
- Think: own eye from record+220 (was +60 fixed); target eye: valid 4..80
  (was 20..80, else 58) and height = clamp(eye-2, 6, 72) (was min 24) ->
  aims at prone/crouched heads. Grenade: also when the target was seen in
  the last 4 s, now hidden, 300-1100 units. TacticalMove cover search also
  when the in-hand clip is 0 with spare rounds (reloading), and at health
  <= 60 (was 40).
- Also: Respawn re-applies the bot's side (team+4, session +604) right after
  SpawnAtPoint (Justin: friendly green compass ping at each bot respawn;
  165's log: 48 "back to side" corrections a frame later).
- 165 RESULT (deaths): kill feed shows only host deaths. Session player
  dumps: the HOST's at death has +0x100..+0x128 floats, +0x130 0x1002,
  +0x138 0xA2000 (a death/state block); a BOT's are all 0 there. Writers of
  that block: sub_823F2E20 (called only from sub_82403840 = the session's
  per-LOCAL-player update, CurrentClient -> place +0x5991 -> session
  player; builds update/death messages), sub_823F78B0 (from sub_823F9528),
  sub_8240DEB8, sub_823CFCB8. Bots have no place -> never updated there ->
  no death message -> no death anim / kill feed. PLAN 167: after the
  session frame (sub_82404580, which calls sub_82403840 for active local
  clients), for each bot: places[3] = his number, CurrentClient = 3, call
  sub_82403840(Session()+0xAEB0, same f1), saving/restoring the session
  timer words (inner +0x85B0 float, +0x861C int) it advances, places[3],
  CurrentClient; KeepFov + BotGunViewScope around it. MoveLocked[3] = 1
  skips its input part.

### 165 - death diagnostics (session player dumps) + War: 1/3 of bots hunt
- 164 tested (20 bots, War): kill feed shows the HOST's deaths (bot killed
  host) but NOT bot deaths; still no bot death animation; most bots stood
  still (log: per-bot distinct positions 2-5 for many; they all held War's
  one flag crouched: Think's "at his flag, hold it" branch).
- History check: kill feed for bot VICTIMS was never confirmed; death anim
  confirmed only at 140 (bot held local place 1 + shared host machine
  object, place still held at death; 141 released it while dead).
- 165: at each death of host or bot, the FeedBots death watch also dumps
  his whole session player (784 bytes, at death and 300 ms on): diff a bot's
  against the host's to find what the dying player's machine sets. Also
  War goal only for slot % 3 != 0 (the rest hunt/wander).
- Next: from the dumps, set the missing death fields for bots (or find the
  local-place-only death path, e.g. in sub_82403840's place loop).

### 164 - bot's own machine object keeps the HOST's machine id (+0x34)
- 163 tested: still no death animation (freeze, vanish), no kill feed
  ("doesn't say I'm getting the kills"). Log: sub_823E9998 wrapper runs at
  each death (local branch builds a session message: type 0x18 / 0x6c
  bytes, queued via sub_823D8F08 on session+0x10000-0x5150).
- Theory: the session's death/kill messages name the machine (+0x34 id);
  the bot's machine id (host id ^ 0x5B0000NN) belongs to nobody, so the
  message is dropped. In 140-144 (death anim CONFIRMED in 140) the bot used
  the host's object itself, id included.
- 164: own object copy keeps host's +0x34 (only changed if g_botRemoteFlag).
  Now bot object == host object except being a separate allocation.
- If still missing: compare with 140 = shared holder (+4) itself; consider
  pointing the bot's session +4 at the host's holder again (bump count) now
  that commands/gun view/local index are separated.

### 163 - bot's own machine object marked LOCAL (+0x38 = 1)
- 162 tested: bot_add 20 -> lots of bots seen ("amazing"), no host bugs.
  Still NO death animation (bots freeze, then vanish at respawn); kill feed
  not checked separately (assume still missing); reloads not seen.
- Death anim/kill feed worked only with the host's shared machine object
  (137-144). Direct +0x38 checks via sub_821AE440 (callers sub_821B2BF8,
  sub_821B35E0 x2, sub_821B3900, sub_821C3318, sub_823D8C08, sub_823E4D80,
  sub_823FB298, sub_824046D8, sub_82405158, sub_82405A88) bypass the
  sub_823D9B50 hook.
- 163: GiveSessionPlayer's own object now keeps +0x38 = 1 (g_botRemoteFlag
  false; true restores 0). Own holder/object/id (+0x34) kept.
- NEXT (Justin): more bot logic (prone aim, cover, grenades), lobby fill
  setting both teams; fix kill feed if 163 doesn't.

### 162 - bots answer the session's local-place checks
- 161 tested: bots' WEAPON MODELS back. Still no death animation, no kill
  feed for their deaths.
- In 137-144 both worked while the bot held a local place (+0x5991).
  sub_823D8FF0(obj, number) = "number is one of this machine's places";
  sub_823D8FB0(obj, entry) = local index or -1. Callers of 8FF0:
  sub_823DCD88 (from sub_823C2850/823C37A8), sub_823E33B8, sub_823E57D0,
  sub_823E5978, sub_823EF7A8, sub_823F9440, sub_823F9528, sub_823FA9B8,
  sub_823FCD18, sub_82407538 (from sub_82407948/82407990/824D0C08).
- 162: wrappers: 8FF0 -> 1 for a bot's number; 8FB0 -> 3 for a bot entry
  (when it would be -1). Logs "answered as one of this machine's places",
  "given local index 3". RISK: index-3 users; if something breaks, try
  8FF0 alone.
- ALSO in 162: bot_add 20 on 161 added only 14 (tries cap 8 per request ->
  now 64) and only the first 6 got session players (SessionBots ran only
  when g_bots >= asked). Now every 30 frames any bot older than 30 frames
  without a session player gets one. Note: record +12 showed huge floats for
  some bots (159133.9 ...) - not investigated.

### 161 - bots count as local in EVERY sub_823D9B50 check
- 160 tested: 3 BOTS STABLE (no host bugs; killstreaks, artillery fine).
  But no death animation, no kill feed for their deaths, and NO WEAPON
  MODELS in their hands again. 160's sub_823E9998 wrapper did run (20 logs)
  but that function builds a damage/hit message (types 0x28 / 0x6c), not
  the death animation itself.
- All three missing things = "taken from a remote player's own console".
  The reasons bots were made remote (135-144: zoom, host full-auto guns,
  freezes) were most likely the command-ring overflow fixed in 159.
- 161: sub_823D9B50 returns 1 for any bot session player whenever bots are
  remote (own machine object kept; no local place; own command ring and gun
  view block). 140's death-flag restore (sub_82404DD0) still guards the
  freeze. If something regresses, the old narrow behaviour = remove this
  block (only g_drawingBot / g_botsLocal overrides).
- Other direct machine checks (sub_821AE440 on +0x38) callers not covered:
  sub_821B2BF8, sub_821B35E0, sub_821B3900, sub_821C3318, sub_823D8C08,
  sub_823E4D80, sub_823FB298, sub_824046D8, sub_82405158, sub_82405A88.

### 160 - remote bots' death animation (sub_823E9998 as local)
- 159 CONFIRMED by Justin: 2 bots, no gun/view/control bugs, bots move,
  shoot and kill him. Gun view pointers untouched during play. Remaining:
  NO death animation (since 145, remote bots). 3rd bot not tried yet.
- Server-side death (sub_8258C778, near the gun drop sub_82580E38) calls
  sub_823E9998(session, handle r4, handle r5, ...): resolves session
  players (sub_823E5F30) and if the first is NOT local (sub_823D9B50)
  builds a network message for his machine (alloc 0x18...), else plays it
  locally (0x823E9C68). Bots' machine = host -> never played.
- 160: wrapper on sub_823E9998 bumps g_botsLocal; the sub_823D9B50 wrapper
  returns 1 for any bot session player while g_botsLocal > 0. Log "a death
  (sub_823E9998) played with the bots as this machine's" (20x).

### 159 - ROOT CAUSE FOUND: bot commands written past the 4-entry local array
- 158's bisect: "GUN VIEW POINTERS CHANGED at FeedBots" every frame after
  bot 2 (slot 5) -> 00000000 then 7ADF0000 00000000 4270C6CF... i.e. a
  command (server time, buttons, weapon, angles, floats) written there.
- Players = 0x82C2ECF0, 6352 (0x18D0) each, ring +1688 (64 x 48), newest
  +4760 is the CLIENT'S per-LOCAL-player command array: 4 entries only.
  Entry 4 = 0x82C34E30 (the CL per-local-player input state 0x82C35030,
  0x804 each, is inside it: slot 4's ring 0x82C354C8.. covers the HOST's
  input struct -> prone standing up, "someone else has control", gun
  glitches with ONE bot). Entry 5 = 0x82C36700, ring 0x82C36D98..0x82C37998
  covers the gun view pointers 0x82C37070 -> gun gone, weapon reselected 0,
  crash. SV_GetUsercmd (sub_82530270) only bounds-checks < 24.
- 159: CommandPlayer(slot): bots (slot >= 4) get their own 6352-byte block
  (same layout); FeedBots writes there; NewestCommand reads there; the
  sub_82530270 wrapper copies a bot's newest command from his block into r4
  and returns. Log "slot N's own command ring at X".
- Many earlier band-aids (KeepFov low memory + host view object restore,
  BotGunViewScope, local client state guards, sub_8251A950/82593D50 skips,
  War view-kick skips, CheckFov) may now be unnecessary; remove one by one
  once 159 is confirmed, testing each.

### 158 - bisect: which step changes the gun view pointers 0x82C37070
- 157 tested: same (gun gone after bot 2). No "view kick ... skipped" lines
  at all (War callback theory not hit). Hardware watch on 0x82C37070 armed
  ("watching ...") but NEVER reported, though the 4 pointers went from
  829D8810.. to 00000000 x4 right at bot 2's add, then floats/garbage
  (7C190000, C2E16628...). The watch mechanism may not catch it (alias
  window? host write?) - don't rely on it.
- Sequence: "bot in slot 5 begun" -> "setup context create ... restored" ->
  "slot 5 holds weapon 0" -> "native select client 0 caller 82557DA0
  target 0" (pointers already 0 there).
- 158: GunPointersCheck(where) compares the 4 words after every step
  (SV hook: between frames, FeedBots, WatchWar, SV_Frame, lobby/mp fixes,
  SetUpBots, session players, ThinkBots, ReKitBots, UpdateBotBodies,
  AddBot; inside AddBot around SetUpSlot and BeginPlayer; every
  BotClientScope stage). Log "GUN VIEW POINTERS CHANGED at <step>".

### 157 - DEEP DIG: War script sets CurrentClient to a bot's client number
- 156 tested: crash shortly after bot 2. Host object 829D8810 NOT changed
  by bot steps, watch on its +4 never fired. KEY LOG at bot 2's add:
  "weapons: native select client 0 caller 82557DA0 target 0" (sub_82557D60
  reads 0x82C37070[cc]+0x18 pending select -> host weapon 0) and the gun
  view POINTER ARRAY 0x82C37070..7C went 00000000 x4, then floats
  (6CBE0000 / 4301371E = 129.2f): an overflow INTO the array.
- Memory map: 0x82C35030 = 4 x 0x804 per-local-client CL structs (index =
  CurrentClient; init sub_825967D8 clears 4 + a word at +0x2010 =
  0x82C37040), 0x82C37050 (0x20, sub_820B9680 / sub_8245AEB0), then the
  gun view pointers 0x82C37070. CurrentClient = 4 -> entry 4 = 0x82C37040,
  its +0x30 IS the pointer array.
- Found a game path that sets CurrentClient to ANY client number:
  sub_8244A1D0 (view kick: CurrentClient = r9 if != -1, writes cg
  0x82C7D490 + n*0x18E0 +0x1844..0x1850). Reached from engine callbacks in
  table *(0x82BCBF64): +0x400 = sub_824C9810, +0x404 = sub_824B52A0 (set up
  in sub_824D3338). Called by compiled script incl. War's (sub_82214EB8,
  sub_82212190, sub_821E4CA8, sub_821EC2F0/3A8/4D0) per player client num.
  For bots (4, 5) -> CurrentClient left at 4/5 inside SV frame, CL lists
  indexed past the end.
- 157: wrappers skip sub_8244A1D0 (r9 > 3), sub_824C9810 / sub_824B52A0
  (r4 > 3). Log "view kick ... for client N (not a local player) skipped".
  Watch back on 0x82C37070 (armed at first bot add) in case it persists.
- Also queued (Justin): lobby fill / bots on both teams as a match setting;
  prone aim, cover, grenades. War objective logic is already in (151).

### 156 - guard the host's REAL gun view object (0x829D8810, 0x400 bytes)
- 155 tested: crashed as soon as bot 2 was added. The pointer watch never
  fired: 0x82C37070 entries are real static objects 0x400 apart: 829D8810
  (host), 829D8C10, 829D9010, 829D9410 - never emptied. The "object 0"
  heals come from sub_82519178's OTHER callers (sub_820B1540, sub_82462C68,
  sub_8257E118, sub_825853A8 pass their own, null for a bot) - harmless now.
  Crash: sub_8251A848 (only from sub_8251A950) on the host's object
  829D8810: its +4 sub-object's table is garbage -> the OBJECT is wrecked.
- 156: KeepFov also snapshots/restores 0x400 bytes at 0x82C37070[0] around
  every bot step (logs "wrote the host's gun view object X, put back: +off
  old->new"); hardware watch moved to that object's +4.
- Note: 153's BotGunViewScope swaps entry [3] (normally 829D9410, a real
  unused object) for a zeroed block; consider using the real [3] object or
  copies of it if bots misbehave.

### 155 - watch the host's gun view POINTER 0x82C37070[0]
- 154 tested: guns invisible and unchangeable after bot 2. Watch on guest
  0x4 never fired. Low-memory diffs at bot spawn steps: +000 and +024 get
  0x520EBxxx, +018 toggles 0/1/FFFFFFFF (some list/struct at guest 0 used by
  a spawn path; put back by KeepFov). Right at bot 2's creation: heal with
  object 0 and saved 0.00, "client 0 in hand 0". Host also lost "in hand"
  20 s after bot 1 (10688).
- Theory: the host's entry 0x82C37070[0] is normally a real object (152's
  log once showed 0x67490000) and gets EMPTIED by bot code; then the game
  falls back on guest 0x0 (zeros) -> FOV 0, no gun.
- 155: hardware watch on 0x82C37070 armed when the first bot is added; the
  "adding a bot" line prints its value; the host-view lines print all 4
  entries. Expect "watchpoint: guest address 0x82C37070 was written ...
  now holds 0x00000000, which is not a pointer" with the writer's chain.

### 154 - find the writer of the host's gun view (hardware watch on guest 0x4)
- 153 tested: own gun view blocks allocated (slot 4 A7E20000, slot 5
  A7EA0000), 1 bot fine, 2nd bot -> host "in hand 0" + heal at once, gun
  glitchy, crash a bit later: bad calls in sub_8251A848 <- sub_82592CD0
  (host's own ClientThink_real) <- sub_82534DB0: the host's null gun-view
  object at guest 0x0 (+4 = sub-object whose table is called) wrecked.
  KeepFov still puts low memory back at our spawn steps -> bot steps write
  low memory through some OTHER null pointer, not 0x82C37070[3].
- 154 (diagnostic): KeepFov's log lists every changed word (old->new, 12);
  when the 2nd bot is added, Kernel::WatchWrite(0x4) + ArmWatchpoints():
  log "watchpoint: guest address 0x00000004 was written ... chain ...".
  (WatchWrite cannot watch address 0 itself: 0 means "nothing".)

### 153 - each bot gets his OWN gun-view memory (Justin's idea)
- Made while 152 was being tested (152 result not known when 153 was made).
- The per-local-client gun view array 0x82C37070[cc] (4 entries): host's
  [0] is 0 (his view lives at guest 0x0), the bots' shared index 3 was 0 too.
  153: BotGunViewBlock(slot) allocates 0x2000 zeroed bytes per bot;
  BotGunViewScope points [3] at it (and CurrentClient = 3 when asked) for:
  BotClientScope (create/side/kit/spawn), each ThinkBots iteration, and the
  game's own sub_82593608 / sub_82593518 for a bot's entity (which also set
  g_thinkSlot so the SV_GetUsercmd swap 3 -> slot works). [3] restored after.
  KeepFov (low-memory restore) stays as a backstop; its "put back" log
  should now go quiet. Log: "slot N's own gun view memory at X".

### 152 - guard guest 0x0-0x7FF around the game's own bot spawn/think
- 151 tested (War, ederdam, 2 bots): War goal WORKS ("flag in play is 2 at
  -1482 1782 -95", then 3 after a capture). Gun "almost better", then
  glitched, then host froze with a feel of "someone else had control".
  Log: KeepFov put back low memory 4x (only at our spawn scopes). Then
  "slots: client 0 in hand 0" (host's gun gone, command weapon 0), then bad
  calls + crash: sub_8251A848 <- sub_82593608 <- sub_82534DB0 <- SV_Frame
  (game's own call, not ours; garbage view objects, commit reads 0x93D1..).
- sub_82593608 = player spawn/think by entity (callers incl. sub_82404DD0,
  sub_824046D8, sub_824D0A40). 152 wraps it and ClientThink sub_82593518
  (handle -> entity via 0x82BD1FC0): for a bot's entity, KeepFov around it.
- If it persists: give bots their own view object (point 0x82C37070[cc]
  at a scratch block during bot work) - Justin's idea, cleaner.

### 151 - War goal for bots + guard guest 0x0-0x7FF (host's null view object)
- 150 tested (War, 2 bots): host had NO GUN again after the bots came; got
  in a vehicle and couldn't drive or get out. FOV fine (healed only once).
- Host's weapon-view object 0x82C37070[0] is NULL: its fields are guest
  0x0.. (+0x74 FOV, 0x28-0x3c floats, +0x1b8 list...). Bot code paths with
  their (also null) object write the same low memory. 151: KeepFov now
  snapshots guest 0x0-0x7FF and restores it after every bot think
  (ThinkBots iteration) and BotClientScope (create/side/kit/spawn); log
  "a bot's step wrote the host's weapon view (guest 0xNNN on): put back".
- WAR DECODED from 150's dumps (flag table hashes): F430CD43 = 0 for the ONE
  flag in play (middle at start, next along after each capture), -999
  (0xFFFFFC19) otherwise; 53377998 = holder float (-1.0 for the side that
  captured; Justin's side, side mapping still unknown); F2F5EAB4 = a piece
  entity handle (0x401A..); 54E300B7 = per-flag constant; AF35F29B = 0.
  151: WatchWar finds the flag in play, its origin from the flag handle's
  entity (+336) else the piece's, sets g_war; Think uses it as the goal when
  no Domination goal (reach 120 units). Log "war: the flag in play is N at
  x y z: the bots go there".
- Next: prone aim, cover, grenades (Justin's asks); bot death animation
  (remote); reload sounds; ADS off to the side.

### 150 - bots' first-person weapon view skipped; heal guest 0x74
- 148 tested: 2nd bot OK, host guns OK, but slight glitching in/out while
  holding guns. Justin captured War flags on 148 (no War dump yet: 149 had
  not been built). Log: "weapon view 00000000 (current client 0) put FOV to
  0 (saved -1)": sub_82519178's object (0x82C37070[cc]) is NULL, so its
  "+0x74 saved FOV" is guest address 0x74 (and sub_8251A950 writes floats at
  0x28..0x3c), shared by every null-object caller incl. bot contexts.
- 150: wrapper on sub_8251A950 skips it when CurrentClient != 0 and bots are
  remote; the 148 heal now also writes guest 0x74 when the object is 0.
- Also carries 149's War dump: test both in one War match with 2 bots.

### 149 - War flags learnt (diagnostic only, for the bots' War objective)
- 148 NOT yet tested when 149 was made (Justin building 148). Note on 147:
  both bots VISIBLE, they kill the host; after the host respawns his gun
  shows and FOV is normal, but ADS looks "off to the side".
- Justin's next asks: bots play War objectives first; then aim at a prone
  host, more cover, grenades.
- War is native (compiled script "_mp_war.bro"). WAR main 0x822190f0
  (in sub_82218BE8). sub_82218660: flags = level object (0x82A99494)
  +0x138 array of "war_flag" entity handles (count +0x140; element via
  sub_8221DB70(level+0x138, i)); level+0xb0 = count-1 at start. Per flag
  sub_82211880 sets script variables (hash-keyed): 0x07AA1578 = index,
  0xAF34F29B = 0, floats 0x53377998 etc. Script var table:
  sub_821DE680(handle) -> table (+0 count, +8 -> pairs {hash, value}).
  WAR_TouchFlag at 0x821d5ae8 / 0x82211dac; WAR_FlagUpdate in sub_82211880.
- 149: WatchWar (SV_Frame, every 2 s, only if mp_gametype == "war") logs
  each flag's whole table when it changes: "war: flag i of n ...". Also
  logs "war: game type now ...". Next: Justin plays War, captures a flag;
  diff the dumps to find owner/progress hashes, then give bots a War goal
  (reuse Domination::BotGoal-style hook in Think).

### 148 - heal the weapon-view object's saved FOV (+0x74)
- 147 tested: still no gun in host's view with 2 bots (weapon switching
  works), no zoom (CheckFov restores). Log: no "local client" lines at all
  (phantom-client theory WRONG; its guards stay, harmless), FOV still
  zeroed every frame "after the server frame" -> it is the HOST's own
  sub_82593D50 -> sub_8251A950 -> sub_82519178 putting obj+0x74 (saved FOV)
  back into 0x82BA2BC0, and obj+0x74 itself is 0: saved (0x82515bf0 /
  0x8251af28) at a moment a bot's code had left the global at 0. Then
  sub_825158b8 builds the view with FOV 0 inside the same call -> no gun.
- 148: wrapper on sub_82519178: if the call leaves the global < 1 while it
  was sane before, restore the global AND obj+0x74. Log "weapon view X
  (current client N) put the field of view to 0 ... healed".

### 147 - no phantom local client for remote bots (freeze, gun gone)
- 146 tested (bot_add 2 at once): no zoom, but host FROZE and his weapon
  disappeared. Log: "base field of view went 0.00 at after the server frame"
  (36x) - i.e. outside our code: chain sub_8251A950 <- sub_82593D50 <-
  sub_82534DB0 <- SV_Frame. sub_82593D50 uses 0x82C37070[CurrentClient]
  (local client view/weapon object); sub_82534DB0 loops the 4 local clients
  (0x82C7A270 + k*0xc88, +0xb8 == 2 means in play) setting CurrentClient=k.
  Theory: local client 3 (the bots' shared scratch index) came up "in play".
- 147: (a) BotClientScope saves/restores local client idx's +0xb8 state and
  logs "set local client N's state A -> B"; (b) KeepHostCurrent remembers
  local states while no bots and restores clients 1-3 when bots are remote
  (log "local client N's state A -> B ... put back"); (c) wrapper on
  sub_82593D50 skips it when CurrentClient is 1-3, bots remote, and no bot
  think running (log "local client N's frame skipped").
- Next after this: death animation (remote), reload sounds, prone, and
  objectives for native modes (Justin asked; needs which mode he plays).

### 146 - keep the global FOV across every bot think/setup
- 145 tested: 1 bot visible & no zoom; NO death animation now (remote path).
  2nd bot: no crash, but host's guns don't draw/pull out properly, host
  stands up when prone, and he hears (the bots') reload sounds.
- 145 log: "base field of view went 0.00 at think, slot 4" every frame once
  slot 5 exists. Writers of 0x82BA2BC0: sub_82519178 (0x82519250: global =
  obj+0x74, obj from sub_8251A950 <- ClientThink path, the weapon view
  state; a bot's +0x74 is 0) and sub_8250D0C0 area (0x8250d460). Savers:
  0x82515bf0, 0x8251af28 (obj+0x74 = global).
- 146: KeepFov RAII around each bot's ThinkBots iteration, and exact restore
  in BotClientScope's destructor (CheckFov stays as a backstop).
- OPEN (145/146): death animation with remote bots (probably the local path
  in the death/corpse code checks sub_823D9B50 or machine +0x38 too - extend
  g_drawingBot-style override to that path); host prone->stand and broken
  gun draw with 2 bots (maybe the same FOV 0 flicker; re-check after 146);
  bots' reload sounds heard as the host's own (2D sound for "local" player).

### 145 - BOTS AS REMOTE PLAYERS (scalable: no 3-bot limit)
- Justin wants any number of bots, not per-bot patches. Local places (137)
  cap at 3 (4 local clients) and every per-local list overflowed.
- 0.5.144 was built but NOT tested before this.
- Key find: sub_82400810 (session player visual update; sub_82403840 calls
  it for ALL 24 session players when CurrentClient is the first local)
  branches on sub_823D9B50(player) = "his machine object is local". Local ->
  body from his entity (0x82400c1c path); remote -> from the network copy
  (sub_823EC930), which a bot never gets -> the 135 remote bot was invisible.
- 145: g_botRemote default TRUE (own machine object copy, +0x38 = 0), and
  g_botLocal default FALSE (no local places). Wrapper on sub_82400810 sets
  g_drawingBot for a bot's session player (player+8 = slot, g_bot[slot]);
  wrapper on sub_823D9B50 returns 1 only for that player during that call.
  All remote bots use local index 3 as shared scratch current client
  (ReservePlace), SV_GetUsercmd swap still maps 3 -> slot during think.
  KeepHostCurrent now runs whenever bots exist.
- Log: "drawn as this machine's", "local index 3 (shared scratch...)".
- Watch: visible? gun? kill feed (was missing when not local, 134)? death
  freeze gone? zoom? Console bot_remote 0 / bot_local 1 brings 144 back.

### 144 - guard the base FOV float 0x82BA2BC0 (zoom with 2 bots)
- 143 tested: 2 bots (bot_add 2, both same tick) still zoom; team change +
  respawn doesn't clear it. Bots presumably still move (not reported broken).
  Log: "native FOV client 0 => 1.00" immediately after the 2 bots are
  created (before session places), 90.00 before.
- sub_82453810 (view FOV) starts from the float at 0x82BA2BC0
  (lis -0x7d46 +0x2bb8, +8), clamps to >= 1.0 (0x8206178C) -> result 1.00
  means that float is wrecked (< 1). The dvar string cg_fov still reads 90.
- 144: CheckFov() keeps the last sane value (10..170) and restores it if it
  goes bad; called around every BotClientScope (create/side/kit/spawn),
  after each bot's think, and before/after SV_Frame. Logs "the base field of
  view went X (bits ...) at <stage>, slot N" -> names the step that wrecks it.
  The FOV log line now also prints "(base X)".
- If the base stays 90 and FOV still 1.00, the cause is the ps/cg branch of
  sub_82453810 instead (cg arrays at 0x82C7D490 + CurrentClient*0x18e0).

### 143 - bot think also uses the local index as current client
- 142 tested: 2nd bot no longer crashes the game (only one bad call at its
  creation), but the host still zooms: log "native FOV client 0 => 1.00"
  from bot 2 on (90.00 with one bot).
- ThinkBots still wrote the SLOT (4, 5) into CurrentClient for each bot's
  whole think (ClientThink, firing). ClientThink (sub_82593518) passes
  CurrentClient to SV_GetUsercmd (sub_82530270, r3 = server client, <24), but
  the rest of the think path uses it as a local index (4-entry arrays).
- 143: ThinkBots writes g_placeIndex[slot] (1-3); a new wrapper on
  sub_82530270 swaps r3 back to the slot when it equals the thinking bot's
  local index (g_thinkSlot). The PmoveSingle lock byte 0x82C73024+idx is
  zeroed/restored as well as +slot.
- RISK: if bots stop moving/aiming, the usercmd swap isn't reaching
  (check sub_82593608's call, which computes the client from a pointer).

### 142 - bots use a LOCAL index (1-3) as the current client, not their slot
- 141 CONFIRMED: killing one bot no longer freezes the host.
- Adding a 2nd bot: zoom + crash again. Log: bad calls start right after
  "bot in slot 5 begun", chain sub_82513F48 <- sub_8252D590 <- sub_8252D870
  <- sub_8252E2E0 (client frame). sub_8252D590 passes 0x82C37070[CurrentClient]
  (a 4-entry per-local-client array); sub_82513F48 also indexes the byte
  array 0x82C73024 by it. The game itself only ever stores 0..3 into
  CurrentClient (0x82BCBE9C): loops over 4 local clients at 0x82C7A270+0xb8,
  stride 0xc88. BotClientScope (0.5.109) wrote the bot's SLOT (4, 5) there
  -> out-of-bounds writes; likely also the cause of the 0x4001/0x4035
  (script-handle-like) values found in wrecked vtables.
- 142: each bot reserves his own local index 1..3 at creation
  (`ReservePlace`, g_placeIndex). BotClientScope (create/side/kit/spawn)
  writes that index instead of the slot. GiveSessionPlayer uses that index
  as his local place (fixes: 2nd bot took bot 1's place 1 while it was
  emptied by 141). ReleaseDeadPlaces also gives the place back once he is
  alive again (141 emptied it again just after respawn and never restored);
  it doesn't run during Respawn (g_inSpawn).
- NOT changed: ThinkBots still writes the slot into CurrentClient around
  ClientThink (comment says SV_GetUsercmd path needs it). If 2 bots still
  crash, that is the next suspect. More than 3 bots: no index left (falls
  back to slot, unsafe).
- Log lines: "bots: slot N's local index K", "slot N takes local place K",
  "slot N is alive: back in his local place".

### 141 - bot leaves his local place while dead (host freeze, real lead)
- 0.5.140 tested: still frozen; death animation CONFIRMED visible. The 140
  hook showed `sub_82404DD0` runs at spawns, not deaths, flag was 0 -> 0
  (wrong lead, harmless; can be removed).
- Real lead in CoD3MP.log: in the frame the bot dies, "call: through a
  pointer to 0x00004035, which is not a function" with chain
  sub_823C9F00 <- sub_82404580 <- sub_82406C70 <- sub_82593D50 <- sub_82534DB0
  <- SV_Frame. sub_82404580 is the session frame; it calls sub_823C9F00 on
  *(session+0x139D0) (voice/talker-like object, vtable +0x48/+0x4c), whose
  vtable is wrecked. Then other threads hit bad pointers too (0x6FFFD478,
  0x82A98030, 0x5212BFB4, 0xC2721EE0). Memory corruption caused by the death
  of a bot holding a local place (same chain as the two-bot crash of 135).
- 141: wraps `sub_82406C70`; before it, any bot that has been alive once and
  is now dead (health <= 0 or state != 3) leaves his local place (byte 0xFF).
  Respawn now gives the place back also when it was 0xFF. Log: "bots: slot N
  is dead: leaves his local place".
- If still frozen: find who writes over *(session+0x139D0)'s first word at the
  bot's death (write watch), or give the bot no local place and fix the zoom
  another way.

### 140 - undo the "local flag" a bot's death clears (host freeze)
- Found by disassembly: the session layer's death handler `sub_82404DD0`
  (session object in r3; it calls the gun drop `sub_82580E38`) ends with
  `if (sub_823D9B50(player)) [[0x82BCCAC0]]+8 = 0` — i.e. for a player whose
  machine object is local it clears a byte. The bot shares the host's machine
  object, so his death cleared it and the host froze (no move, no pause).
- bots.cpp now wraps `sub_82404DD0`: reads that byte before, calls the
  original, and if it changed while the host (client 0) is alive (health > 0,
  state 3) and bots exist, writes it back. Logs "bots: a session death: local
  flag A -> B ..." (40x) and "bots: a bot's death cleared the host's local
  flag: put back".
- If the freeze persists, check the log line: if the flag did not change, the
  freeze is elsewhere in that handler (the `0x82BCBF64`+0xcf0 callback, or
  `sub_82593608` / `sub_823F9A78`).

### 139 - spawn the bot outside his local place
138 did NOT fix the freeze (reset fired, host still frozen after bot death).
Bot animations confirmed good on 138. 139 empties the bot's local place byte
during Respawn (his spawn goes the game's way via session and as a local
player it took the host's controls), restores it after.

### 138 - host frozen after killing the bot (current client left on bot)
137 CONFIRMED: no zoom on bot_add, bot's kills in kill feed, death anim
probably seen. NEW: host frozen after killing the bot (commands arrive,
nothing moves). 138 resets CurrentClient 0x82BCBE9C to 0 before/after each
SV_Frame when bots hold local places. If that doesn't fix it, find who sets
it on the bot's death and what the host's input path reads instead.

### 137 - bot takes a free LOCAL PLACE (session object +0x5991, 4 bytes)
Session object = (session-player list address) - 0x1010; local places at
+0x5991 (bytes: session numbers of this machine's 4 local players, >=24 none).
sub_823D8FB0(obj, entry) -> local index or -1; sub_823D8FF0 -> is local.
Bots had none, so local lookups fell back to the host (zoom/gun/crash).
137 writes the bot's session number into a free place 1..3 (bot_local 0/1).
RISK: split-screen viewports. Test: zoom, guns, 2 bots, screen not split.

### CRASH with TWO bots (0.5.135, bot_remote 0, bot_add 2)
Both got session players (1 and 2, copies of the host's), then zoom-in bug
returned and the game crashed: Guest fault read 0x37660000. Stack:
__imp__sub_8251A848+0x482 <- sub_8251A950 <- sub_82593D50 <- sub_82534DB0 <-
SV_Frame(sub_82535810). 8251xxxx is the rig/animation code (near 8251ED08).
Likely a per-local-player array (4 entries) indexed by a "local" bot beyond
its size, or two entries sharing the host's machine holder. One bot works.
Until fixed: keep to ONE bot. Investigate sub_82593D50 (what index it uses
from the session/machine object) before allowing more.
Justin asked about campaign-style AI (actors) instead of player bots: the
actor/AI system lives in the campaign exe (CoD3.exe), not CoD3MP; porting it
is a very large job. Bots stay player-based for now.

### 136 - bot_remote default OFF (135 made the bot INVISIBLE again)
A remote machine object stops sub_82400810 drawing him: that update is for
this machine's players. Keep the shared object. Open: host guns briefly
full-auto/unlimited (likely a local-player path keyed by machine/local index
taking the bot), kill feed missing, no death animation. Find those specific
paths; don't change his identity again.

### 135 - bot gets his own machine object (session +4 holder/+8 object)
134 CONFIRMED: bot reloads and switches to pistol. Justin: host guns briefly
full-auto/unlimited and hits not registering; bot kills missing from kill
feed; no death animation. Session +4 holder -> +8 machine object: +0x38
"local" byte (sub_821AE440), +0x34 machine id (sub_821AE448). Shared with
host = bot treated as a 2nd LOCAL player. 135 copies both per bot (0x100
bytes, AllocatePhysical, count 0x10000 so never freed), +0x38=0, own id.
Console bot_remote 0 reverts. RISK: remote path may expect a network peer.
Also: committing files sometimes lands a STALE copy (sizes differ). Always
copy to a fresh outputs folder before device_commit_files, and verify sizes.

### 134 - bot switches gun when out of rounds (CONFIRMED)
Death animation STILL missing on 133 (bot freezes/vanishes). Next: trace
what plays a player's death animation (server player_die vs session
update sub_82400810 for dead players; does sub_82403840 skip dead entries?).

### 133 - bot asks for last weapon: CONFIRMED (Justin: gun visible, bot killed him)
132 FAILED: kit re-given 5x, "in hand 0" each time. 133: FeedBots puts the
last weapon he held (19) into his command +8 when in hand is 0. Next if it
fails: check ownership/ammo after session respawn (ps +1040.. ammo words).
Justin also: bot "got stuck looking for me" (movement/tactics, not traced).

### 132 - bot gun (kit re-given after session respawn)
131 CONFIRMED: bot named "Bot 9". bot_setup 0 (menu kit callback) ZOOMS the
host again and gave no weapon: never use it. Real cause of weapon 0: the
session respawn spawns him after his first kit. 132 ReKitBots calls the kit
sub_82206028(&handle, class 0) for living empty-handed bots. Animation note:
sub_82400810 computes the move/anim bytes itself (sub_823F2620 from the
velocity), so movement animation is NOT a frozen copy; the death freeze
may come from weapon 0. Test 132: gun model, death animation.

### Open bot items after 130 (Justin, live)
- Bot VISIBLE automatically on 130. Kills count (log: "killstreak: client 0
  killed client 4"). But on death he freezes in his pose, then vanishes at
  respawn: no death animation. Likely cause: session player +0x260..+0x2D0
  is the networked animation state the owning machine sends each frame; the
  bot's is a frozen copy of the host's. sub_82400810 reads it (see field
  list +0x44..+0x2d0). Bot brain must write these fields (stance, move,
  death) each frame, or a native writer must run for him.
- Gun model invisible: bot weapon slot 0 (log "bots: bot 4 weapon 4005 0 0"),
  though he fires and damages. Trace how a real player's weapon is equipped
  (third-person weapon model attach) and do it for bots.
- Justin wants: switch to pistol when primary is empty, reload.

### 131 - bot names
130 CONFIRMED: auto session player given ("slot 4 given session player 1").
Bot showed host's name: session player name is text at +104 (32 bytes).
131 writes "Bot N" (random unused 1..24). If scoreboard still shows host
name, also check client userinfo/name in svs client (not yet traced).

### 130 BUILT - BOT VISIBLE (session player), Claude session Oct 15

Justin confirmed 129 launcher return and mouse-wheel cycling WORK.
The entity+532 "empty render record" lead is WRONG: it is the local
first-person weapon scene record (sub_82468D58, local client only). Do not
chase it further. The real gate: session players (session +48832, 24 x 784;
+0 number, +4 counted object, +8 slot). sub_82403840 calls sub_82400810
(full player visual update) only for filled entries; host bots had none.
Live proof on 129: debug.txt `bot_session 2` + `bot_bring` -> Justin SAW the
bot, it fought, took damage, died, respawned, no crash. Gun still invisible
(weapon 0). 130 gives bots session players automatically after bot_add
(Session()!=0 only) and bumps the shared +4 object's count. NEXT: test 130
(auto visibility, map restart, scoreboard name), then the invisible gun
(bot weapon 0 / equipment), then radar markers (may now work via session).

### 129 INSTALLED - final checkpoint before usage cap

User confirms128 Y cycling WORKS again; log19->15->19 and actual held follows.
Do not reopen as unresolved without new report. Wheel shares bridge but user
specifically confirmed controller Y; independent live wheel check pending.
User reports pistol->launcher->away selects primary; old code hardcoded
ownPrimary.129 saves actual held weapon name/id, uses actual launcher-in-hand
instead of stale toggle, returns to saved weapon if still owned; fallback
primary and reset saved return on spawn. Only mp_fixes.cpp behavior changed.
Actual-source return test PASS (test-launcher-return-0.5.129.log); both MP/SP build PASS (build-0.5.129.log).128 closed via native Alt-F4; no game process during install. Both EXEs/PDBs installed, executable hashes match, VERSION129. Game left CLOSED; no background test instance. Native user settings preserved; debug.txt reset to move_watch1 while closed. Pistol->launcher->pistol live check pending. User added bot again; read-only
work/live-bot-state-128-baseline.json captured and bot_bring1 requested. Bot
visibility NOT fixed; next trace visible scene submission rather than more
animation setup. All runtime/helper files remain local.

### Invisible bot: next investigation (scene submission, no visibility fix claimed)

Read-only128 evidence: work/scene-state-128.txt and live-bot-state-128-baseline.json.
Host entity670BF1E0 rig8299D810; bot4 entity670BE120 rig8299D610. Bot model,
92 bones, private pose and animation update are present and advancing. Its
entity+532 points66F221B0 whose first112 bytes are all zero. Host+532 points
66F22540, populated with render record: +92=8299D710, +96=host entity.
CAUTION host record rig D710 differs from host main rig D810; this may be a
first-person weapon scene cache. Empty bot cache is a lead, NOT proven cause.
Do not copy host cache or blindly force render flags/registration.
8248FA50(entity) lazily allocates/cache-initializes entity+532 (first104 zero).
82446458(scene record) checks record+92 rig192 against global825FCE40 view mask,
then calls82444880(rig,entity,worldTransform,...), or82444358 for extra entity.
82446810 traverses scene-visible objects and calls82446458; alternate handles
call824456F8. Need trace whether a LIVE bot reaches scene preparation/submission,
then identify the native registration path and all gates before changing it.
Candidate factory callers8247B780,82468D58,8243E898 are saved for inspection.
Evidence/disassembly: work/body-scene-entry.txt, body-render-submit.txt,
scene-initialization.txt, scene-factory-callers.txt, body-scene-pipeline.txt,
bot-render-function-index.txt, scene-object-lifecycle.txt. The guessed reader
viewmask address8260CE40 is WRONG; native -32160/-12736 computes825FCE40.
User prioritizes making bot visible; launcher129 was the already-prepared small
fix completed before cap. Next functional bot patch must be separate. No bot
visibility/radar success claimed. Native weapon0 also remains unresolved.

Build/test logs125..129 copied into docs/build-logs for durable project records.
Runtime128 confirmation log: work/CoD3MP-0.5.128-confirmed-cycle-final.log.
Source scratch path is C:/Users/hovgt/Documents/Codex/2026-10-14/i-m-working-on-a-pc/work.
No uploads, commits, pushes, or game-file sharing performed.

### 128 installed - repeated weapon-cycle input bridge

127 user confirms STILL INVISIBLE after bring and animated updates; do NOT
claim visibility fixed. Also repeat-confirmed pistol cannot switch to primary.
Bot live127 had health<0 after user shooting (death logged); do not infer
render hiding from corpse flags620 C020 as original alive cause. Need next
trace of visible scene membership/submission, not more animation allocation.
128 only weapon input change: raw Y/wheel rising edges in input.cpp latch;
MpFixesWeaponCycle drains each host frame, dispatches native82457A60(1,0).
Bridge eligible only native weapon-ready/live/on-foot/local0/menus closed;
Xam normal MP report masks Y while bridge enabled, avoiding double-dispatch.
Pause/F11/dead/mounted presses drained, native menu/vehicle Y preserved;
SP unchanged reporting. Native cycle still owns selection/ammo/animation.
New header weapon_cycle_input.h plus input.h/.cpp, kernel_xam.cpp, mp_fixes.cpp.
Tests PASS: work/test-weapon-cycle-0.5.128.log (header latch and actual-source bridge guards). MP/SP builds PASS: work/build-0.5.128.log. Installed128 after native Alt-F4 closed127 and process exit verified. Both EXEs/PDBs copied, executable hashes match, VERSION128. debug.txt reset while closed; choices preserved. Preserved work/CoD3MP-0.5.127-user-invisible-and-stuck-pistol.log. Live weapon verification pending.128 launched visibly through Sky, window526094 title confirmed and native main menu rendered. F11 open/close works; automated grave taps did not open console, so leave native menu to user rather than repeated taps. No hidden test instance running. Ask user start match then test repeated Y/wheel in both directions. User subsequently confirms Y cycling fixed; see129 checkpoint above.

### 127 installed - evaluate bot animated body, live check required

126 fresh match bot4: ONLY model-update call is composition82447C60, component0.
125/126 constructor creates component afterward; NO animated update calls seen
in126 over multiple minutes. Native network-player path calls8251ED08 as part
of visual update, absent for host-only bot.127 adds UpdateBotBodies after
ThinkBots in SV_Frame hook: registered slot>=4, ps1932==2, rig owner matches,
valid model/pose/component, then native8251ED08(rig,-1). Native gates retained.
No host/session copy, no local input/camera context change; no weapon change.
126 weapon test: first Y reaches82457A60 (caller8254331C through dispatcher
82543290 -> 8247F7B0), selects15 from19, equipped becomes15. Subsequent Y edges
logged by input.cpp but NO more native-cycle calls. So per-frame slot swap is
NOT proven cause; investigate dispatch/press-release/virtual input gate next.
User says pistol now out, cannot switch back. Gameplay left running.
Computer Use: selected126 window656010, attempted Tab/Escape taps missed by
polling; user manually returned from scoreboard and reproduced Y. Do not
keep retrying automated key taps. No game files uploaded.
127 actual-source guard test PASS (work/test-bot-body-0.5.127.log), MP/SP build PASS (work/build-0.5.127.log). User authorized take over; closed126 via native Alt-F4, process exited naturally. Preserved work/CoD3MP-0.5.126-final.log. Installed both EXEs/PDBs, hashes match, VERSION127. Scripted native-console runtime test PASS: devmap mp_merv at12s, bot_add1 at45s. Model and animated-update counts advance together (428/427,536/535,638/637); no runtime crash observed through capture. Evidence work/CoD3MP-0.5.127-scripted-body-update.log. Visual visibility STILL UNVERIFIED. Shell-launched test process19152 window was not exposed to Sky despite main title; stopped only that owned test process after capturing log to avoid background music. Relaunch through Sky for visible manual match; no COD3_CMD env left set.

### 126 installed - diagnostic trace, no fix claim

125 user explicitly confirmed bot STILL INVISIBLE after bot_bring1. Animation
component exists/advances and pose buffers valid; this was only a partial fix.
Read-only evidence work/render-state-125.txt, bot-animation-live-125.txt.
Entity304..316 is a cached world/collision query, NOT proven render bounds;
82530720 -> 824940E8 updates it. Do not blindly write host values into bot.
125 user also reproduces weapon cycle failure: Y x3/wheel no change. Launcher
out/away restores primary. Logs show controller Y edges, held stays19/15.
work/weapon-cycle-stuck-125.txt confirms owned MP44/P38, ammo30+67 /7+14,
selected global82B9F310=19, held19. Continuous slot swap in MpFixesLauncher
(mp_fixes.cpp around1926) is suspect, not yet proven. Do not remove blindly.
126 adds observe-only wrappers in bots.cpp for8251ED08/8251E5F0 (bot model
update) and82457A60/82485D28 (native local cycle/selection); delegates exactly
once, retains return context. Both MP/SP builds PASS; 126 installed after user confirmed CLOSED125. EXE hashes match staged build; both PDBs copied; VERSION126 verified. Live trace pending.
Prepared build work/build_126.cmd and log work/build-0.5.126.log.
Next live test: add bot, press Y and wheel, examine new 'weapons:' / model
update logs. If native cycle never runs, inspect command binding path; if
select returns target but held unchanged, inspect usercmd/movement path.
User confirmed CLOSED; no game processes present during126 installation. debug.txt reset to move_watch1 while closed to avoid automatic re-teleport on next launch. Preserved125 log at work/CoD3MP-0.5.125-invisible-and-weapon-cycle.log. Preserve logs
before next launch and ask close only after staged build ready.

### 125 live test - animation construction verified, visibility pending

User added bot in fresh125 match. Read-only inspection confirms bot4 rig8299D610
component82700310 (host826FE8D0), registration flags61; constructor succeeded.
Evidence work/bot-animation-live-125.txt and live-bot-state-125-animation-created.json.
Bot alive/moving but ps164 weapon0, entity304 bounds0 remain. User confirmed it remains INVISIBLE after teleport. debug.txt bot_bring1 requested to bring bot beside host;
visual visibility and radar still unverified. Do not claim invisible bot fixed.

### 125 active bot work - missing animation component proven

User wants bots now, acknowledges Kangaroo/controller/volume fixed. Focus
visibility then radar before expanded AI, one change/build. 125 now installed.
User freshmatch bot_add1; read-only inspector work/read_live_bots.py validates
Guest base, reads only safe ReadProcessMemory. Snapshot work/live-bot-state-124.txt
and work/live-bot-state.json. Bot4entity677B6A00 ps82BF5090, definition
c_mp_GE_ass_Lod2, rig8299D610, generic_human695F8DA0,1model92bones. Hostrig
8299D810 has2models96bones. Bot weapon0 andps132recentactivity0.
Native animation component: host rig+32=826FE810; bot rig+32=0. Both source
model parts+8->+56 animresource65F3D600 type82028C78, matching native required
signature at825C3570. Evidence work/bot-animation-before-125.txt.
Native8243F138(rig,0) constructs animationcomponent rig+32, validatesresource,
private allocation, schedules task8250F1C0 (setsentity624 bit0x40). Host624=61,
bot21. Normal networksetup near82400D40 already calls8243F138.125 adds this
native step after bot spawn finalizer, guarded rig exists/component absent.
bot_setup bit4 disables only newstep forA/B; default1enabled,5old. No radarfix.
Actual-source guard harness PASSED (work/test-bot-animation-0.5.125.log). Both MP and SP built successfully (work/build-0.5.125.log). User confirmed CLOSED124; no game process present at installation. Installed both EXEs/PDBs and VERSION125. Live native component creation and visible character are still unverified; do not claim fixed.
Native level base CORRECTION: lis-32061 plus-32304 =82C281D0, NOT82BB81D0
(oldLogWhere incorrectlyprints game0). Real level+148 maxclients24, timevalid;
world loop DOES processbot (serverstate1 samehost,connected+1880=2both).
Do not falselyfix serverclientstate. No code correction to oldLogWhere yet.
An attempted autonomous testlaunch was blocked by existing game guard; NO
extra test process launched. User subsequently closed the game for installation.

### USER CONFIRMED124 - sensitivity reset fixed

Justin answered the lower/raise/apply test "Yes, same speed again" and explicitly
"yes it is fixed".123 D-pad menu fix also user-confirmed. Do not re-open these
as unresolved regressions without a new report.
Live124 log confirms horizontal100->90->100 and vertical90->100 cycles. User
subsequently chose90/90 and applied; log saved controller horizontal90 vertical90
volume50%, pad look then consumed90/90/audio25. Actual port INI now has
native_volume50,controller_horizontal90,controller_vertical90. Preserve this
user choice; do not force200 on next startup.200 is the maximum,100 the default.
Snapshot:work/CoD3MP-0.5.124-confirmed-sensitivity-fix.log.
Saving to disk is VERIFIED; full close/relaunch restoration still not tested.
No new build needed for this confirmation;124 remains installed. No game process
stopped or settings changed during record update. Invisible bots/radar next
priority before expanded AI; weapon switching and window drag live test remain
open. No bot visibility fix claimed. All game files remain local.

### INSTALLED124 live checkpoint - supersedes earlier pending states

124 BOTH built/installed after user CLOSED123, no processverified; VERSION124.
Build work/build-0.5.124.log exit0. Hooks test pass plus init-before-first-draw
regression with old37/50 limits. Updated generator work/prepare_live_slider_test.py
and actual-sourcework/test_live_slider_124.cpp. Fake test initially lacked mapped
localprofile address82C8A324; added shim, retest PASS. No source test failure.
124 visible startup passed; user started match/opened actual pause Controller.
Read-only live memory PROVES actual primary sliders value100/min10/max200:
work/live-options-124-controller-open.txt. Globalprefs100/100/audio25 unchanged.
Range initialized before first render via824F6758 guarded controller vtable,
or before first step if resource assigned old limits late; setter820BDA20
changes range only for verified controller axes. Native bar value/max uses200.
ControllerInitialize maps panel+80 local index to profile via82C8A270+index*3208
+180. Preserves native up/down effects, return value; overridesstep10 andredraw.
Observed actual apply8250A4D0 saves native Capture(controller) afteroriginal.
Current async user test asks lowerhorizontal once/raise once/leave Options,
confirm same turning speed. Await logs live horizontal90/100 and savedfields;
real disk-relaunch not yet proven. Game LEFT OPEN for this test (window132710,
process new124); do not install while running. No hidden launch.
123 D-pad fix now USER CONFIRMED.123 audio did not reset in traces (25/50).
Bot visibility/radar remain unresolved; no bot change124. Next gameplay priority.
Docs/port note copies updated; all files local. No uploads/network.

### 124 active fix - actual sliders identified

123 user confirms D-pad menu bug FIXED. Sensitivity still resets. Live logs
captured nativeapply caller8250A57C H35/V47 then37/50; audio stayed25/50.
Actual apply8250A4D0 copies panel+288 preferences via824DA350 then825095D8.
Read-only ctypes ReadProcessMemory with checked return values and validated
Guest base (matching vtable825010F8) succeeded, no unsafe dereferences/helper
exe. work/read_live_options.py, work/live-options-123.txt. No process writes.
Actual controller panel vtable820716DC objects829DB010/829DCC10/etc, children
array+4 indices2/3 sliders. Slider vtable82070AC4, setter820BDA20 /getter821BEB70;
current+24,min+28,max+32. Actual maxima37(horizontal) and50(vertical).
Native up82509E00/down82509E58 step1 and redraw824E8C40 dividescurrent/max.
124 source adds guarded actual slider setter range10..200, step hooks10%,
controller apply8250A4D0 saves Capture(controller). No bot changes.
Actual hooks harness PASS observed slider layout and100->90->100,
200->190->200,independent axes,bounds,untouched other control,save200/10,
audio unchanged. work/test-live-slider-0.5.124.log, test_live_slider_124.cpp.
123 log preserved work/CoD3MP-0.5.123-live-slider-reset.log. User CLOSED123
explicitly for install.124 build underway; not installed until successful.
Need inspect real widget initialization in124 too: setter may be called before
its parent's child array is assigned; first-step init/render guard may be needed.
Prior120-122 unrelated menu hooks retained, not evidence of integration.

### FINAL 0.5.123 checkpoint - October15,2026

User requested finishing quickly due usage limit and preserving all records.
INSTALLED:0.5.123, both exe/PDBs copied; port VERSION.txt verified123.
Build work/build-0.5.123.log exit0. Initial duplicate pad-look hook link failure
preserved work/build-0.5.123-first-link-failure.log; fixed by helper call from
existing mouse_look.cpp hook, not a second override. No outstanding build job.
One functional fix: launcher D-pad handler drops presses while native pause
byte82C73024 is set, updates previous buttons before returning so held input
cannot become a deferred equipment switch. Actual handler sequence harness
PASS:work/test_launcher_pause_123.cpp and test-launcher-pause-0.5.123.log.
Native live D-pad check still needed. Startup self-test reached visible MP menu.
Game LEFT OPEN for user: window527000 titlev0.5.123. No hidden game launch.
Sky Down navigation did not reliably change selection; subsequent screenshot
showed chat overlay/user activity. Do not claim in-match slider reproduction.

SETTINGS STILL BROKEN IN LIVE USER TEST: pause->Options->Controller decrease
then increase feels much slower than startup and appears to reset audio.
User calls startup200% by feel; new123 startup trace actually shows native
H100/V100/audio25, i.e.100% and50% volume. Native yaw ceiling580deg/sec may
explain faster initial feel. Port INI still had NO native_options save fields.
Prior unit mocks and user15(150%) reply did NOT confirm actual screen integration.
No120-122controller menu/adjust logs were observed. Need identify actual live
menu callback before another fix, not assume824E7358 is reached.
123 diagnostic logs first40 native825095D8 applies prefs/caller/H/V/audio/pause;
existing8252B578 hook calls NativeOptionsTracePad on changes (first40).
Startup caller82559108 then82559434 applied100/100/audio25 to each of4profiles.
Current log snapshot work/CoD3MP-0.5.123-startup-diagnostics.log.
NEXT: user start match, open pause Options Controller, lower/raise once, return
to gameplay; read native options apply/pad look lines and saved INI. Then fix
actual menu overwrite/range path, preserving audio on controller apply. Native
widget frontend823D5608/823D5198 remains unhooked; actual path unestablished.
Do not claim sensitivity/audio persistence fixed or live slider test passed.

BOTS STILL INVISIBLE/RADAR ABSENT.122 DID create slot4 despite user finding none:
entity677B2C90 ps82BF5090 generic_human55B1C9F4 instance68CF56D0. Active AI
logged, visual/radar unresolved.121 verified bot_bring1 beside host also invisible.
Preserved work/CoD3MP-0.5.122-user-settings-reset.log and121confirmed-invisible.
No bot behavior change123. Earlier bot_session1 diff request occurred after game
closed, so no fresh diff. debug.txt restored to move_watch1. Do not use session2
(host clone) due prior crashes. Bots next gameplay priority after settings.

All files remain local; no uploads/network. Patch notes synced beside port as
PATCH-NOTES.md/.txt. Work files are in chat cwd/work; source project on D drive.

### 123 active development - settings integration regression

User tested122: pause -> Options -> Controller slider adjustment becomes much
slower, seems to reset audio; D-pad in menu pulls bazooka. Native_options INI
fields still absent.122 log has NO controller menu/adjustment or saved messages:
previous screen identification and unit mocks did not establish integration.
Do not claim native range/persistence fixed. User prior15% reply was insufficient
without actual logs/display. Need native apply caller and pad prefs change trace.
123 source: launcher D-pad handler now checks82C73024 native pause byte AFTER
updating lastButtons (no delayed edge). This is the one functional change.
Read-only bounded40 native825095D8 apply prefs/caller/values and8252B578 pad
value-change diagnostics added; existing mouse_look.cpp hook calls helper
NativeOptionsTracePad (first separate duplicate hook caused link failure,
resolved; preserved build-0.5.123-first-link-failure.log). Both executables
built/installed123 after no game processes verified; VERSION123 checked.
Actual-source launcher press-sequence test passes, log
work/test-launcher-pause-0.5.123.log; build work/build-0.5.123.log exit0.
No live123 menu trace yet. Need open actual pause Options/controller, decrease
one step and inspect new apply caller and pad look value-change traces.
Do not call settings reset fixed; do not expand slider hooks without observing
actual path. No bot code changed123.
122 latest bot DID spawn slot4/entity677B2C90/ps82BF5090, generic_human55B1C9F4,
drawable68CF56D0, active AI but not seen. Preserve
work/CoD3MP-0.5.122-user-settings-reset.log. Radar still unresolved.
Launched122 visibly for own menu test, window1442184; Sky input reported user
input detected immediately. Stopped UI input. Game later observed closed before123 installation. No live menu adjustment/test completed by agent.

### 122 development checkpoint � supersedes installed121 status below

User confirms121 native controller menu reaches15(150%). Now requests maximum
20(200%) through the same native slider. 122 changes both axes10..200, steps10,
bar normalization200->50, Settings validation through200 and free-look cap
0.580deg/ms (580deg/sec). Defaults remain100; saved150 remains150.
122 both executables BUILT successfully (cmake --build build --target CoD3MP
CoD3 exit0); no full recompile of unchanged guest code needed. Build source
version_number.h122, generated mp_src/native_options.cpp updated by build.
Actual-source tests PASS200 bounds/bar/save/callback/defaults/cap; Settings
reload200 and reject201. Logs work/build-0.5.122.log and test-*-0.5.122.log.
122 BOTH INSTALLED after verifying CoD3MP/CoD3 absent, copied exe/PDBs and
VERSION122. Reset debug.txt to original move_watch1 after game closed.
No new runtime122 test yet. Asked user launch122/match/bot_add1 and leave open;
then toggle bot_session1 for read-only host/bot state diff (NOT2, prior crashes).
121 bot_session1 was requested after it had closed: NO DIFF COLLECTED.
Preserved final failed visibility log work/CoD3MP-0.5.121-confirmed-invisible.log.
121 live bot_add1 yielded slot4 generic_human558119B4, drawable69F26D80,
spawn finalizer82211DF8; active crouch/aim/fire logs. This confirms a model
object exists, NOT visibility. User CONFIRMS still invisible after bot_bring1;
UAV/radar still no marker. Teleport confirmed log record/entity near host. Captured log at
work/CoD3MP-0.5.121-bot-test.log. No native_options saved fields observed in
port INI at this checkpoint; restart persistence still unverified.
Read live bot generic_human/drawable result next; visibility remains priority.
No live persistence/turn feel/window drag success established yet.

### INSTALLED121 final build checkpoint

121bothbuilt/installed; VERSIONverified, buildDone preservedat
work/build-0.5.121-completed.log. MPvisiblyreachedmainmenu/titlebarreachable.
Automated borderdragNOTcompleted: firstoutsidebounds, refreshedthenuserinput
wasdetected. StoppedUIinputs; doNOTclaimliveresizepassed. GameLEFTOPENforuser,
window titleCall of Duty 3 Multiplayer - recompiled v0.5.121. Askbeforeinstall
anythingwhilegameopen; userhasalreadyagreedclosingbetweenbuilds whenasked.
Actualinputcaptureharness PASSED: work/test_mouse_capture.cpp generatedfrom
actualSetHold/MouseDelta/SuspendMouseCapture withfakeWinAPI, prefix/suffix
preserved. Logs test-mouse-capture-0.5.121.log andgeometrylogpreserved.
Native MPcontrollerlevels1–15 labels/feel andmenu/relaunch stillUNVERIFIED;
asyncquestionpending asksusercheck10(100%)->15(150%) incontrollerOptions and
leavescreenopenforvisualQA. UseSkyfreshstate, don'tinterferewithuserinputs.
Continue afterreply, checklogs/screenshot andpersistedINI native_options fields.
121 retains117botdrawablecandidate. Bot visibilityisnextGAMEPLAYpriority:
userexplicitlyaskedaftersettings. No121botbehaviorchanges,noUAV/weaponfixclaim.
Windowfixwasrequestedmidsettingsworkandimplementedasseparate121build.
No gamefilesuploaded/networkused.
### CURRENT121 source/build — supersedes pending120 statements below

120 BOTH executables built/installed; preserved logwork/build-0.5.120-completed.log.
Source has testedactualnativeOptions MP1–15, labels, savecallbacks, default50%.
No120livecheck yet. Userclosed119; noCoD3MPprocess observedbefore121 build.
121 buildingseparately forwindowresizebug, includes120changes:
- Input::SuspendMouseCapture(bool) blocks MouseDelta warps/ReadKeyboardcapture,
  releasesheldclip. Calledduringmove/resize, nonclienttitle/borderbuttons,
  windowmode/resolutionchanges. Captureclick nowrequirespointerinsideclient.
- window_geometry.h pureFit usedbyWindow::KeepVisible afterrestore/dragresize;
  clampstodisplayworkarea, preservingvalidplacement/maximizedstate.
- actualprojectheadergeometrytest work/test_window_geometry.cpp/.exe passes
  offscreen corners/negative positions, oversized/tiny, unchangedvalidplace,
  leftmonitor/offsettaskbar. Logwork/test-window-geometry-0.5.121.log.
- mousecapture actualsourceharnesswork/test_mouse_capture.cpp beingchecked;
  prefix/suffixextractactualSetHold,MouseDelta,SuspendMouseCapture.
Live121startup, nativeLabels/level15feel, save/relaunch andwindowdragpending.
Noadditionalgameplaychanges. Bots nextpriority aftersettings/windowrecovery.
Do not falselyclaimbotvisible,UAVfixed,orweapon-switchfixed.
### CURRENT120 source/build — supersedes119A below

119A user confirms audio lower; sensitivity fullbar stillnotfaster afteradjust
because original UI clamps50. User opened native controller screen while inmatch;
this is824E7560 populate /824E7358 adjust /824E72C0 draw, NOT823D5608.
Screenshot shows simplebars with no numericvalues. Removed unused119diagnostics.
120 source now native_options.cpp hooks actualMP screen:10..150 steps10,
level/percent labelsmenu+112/+116, values+216/+220, bars+200/+204; preserves
native arrowanimation using originalthenoverridesvalue. Barhelper maps150->50.
Lookup label.vtable+132 uses persistent gueststrings, copycontext/stack-0x2000.
Yaw fallbackconstant82085A6C .29->.435deg/ms; verifiedONLYnative8252B578
loads21148? CORRECTION offset23148(lis-32248); actualaddress82085A6C.
Mountedexplicitcapbranch skipsfallback; ADSfactor unchanged. Feel uncalibrated.
Settings.h/.cpp add nativeVolume50,controllerHorizontal100/Vertical100,
load/validate/save INI native_options. Ready() restoresprimaryprefs once after
console save mayload, controller/audioapply hooks capturetoSettings::Set.
SP onlyconstructor defaults; extendedUI/persistence callbacks implementedMP.

Tests passedactualsource: work/test_native_settings.cpp includes Settings.cpp,
work/test-native-settings-0.5.120.log; test_native_options.cpp generated from
native_options_harness_prefix/suffix and actualmodule (includes removed), fakes
nativeoldclamp50, checksbounds/axis/bar/callback/restore/defaults/cap. Allpass.
Preserve test code/logs. Native label display, feel and menu/relaunch untested.
Userclosed119 toallow120 install.120 build pending/underway. Still119installed
untilDone. Record final result before asking user tolaunch.

NEW window bug: switchingwindowed/resizing pusheswindowcorner, can'tdragbar.
Likelycause: input.cpp SetHold calculatesg_centre/ClipCursor onlyonstatechange;
MouseDelta keepsSetCursorPos oldcentre while OS moves/resizes. NextSEPARATE
build suspend capture duringWM_ENTERSIZEMOVE/EXIT and modechanges; prevent
ReadKeyboard recapture /MouseDelta recenter while suspended, keep titlebar in
monitorworkarea on exit/windowedrestore. Window.cpp nofixyet. User canEscape
freecursor/AltEnter mode. Userrequestedbotvisibility priorityAFTERsettings;
recoverwindowbugthenbots beforeexpandedAI. Visibility stillunconfirmed.
### CURRENT 119 development A — supersedes118 checkpoint below

User clarified118 restart persistence has NOT been tested. No failure established.
Latest priority: finish native volume/controller patch, then invisible bots FIRST,
before expanded AI. Visible bot fix still unconfirmed. User closed118 forbuild.
119 development A built/installed both executables successfully; preserved log
chat work/build-0.5.119-A-completed.log. This is NOT a complete150% patch yet.
CoD3Host/native_options.cpp added to CMake, MP_NATIVE inmake_mp_host.py:
SP824D9C50 / MP82506E78 native preferences constructor hooks set horizontal68
and vertical72 to100, volume92/96/100 to25. Native50 audio is FULL volume:
application825095D8 uses float*.02. Existing F11 master70 remains unchanged.
MP823D5608 controller menu population hook logs widget arraymenu+4 elements
+8/+12, vtable methods192..208 and widget words16..112 (bounded4reports).
Asked user to launch119A, open native controller Options, leave screen there;
read portCoD3MP.log diagnostics to identify actual slider range API, thenfinish.
No119 gameplay/menu confirmation yet. Do NOT call150% implemented.
Frontend audio apply823F6DC0 writes92/96/100 then calls825095D8; audioadjust
823F6F40 clamps0..50 and appliesvolume. In-gameaudio824DB070 writesvolume.
Controller menuapply823D5198 writes68/72, in-gamecontroller824DB360 writes
frommenu+216/+220. Constructor defaults original24horizontal/32vertical.
Native padlook8252B578 readspercent*.01, ADSfactor separately+1632. Native
unrestricted yawcap constant0.29degrees/msec at82085A6C may caphighsettings;
no turncap change yet, no exactWaW/MW2 calibrationclaim. User wants100%=10,
150%=15, faster actualcontrollerturning (notjustF11orcosmeticlabels).
### Installed 0.5.118 — active checkpoint

Continue working; Justin requested records for continuity, not a stop/handoff.
Both executables built/installed as118; VERSION.txt verified. Build log ends
Done; preserved chat work/build-0.5.118-completed.log (117log also preserved).
One functional change: native Xam profile persistence in kernel_xam.cpp.
Writer now saves typed native records; reader uses verified40-byte layout
and rebases payload pointers. Bounds, corrupt-file rejection, atomic replace,
read locks released before completion callbacks; no guest pointers on disk.
Files stored under SavesRoot()/profiles/user-N.bin. profile_store.h is codec.

Tests passed (chat workspace work/):
- test_profile_store.cpp/.exe: actual codec disk reload, types1..6, binary/
  empty data, every truncation, corruption, duplicate IDs, count/size limits,
  failed decode preserving prior output. Log:test-profile-store-0.5.118.log.
- test_profile_api.cpp/.exe: actual helpers/imports extracted from kernel_xam
  between namespace CompleteOverlapped forward declaration and Controllers;
  wrapped by profile_api_harness_prefix.cpp/suffix.cpp. Native40-byte record
  layout, file reload, sizing/short buffers, pointer rebasing, user isolation,
  failed write preserving saved file pass. Log:test-profile-api-0.5.118.log.
- MP visibly reached menu; log confirms15-setting profile read. Captured log
  work/CoD3MP-0.5.118-startup.log. UI input injection unreliable; native menu
  save/relaunch UNTESTED. Alt+F4 injection did not close; stopped own test PID
  explicitly. No successful normal-close retest, no proven regression.

Next: native volume FIRST-RUN50%, sensitivity FIRST-RUN100%, slider150%.
These defaults/UI changes are NOT implemented118. Verify native fields and
slider max; do not force defaults every launch and overwrite saved choices.
Controller preferences MP82C6B730 stride6984: +68 horizontal/+72 vertical
integer percentages, +65 invertY. Controller Options read823D5608 and apply
823D5198, widget arraymenu+4; sensitivity widgets atarray+8/+12. Gameplay
native8252B578 reads percentage*0.01, separately multiplies ADSfactor+1632.
Slider max/default initializer and volume field still require verification.
F11-only draft withdrawn: masterVolume default/fallback70, original Apply;
no SetMasterVolume API. Installed INI master_volume70 restored. Backup:
work/CoD3Recomp-before-volume118.ini. Do not double attenuate inadvertently.

117 bot drawable candidate is retained in118; visibility STILL unconfirmed.
Latest user asks whether fixed: answer candidate installed, no live proof.
UAV and weapon switching remain open. Do not label them fixed.
CoD3Scan has safe --ascii/--words/--datafind modes; no arbitrary-data disasm.
### Latest user requirements (native game Options, not F11-only)

1. Native menu volume starts50% on first/default launch, then remembers the
   last user-set volume when closing/relaunching. Do not reset every launch.
2. Controller sensitivity starts100%, remembers changes, and native slider
   goes up to150%. Both axes may exist; inspect native menu before changing.
3. Continue bot visibility, UAV markers and weapon switching afterward.
   Preserve confirmed Kangaroo exits and native scoped L3 steady aim/sounds.
4. One functional change per build, notes every build, self-test where useful,
   concise status after tests; never upload any game data to GitHub.

### Verified gameplay / outstanding bugs

- Kangaroo left/right/rear exits confirmed good by Justin; ride height still
  looks4-6in high.112 private wheel radius18->13 initialized, visual result
  not confirmed. Bullet-hole flicker remains open after vehicle/bot work.
- Scope ADS/L3 hold-breath, sounds and walking while scoped confirmed good.
  Closing game window terminates process/audio (105 tested campaign and MP).
-113 user camera remained normal after bot_add; native FOV90 logged, but
  bot inactive.114 missing spawn-finalizer fixed: health100/state3/move0,
  movement and damage observed. Bot still invisible even beside host.
-115 gives valid class0 instead of-1, but still invisible and weapon0.
-116 exact ownership query allowance at821FB408 was hit, yet drawable
  entity+536 stays0. This did not solve visibility.
-117 adds native generic_human object factory after placement, before
  finalizer:824B5688(820653E4) ->8257C090(entity, entry), guard entry+4.
  Its live result is STILL NOT REPORTED. Do not claim visibility fixed.
- UAV activation/message confirmed but no bot marker on compass. Native
 825049C8 gates enemy_radar82B2CF90+12 AND ps+132 recent activity(15s).
  Other compass widgets still need review. No UAV change115-117.
- User cannot switch weapons using controller Y/Triangle OR mouse wheel.
  Respawn may equip pistol or Kar; then switching fails in either direction.
  Host inventory logs include Kar98_sniper21 and pistol15. Whether this
  happens before bot_add is unanswered.117 logs rising PadY source ininput.cpp;
  both controller/wheel sharePadY; sprint filter preserves that bit. Not fixed.
- All temporary debug.txt probes were restored to original move_watch1.
  bot_bring is available only throughdebug.txt; changing its value (including
  restoring0) triggers placement again. bot_session1 only logs differences;
 2 clones session state and has past crash risk, so do NOT use2 routinely.

### Evidence and settings research

Chat workspace root:
C:/Users/hovgt/Documents/Codex/2026-10-14/i-m-working-on-a-pc
Snapshots inwork/: CoD3MP-0.5.114-invisible-class-minus1.log,
CoD3MP-0.5.115-class0-still-invisible.log,
CoD3MP-0.5.116-invisible-after-bring.log. Current port log may be117 if run.

- kernel_xam.cpp XamUserReadProfileSettings currently returns allunset;
  Header8/Entry24 is wrong for native title consumer. Writer discards writes.
- MP wrapper8222C0C8(user,count,records,overlapped) invokes import with
  r3=0, r4=user, r5=count, r6=records, r7=overlapped. SP analogue82195E68.
- Native writer824135D8 builds ONE40-byte record atstack+96:
  +0 source2, +8 user index, +16 ID63E83FFF, +24 byte type6(binary),
  +32 byte length, +36 guest payload pointer. r4count1, r5record, r6overlap0
  at wrapper call. Verify layout and limits before implementing persistence.
- Native reader82414788 walks40-byte returned records, uses +0presence,
  +16ID, +32scalar/size, +36payload. Requested IDs15 include10040018,
 10040015,10040002,10040003,4064000F,402C0011,10040005,5004000B,
 10040004,1004000C,1004000D,1004000E,63E83FFF,63E83FFE,63E83FFD.
- Native profile state: four832-byte objects; MP base82BAC0D0
  (lis-32069/addi-16176).82414A60 allocates blob size4 forindex12,
 1000 forindices13/14, size512 otherwise. Native defaults/binary schema and
  precise sensitivity/volume fields are NOT established. Do not guess them.
- Native menu labels: volume8206BC28(FEMENU_COP_VOLUME);
  vertical8206BE1C, horizontal8206BE34; MPGAME sensitivity labels8206C3F0/
 8206C40C. String pointer tables at82061A30(volume),82061B40/44(horizontal/
  vertical), duplicates82063734 and820636B8/BC. Native global tables using
  lis-32163 are825Dxxxx, NOT8206xxxx; don't confuse these bases.
- mouse_look.cpp native pad hook SP8250FF48 -> generated MP8252B578;
  clientActive base MP82C2ECF0 stride6352, sensitivity factor+1632 is also
  modified forADS, so do NOT overwrite it as a persisted slider blindly.
- Existing settings.cpp F11 persistence works through Settings::Set->Save,
  but does not solve native menu/profile persistence. Avoid double attenuation
  by forcing both host master and native volume to50 without intention.

### Inspector incident / safe continuation

Broad CoD3Scan --disasm0x82000000 131072 caused memory-read Application Error.
Current inspection was stopped; csrss-hosted error dialog was dismissed and
verified absent via window list. Do not run that data-disassembly command
again. Use --ascii/--words fordata; --disasm only verified code/small known
instructions. Earlierbreath_lookup.exe also crashed; never relaunch it.
Do not claim an inspector crash was a game crash. No game files uploaded.

### Next action

Finish native Options persistence plus volume defaults/sensitivity150 with
verified layout and bounded writes. Test save/reload with actual code and
native menu if possible. Only then build118 and rewrite its pending notes
around final behavior/results. If implementation must be split acrossbuilds,
state exact scope honestly. Preserve117 bot factory change and open tests.

## What this is

A PC port of **Call of Duty 3 (Xbox 360)**, made by static recompilation with
XenonRecomp. It's a fork of FrankHUN88453/CoD3Recomp, now on Justin's GitHub.
There are two programs:

- **CoD3.exe**: the campaign.
- **CoD3MP.exe**: multiplayer. Most current work is here.

The recompiled PowerPC code is in `CoD3RecompLib/ppc` (campaign) and
`CoD3RecompLib/ppc_mp` (multiplayer). It's generated, so don't edit it by hand.

All hand-written code is in `CoD3Host/`. The game's functions are hooked by
defining `PPC_FUNC(sub_XXXXXXXX)` and calling the original through
`__imp__sub_XXXXXXXX`; the recompiled functions are weak aliases.
Multiplayer-only code is inside `#ifdef COD3_MP`.

**Owner:** Justin ("Native", "brudda"). He tests every build and wants a short
status after each test. He prefers the assistant to test changes itself where
it can.

## Never do this

- **Never upload game data to GitHub.** That includes the ISO, `.cod`/`.xex`
  files, generated `ppc`/`ppc_mp` sources, textures, shaders and exes. Only
  hand-written source and docs go up.
- **Never write into game structures past a bound you haven't verified.**
  The 0.5.95–0.5.97 crashes all came from one helper (0.5.93's weapon-marks
  fill). It walked 255 slots of the weapon list, but the list is shorter, so it
  wrote 466 pointers into random game memory. The crashes showed up somewhere
  different each time.

## Folders (on Justin's PC)

- **Source:** `D:\Call of Duty 3 (USA, Europe)\CoD3Recomp-main\CoD3Recomp-main`
- **Game / port folder:** `D:\Call of Duty 3 (USA, Europe)\CoD3-PC-Port-0.4.0`
  - `CoD3MP.log` is the current run's log, `CoD3MP.previous.log` the run before.
  - `CoD3.log` is the campaign's log.
  - `CoD3MP.pdb` is for symbolizing crashes.
  - `CoD3Recomp.ini` holds the settings (`texture_dump`, `watch_writes`,
    class loadouts and so on).
  - `textures\replace\` holds the upscaled textures (4x Real-ESRGAN).
  - `game\` holds the original disc files (`mp\`, `sp\<level>\*.cod`).
- **Investigation material from Justin:**
  - `kangaroo-technical-dossier\`
  - `bullet-impact-dossier\`
  - `Call-of-Duty-3-investigation.zip`: indexes and strings of every archive.
  - `CLAUDE-READ-FIRST-BULLET-HOLES.txt`

Build-tool note: this session had inaccessible cached WinGet Ninja and Python
paths. CMake caches now use VS BuildTools bundled Ninja and Codex bundled
Python; add bundled Python to PATH when running the normal build bat.

## Building and versioning

- **Build:** run `BUILD-MULTIPLAYER.bat` in the source folder. It builds both
  exes, takes about 5 minutes, and logs to `build-mp-log.txt`, which ends with
  "Done." or "STOPPED".
- **Version:** the build takes it from the **first `## X.Y.Z` heading** in
  `docs/PATCH-NOTES.md`. To make a new version, add a new entry on top.
- **Watch out:** `BUILD-MULTIPLAYER.ps1` sits next to the `.bat`, and opening
  it from Explorer's Recent list doesn't run the build. Run the `.bat`.
- **Syntax check before building (in a sandbox):**
  `clang++ -std=c++20 -fsyntax-only -fms-extensions -DCOD3_MP` with stub
  `windows.h`/`imgui.h`, plus the XenonRecomp headers (`XenonUtils` and
  `thirdparty/simde`). Errors that come only from the Windows/ImGui stubs are
  expected.

## Testing

1. Launch `CoD3MP.exe` from the port folder.
2. Main menu → System Link → Ready → Play Game → Create Match (War, Merville)
   → Auto Select → a class.
3. Menu keys: Space or Enter selects, Backspace goes back, arrows move.
   When driving the game remotely, click an item first; keys sometimes don't
   register otherwise.
4. Read `CoD3MP.log`. Every feature logs its own lines: `kangaroo:`,
   `vehicles:`, `bots:`, `marks:`, `render:`.
5. The F11 menu has settings, an FPS display and a console.

**Console commands (host code):**

- `kangaroo_tread 0-10`: how the treads are posed. 10 is the default: the
  game's own poses with the Kangaroo's wheel list corrected.
- `kangaroo_type 1|2|3`: 1 is the default (four-wheel kind, tank preset;
  drives and can be entered, but slow on hills). 3 (tracked physics flag on a
  four-wheel kind) drove broken: rocking on hills. 2 (whole tank kind) could not
  be entered at all. Both tested by Justin on 0.5.100.
- `kangaroo_build 0|1|2`: how the Kangaroo is made at map start. 1 is the
  default (a Kangaroo from spawn with the tank physics preset), 2 is the same
  but keeps the Horch's physics, 0 is the old pre-0.5.99 way.
- Bots: `bot_add`, `bot_setup N`.
- Killstreaks: `streak_give uav|artillery|typhoon`.
- Vehicles: `spawnvehicle <kind>`, `kangaroo 0/1`, `vehicles_everywhere 0/1`.

## How the Kangaroo works now (multiplayer, `mp_fixes.cpp`)

The Kangaroo APC is a campaign vehicle. Its model `v_Kangaroo` comes from
`sp\mace2\kangaroo.cod`. On Merville and Poissons it's loaded after the map's
own archive, because its tread and wheel shaders `v_tracked_tread` and
`v_tracked_wheel` live there.

**1. A kind of its own.** `MakeKangarooKind`, called from `sub_82561838`,
copies the Horch's vehicle file into a new kind, `mp_kangaroo`.
`KangarooPrepareKind` then fills it in:

- driving, bounds and health from the Sherman;
- tread scroll from the Sherman (texureScrollScale +76 = 5.5);
- `vehiclePhysics` +540 = `mp_tankdefault` (the Sherman's);
- subtype 8, curve `Kangaroo`, 8 seats.

Vehicle file field table: 0x825FE3C8. Kinds table: 0x82B2DC90; count at
0x82B34594.

**2. Made a Kangaroo as it spawns (0.5.99).** `sub_8257E3A8` (entity r3,
record r4, kind r5) sets a vehicle up: seat tags, then the physics object at
record +1304. That object is built from the kind's preset and the model's
bones and size. Our hook does this, in order:

- for each saved Kangaroo placement on the map, takes the map's Horch;
- registers its entity as a Kangaroo;
- sets the `v_Kangaroo` model;
- swaps r5 to `mp_kangaroo`;
- calls the original;
- only with `kangaroo_type 3`: sets the tracked flag 0x20 on the physics
  object (+640). Not the default: it drove broken.

Then `sub_82564520` (map start) moves it to its saved place from
`vehicles.txt`.

**3. Tag stand-ins.** `sub_82434E60` answers tags the model lacks:
`tag_enter_left/right` become the middle wheels, the hatches become driver
and gunner, and so on. This only applies to entities in `g_kangarooEntities`.

**4. Wheels.** The physics object keeps its wheel bone numbers at +36, found
by the name order at 0x825D5C6C. `FixKangarooWheels` re-checks them each
frame. When it was built from the Horch, bone 4 (`tag_tread_left`) was turned
like a wheel, which was the old spinning tread.

**5. Treads.** `sub_82568600` adds the distance driven to record +1308/+1312,
scaled by kind +76. `sub_82434918` poses the tread and sprocket bones from
those distances (the shader scrolls by the tread bone's pose).
`sub_8242B938` poses the vehicle and, with flag 0x20, calls `sub_8241E908`,
which aims the treads along the wheel points.

**Physics object layout:** preset pointer +0x250, entity +0x270 (624), flags
+0x280 (640). Presets are looked up by `sub_82418520(name)`; the object is
initialised by `sub_82420998`, and `sub_8241BB90` drives the wheel joints.

**Entity layout:** origin +336, yaw +356, mins +240, maxs +256, DObj +492.
A DObj's skeleton is reached through model +128 → +36.. → +8 (count +16,
12-byte entries from +20 with a name hash at +4; the hash is h*33 + tolower).

## Current continuation (0.5.112)

Justin's 0.5.107 log proved every passenger/rear exit reached the hook but
failed grounding. 0.5.108 corrects a definite native trace layout error:
hit position is +0, plane normal +16, so normal Z is +24, not +8. Installed
builds 0.5.109/110 include that fix, and Justin now confirms side/rear dismounts work well.

Justin added requests for fixing bot_add camera zoom, bot cover/head aim/
objective and enemy routing, and sniper L3 steady aim. Separate builds:
109 scopes bot setup to its own CurrentClient; 110 adds tactical behavior;
111 preserves MP native +sprintbreath through the tap-sprint filter. Latest
clarification: walking forward stays scoped, L3 is breath while scoped,
sprint latch cancels during ADS, and only manual aim-out leaves the scope.
Both builds for 109/110 passed. Planner and input tests passed against actual
source headers. 111 also built and installed after fixing the console handler's missing input.h include.
Justin confirms L3 hold-breath, scope behavior and native sounds work.
Camera zoom still fails on 0.5.111; context isolation was insufficient.

Driving/treads remain good; ride height and bullet flicker are still open.
Closing X in both executables was actually tested on 0.5.105: close marker
logged, process exited, console/audio stopped. No hidden game tests since
Justin reported hearing invisible test audio. Keep one feature per build.

## Open items, most wanted first

1. **Kangaroo exit placement: CONFIRMED WORKING by Justin on 0.5.111.** 0.5.106
   did not move any exit: live log used 8257AD34, outside its allowlist.
   Hook 8257ABF0 AFTER native dismount cleanup and origin placement;
   require player link +900 zero and client +240 bit 0x100000 clear.
   Entry tags from 82571A58 are remembered per player/vehicle: left/right
   return to that side, rear/turret returns behind (local negative X).
   Full player-box sweep above hull then down checks grounded clearance.
   IMPORTANT: trace +0 is hit position, +16 normal, +24 normal Z; the
   old +8 slope check rejected all Merville ground. Watch new trace values.
   Watch `kangaroo: exit: placed on left/right/rear` or failed landing.
   Console `kangaroo_exit 0` restores native placement, 1/2 fallback left/right
   when no entry tag is recorded. Preserve this working code.
   - sub_8255E118 updates authoritative player/entity origin.
   - Trace sub_82534200: result/start/mins/maxs/end/filter in r3..r8;
     flags r9/r10 and stack +84/+92; own guest scratch/stack.
   - Stable entry rotation from 0.5.104 remains. Keep build 2 (Horch
     physics), type 1, tread mode 10; ride height unchanged.
2. **Bot_add zoom.** 0.5.109 scopes create, side/kit callbacks and respawn
   to CurrentClient 82BCBE9C, like existing bot movement. RAII restores the
   original context and logs host ADS/weapon before/after. bot_camera 0/1
   rollback. Justin confirms zoom still fails on 0.5.111. Host ADS/weapon
   are unchanged around setup and cg_fov stays90. Next isolation: fresh match,
   bot_setup 3 then bot_add 1 skips both menu callbacks. Asked Justin via
   async test question; await result before another speculative fix.
3. **Tactical bots.** 0.5.110: bot_tactics 0/1 rollback, head/eye-height aim,
   LOS and pitch firing gates, reaction after visibility returns; collision
   route stepping, stuck escape, damage/low-health nearby cover and crouching.
   Pure header planner tests passed six scenarios. Live behavior untested.
   Current objective source is Domination::BotGoal; route steering is local,
   with no full-map navmesh/maze guarantee. Grenades require visibility.
4. **Scoped L3 hold breath.** 0.5.111 fixes tap-sprint erasing physical L3
   during ADS. Native MP config binds L3 +sprintbreath (breath/timing/sounds
   already exist). Walking keeps ADS; sprint latch is cancelled; no auto
   aim-out. scope_breath 0/1. Input tests pass. Campaign has different
   binocular/breath mappings and retains its old mapping. Justin confirms
   native MP steady aim/sounds/scoped walking work on 0.5.111.
5. **Kangaroo rides slightly too high.** 0.5.112 built/installed: private
   208-byte Horch physics preset (copy204 verified bytes), wheel_radius +20
   reduced from18 to13 by default. Hook82420998 changes r5 before native init.
   Only registered Kangaroos; shared presets untouched. Console kangaroo_drop
   0..8 (default5), then map_restart; 0 original. Visible runtime log confirms
   radius18->13. Actual visual/driving test pending; initial stationary origin
   still logs -105, so do not claim a measured five-unit hull drop yet.

6. **Bullet holes come in slowly and flicker (campaign and multiplayer).**
   - 0.5.94 made vertex buffers under 1 MB always hash whole, so holes appear
     much sooner.
   - 0.5.99 added a write-watch for big buffers within a frame
     (`render_resources.cpp`, `liveSince`). It never fired in testing, so the
     marks aren't rewritten inside a big buffer mid-frame.
   - Next suspects:
     - (a) The decal draws' depth offset. `render_commands.cpp` moves the
       constant poly offset (0.0001) into vertex depth, and the Z pass is
       pushed "behind". Check whether marks z-fight when the camera moves.
     - (b) Small buffers (≤256 KB) are only sample-checked (64 words) in later
       draws of a frame.
     - (c) The index buffer path, which is page-watched once stable.
     - (d) The game's dynamic decal manager itself (`dynamicdecalmgr`, MP
       `sub_825583F8`; see the bullet-impact dossier).
   - Useful env vars:
     - `COD3_VBAUDIT=1` with `COD3_DENSEALL=1`
     - `COD3_NOLIVEBUFFERS=1`
     - `COD3_OLDPOLYOFFSET=1`
     - `COD3_NOPOLYOFFSET=1`
     - `COD3_NOMIDFRAME=1`
7. **Campaign menu, UI and text upscales,** a few images at a time:
   1. Set `texture_dump=1`, browse the menus, then set it back to 0
      (dumping during movies hangs the campaign).
   2. Run `LIST-DUMPS`.
   3. Upscale 4x with Real-ESRGAN.
   4. Put the results in `textures\replace\` under the dumped hash name.

## Working style that has worked

- **Change one thing at a time, log it, and test it.** Each change gets a
  console switch to put the old behaviour back.
- **When a crash appears, reproduce it and read the log** before blaming the
  newest change. 0.5.96 and 0.5.97 undid innocent changes because of that.
- **Write a patch-note entry for every build:** what Justin saw, why it
  happened, what changed, and how it was tested.

0.5.106 test update: both executables built and installed. Scripted devmap mp_merv loaded and ran without a crash. The host stayed at its spawn (4741,320,-116.7), despite queued setviewpos/use, so no exit hook was exercised. Test process 16356 was stopped explicitly. Passenger/rear turret side exits still require a real seat test. Ride height remains a separate investigation; saved spawn Z -128 versus stationary Z -105 is evidence, not a confirmed cause.

0.5.107 continuation: user confirms 0.5.106 still fails right/rear exits. Live log only contains unlink callers 82571CFC (relink) and 8257AD34 (native vehicle dismount), never the 0.5.106 allowlist. New hook wraps 8257ABF0 and applies after native origin setter/cleanup, with link zero and client+240 bit0x100000 cleared. Entry tags remembered by player/vehicle through 82571A58: left/right/rear. Rear sweeps to local X below hullMinX; sides to local Y outside bounds. Build/test pending. Old open-item description above describes 0.5.106 and is superseded by this continuation.

0.5.107 test status: both builds passed, installed VERSION 0.5.107. Merville startup passed. Background test PID21852 was stopped and absence verified after user heard invisible test audio. It never reached a dismount; no claim of live seat validation. Do not launch another hidden audio-producing session while user is testing.

0.5.108: all 0.5.107 exits failed grounded check. Found definite layout error: trace hit position +0, normal +16, fraction +32; slope must read normalZ +24, not positionZ +8. Native producer 824A0468 proves start+travel*fraction goes to +0 and plane to +16. Fixed and added per-candidate fraction/height/normal diagnostics. User requested bot cover, head aim, objective/enemy routing while finishing exits; implement as separate subsequent build. No hidden audible gameplay tests.

0.5.109 camera investigation: bot creation, side/kit selection and respawn lacked ThinkBots' CurrentClient scope (82BCBE9C). Added RAII scope with stage/slot and host ADS/weapon diagnostics, console bot_camera 0/1 rollback. This corrects a definite inconsistent context, but zoom causality still needs live bot_add confirmation. Do not claim camera fixed based on compilation. User also requested cover/head aim and better objective/enemy routes; next separate build. 0.5.108 both executables built/installed; no hidden game run.

0.5.110: tactical bot layer with bot_tactics 0/1. bot_navigation.h pure collision steering/cover planner, tests in chat work/test_bot_navigation.cpp passed six scenarios. bots.cpp native traces use own guest stack + corrected result layout, 200ms LOS cache, head/eye-height aim, pitch gate, post-visibility reaction, collision route steps every250ms, damage/low-health cover every2.5s, four-second shelter. Existing Domination objectives only; local routes do not guarantee escaping large mazes. Both builds pending. 0.5.109 camera isolation built/installed but live zoom test outstanding. User requested sniper ADS L3 steady aim next, separate build.

0.5.111 sniper input: original MP config/mp_xenon_a.cfg binds L3 +sprintbreath. Tap-sprint filter stripped this while ADS. New sprint_input.h preserves raw scoped L3, cancels latch, reports ADS movement as walking. No automatic aim-out or ADS movement restriction. Native game supplies breath/steadiness/recovery. scope_breath 0/1 rollback. Campaign mapping untouched (SP binds L3 binoculars, Up holdbreath). Compiled input-state tests passed; builds pending. 0.5.110 both builds passed/installed with corrected visibility accepting enemy handle as well as clear fraction. All live tests remain outstanding; do not claim gameplay confirmation.

Final installed status: VERSION 0.5.111. Both exes built successfully after repairing the missing input.h include. Bot planner and sprint-input tests were recompiled against the actual source headers and passed. No hidden game launch. Live exit, camera, tactical AI and breath/sway tests are still required. Native hold-breath inhale/exhale assets exist; user offered replacement audio, but no replacement is needed until native behavior is verified. Next user test should isolate Kangaroo right-side/rear exits first, then bot_add camera, then tactics and scope controls. Ride height and bullet flicker remain open.

0.5.112 continuation: Justin confirms the Kangaroo side/rear exits work and feel good on 0.5.111. Remaining visual gap estimated 4-6 inches. Native physics table 825FEAF0 has wheel_radius +20, susp_adj +32, name +112; constructor 82418C80 last float +200. Hook 82420998 before initialization, give only registered Kangaroo entities a private 208-byte copy (copy204 known bytes), reduce mp_horsch radius by5, min8. No shared preset mutation, no entity teleport. kangaroo_drop 0..8 (default5), next map start/restart. Build pending. Other behaviors untouched. Asked optional status of bot_add camera on 0.5.111 before changing camera again. Inspection helper breath_lookup crashed from local executable-data reading and was stopped; use existing CoD3Scan, no more helper launches.

0.5.112 runtime/current camera research: current visible game PID4456 belongs to user/test, leave its controls alone. Header/notes say112, both exes installed. Native FOV calculator 82453810 reads CurrentClient82BCBE9C and host record ADS+172; base cvar float82BA2BC0; local view array82C7D490 stride6368, cached weapon pointer+4732, ADS transition flag+4644, zoom tween+6212/+6216/+6220/+6224. Camera callbacks 824553A8/824553F0 and824674E0 write/use FOV+96 in camera object; +48 gates recomputation, +272/+276/+280 tween. 824674E0 camera+480 selects local-view array. No camera writes or hooks added yet. Existing generated code inspected only; no edits. Helper breath_lookup was stopped after crash; use existing CoD3Scan for inspection (disasm supports data words).

0.5.113: Justin confirms native sniper steady aim/sounds and exits work; zoom persists on111. Optional bot_setup3 isolation was not run. Camera-focused candidate fix bypasses both local menu callbacks by default (g_botCamera1), directly writes team+4 and existing SessionPlayer+604 as native823EA4E0 does, then existing server respawn. No forced host FOV. bot_camera0 legacy path. Added observational FOV hook82453810 (f1 return) and camera824674E0 (object+480 local index0; fields+48,+96,+272,+276,+280), throttled2s. Build pending; need fresh match bot_add, camera/teams/runtime. Current game112 user actively testing; do not seize controls or kill it. Height112 private radius18->13 initialized, actual hull drop not yet confirmed.

### 0.5.113 build and latest isolation result

Both executables built and installed successfully. Justin tested bot_setup 3,
then bot_add 1 on 0.5.112: no visible bot and no zoom. Log confirms slot 4 was
created and the spawn path ran, but health stayed 0, move type 6, state 0.
This is an inactive bot, not a successful live spawn/camera fix. Snapshot:
chat work/CoD3MP-0.5.112-setup3-inactive-bot.log. Team/menu setup is implicated,
but actual cause remains unproven. 0.5.113 uses direct team assignment and
keeps the local menu callbacks out; native FOV diagnostics are included.
Next fresh-match test must use bot_setup 1 before bot_add 1, since setup 3
skips the new team assignment too. Verify a living visible bot, teams and
normal hip/ADS camera. Sniper steady aim/sounds and Kangaroo exits confirmed.
Kangaroo actual ride height remains unconfirmed. No game is running now.

### 0.5.113 live result: camera normal, bot still inactive

Justin ran bot_setup 1 / bot_add 1: view remained normal, no bot found.
Log verifies direct side 1 assignment, no SessionPlayer, and spawn leaves
slot 4 at health 0, move type 6, state 0. Actual native and camera FOV remain
90 while hip firing. Camera symptom improved, but live bot spawning is NOT
fixed. Do not send Justin searching for a living bot until spawn succeeds.
Existing console streak_give uav grants a charge; d-pad right calls it.
UAV draws enemies and cannot reveal an inactive bot as a living enemy.
Snapshot in chat work/CoD3MP-0.5.113-normal-view-inactive-bot.log.

### 0.5.114: missing native bot spawn finalizer

Native offline spawn 82406BB8 calls 82593608(entity, origin, angles, 1, 0),
THEN callbacks+3312(handle pointer, 0). Bot hook 824D0AF0 only did placement.
82593608 explicitly clears ps+304 health. Default finalizer 82211DF8 sets
state through callback table+4944 (value3), health/loadout through game APIs.
Added the missing finalizer with the bot's handle and existing isolated
current-client scope. No forced health or state writes. Build pending;
needs fresh-match bot_setup1/bot_add1 with visible live bot and normal ADS.

Justin's enhanced battlefield AI roadmap: squad coordination (advance,
defend, support, attack together); dynamic cover (peek, crouch, relocate,
avoid exposure); suppression/morale (pinned vs aggressive); tactical
awareness (last known enemies, gunfire, flanks, grenade response); human-like
combat (natural aim, reload choices, mistakes, personalities). Implement in
separate reviewable changes after living bots spawn reliably. Existing110
local collision/cover/head aiming logic needs real combat validation first.

0.5.114 result: both exes built/installed successfully. Visible launch via
computer-use reached multiplayer menu, title confirms114. Click/Down/Space
inputs did not advance native menu; no match or bot runtime validation yet.
Game intentionally remains visibly open at menu for Justin's fresh-match
bot_setup1 / bot_add1 test. Do not claim health/spawn is fixed from build.
No test debug.txt modification was made; existing move_watch1 remains.

### 0.5.114 live confirmation / 0.5.115 class fix

Finalizer82211DF8 succeeds: bot health100, state3, move type0. Justin cannot
see him even after debug bot_bring1 places him beside host; bot subsequently
moves/chases. debug bot_session1 logs differences (read-only, no session
clone); restored original debug.txt containing move_watch1 afterward.
Class log explicitly shows bot class -1; ps+1920/+1922 FFFF, entity+536 model
pointer0, weapon0. Native getters820CE3D8/820CE538 read these halfwords;
8220D1C8 calls82206028 with sign extended class. Initialize both to0 for
new bots instead of join sentinelFFFF. Native finalizer supplies valid kit.
115 build pending. Need fresh match bot_setup1/bot_add1 model/weapon/camera.
Snapshot chat work/CoD3MP-0.5.114-invisible-class-minus1.log. UAV reports
activation but actual bot markers not verified; implementation simply enables
enemy_radar at82B2CF90+12, native compass825049C8 and related widgets need
inspection if radar still misses live visible bots. No UAV code change115.

UAV read-only finding during115 build: native825049C8 gates on
EnemyRadar82B2CF90+12, then enumerates opposing player entities. Each candidate
also requires ps+132 !=0 and ps+132+15000 >= server time (82504AE0 onward).
This path still filters recent gunfire even with radar enabled. Thus activation
alone does not prove continuous bot-location markers; review other compass
widgets and this timer before a separate UAV fix. No UAV source changed115.

0.5.115 installed: normal build completed successfully for both executables.
Game remains closed; no new gameplay test launched. Next: fresh match,
bot_setup1 / bot_add1; check log class0 vs previous-1 and visible soldier/gun,
normal hip/ADS camera, combat. User can call streak_giveuav then d-padright,
but UAV continuous markers remain a separate unresolved issue. All diagnostic
debug.txt edits from114 have been restored. No uploads or network activity.

### 0.5.116 presentation investigation

115 confirmed class0 at82206028, living bot still invisible, weapon0. Native
ownership query824C3490 calls823E5CC0, which searches FOUR LOCAL session
entries for the entity; bots have none. In model selector821FB3E0, exact
query return821FB408 gates model definition assignment via callbacks+2752
(native824CB9E8 storesps+200), then821FB300 selects accessories. Hook only
that query/callsite for a registered bot with valid handle generation. No
other ownership overrides, session copying, hardcoded model or input changes.
116 build pending. Needs visual/camera confirmation; may need later native
model build/equipment dispatch work. Weapon0 and UAV remain open. Native
UAV825049C8 still uses ps+132 recent-activity filter while radar enabled.

Justin asks whether bots can be configured in pre-match settings and whether
his friend can see them. Explained current design is authoritative host AI;
pre-match count/difficulty/team controls can be added. Friend-side visibility
and state replication are NOT verified. Need two-client test after local
model/equipment works; do not promise session/network correctness merely
because host AI runs. Existing SendClientSnapshot hook skips packets TO bot
clients only, not necessarily bot entities from real clients' snapshots.

0.5.116 built/installed successfully for campaign and MP. No gameplay test
launched; prior automated menu inputs do not register reliably. Source and
native callsite review passed; live model/normal camera still required.
Next fresh-match bot_setup1/bot_add1, inspect model eligibility log plus
class0, visible soldier, weapon (still possibly0). No UAV change this build.

### 0.5.117: native drawable creation, weapon switch report

116: bot class0 alive, ownership allowance logged, still invisible when
brought beside Justin; entity+5360, +540 has definition. Debug probes restored.
Missing native network setup step found:823F818C and82400D40 lookup text
820653E4 (CoD3Scan verifies generic_human) through824B5688, then call8257C090
(entity, entry). Factory82516590 creates separate instance;8257C090 stores
it inentity+536 and calls8257A6C8 to rebuild rig. Added after placement and
before114finalizer if drawable absent; guard entry+4 per824CDFAC. Logs entry
and drawable.117build pending. Need visual/camera test; don't claim fixed.
User also cannot switch weapons with controller Y/Triangle or mousewheel;
respawn equips pistol or Kar but cycling both ways fails. Host log has
primary21(kar98_sniper) and pistol15, inventory exists. Await optional fresh
match BEFORE bot_add comparison. Input-edge Y diagnostics added117 only,
no weapon behavior change. Both inputs map toPadY; sprint filter preservesY.
Snapshot work/CoD3MP-0.5.116-invisible-after-bring.log in chat directory.
UAV remains unresolved: message activation but no botcompass marker.

117 result: both executables built/installed, native source review passed.
No gameplay launch this build. Game closed for installation. Next test should
check Y AND wheel before bot_add, then bot_setup1/bot_add1; inspect117factory
log for nonzero entity+536, and visible bot after bringing near if needed.
All prior debug.txt modifications restored. Host inputY logs distinguish
controller/keyboard-wheel and remain diagnostic only. Weapon switching and
UAV are unresolved, not repaired117. User wants concise status each test.

### 0.5.118 volume persistence (separate user-requested patch)

User requests50% starting level, then remember last volume. Existing F11
slider only updates g_edit; Settings::Set/Save requires Apply. Added volume
only SetMasterVolume(int), clamps0..100, changes live setting under mutex,
saves immediately on slider change. F11 explains auto-save. Default/fallback
50 instead70; existing saved values respected. Installed INI set50 once,
backup chat work/CoD3Recomp-before-volume118.ini. Tests/build pending.
No bot/UAV/weapon changes118.117 drawable change awaits live result, and
weapon switching with Y/Triangle AND mousewheel remains unresolved; first
fresh-match test before bot_add still requested. Do not lose AI roadmap.
