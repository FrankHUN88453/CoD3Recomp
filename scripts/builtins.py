"""A catalogue of the engine functions the level scripts call.

    python scripts/builtins.py <image.bin> [--level chambois] [--markdown out.md]

CoD3's level scripts are C++ ("Broc", c:\\cod\\code\\script\\include\\*.inl)
compiled into each level's DLL. They reach the engine through one table of
function pointers at *(0x82A2A2A0), which sub_824BB028 and sub_824B9D70 fill
from two lists of (byte offset, function) pairs in the executable's data
(0x825F5638, 265 pairs; 0x825F5E80, 222 pairs). The level code calls
`lwz rX,offset(table) / mtctr rX / bctrl` with the arguments already in
registers, so the names of these built-ins are nowhere in the game: only
their offsets and what they do.

For each entry this prints the offset, the function, how many times the
level's code calls through that offset, the argument registers the function
reads before it writes them, the notable functions it calls, the text it
refers to, and a name where one has been worked out (NAMES below).
The image is COD3_IMAGEDUMP's (0x82000000, sixteen megabytes).
"""
import collections
import glob
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
image = open(sys.argv[1], 'rb').read()
level = sys.argv[sys.argv.index('--level') + 1] if '--level' in sys.argv else 'chambois'
markdown = sys.argv[sys.argv.index('--markdown') + 1] if '--markdown' in sys.argv else None


def word(address):
    return struct.unpack_from('>I', image, address - 0x82000000)[0]


def text(address):
    if not 0x82000000 <= address < 0x82000000 + len(image):
        return None
    start = address - 0x82000000
    end = image.find(b'\0', start, start + 200)
    raw = image[start:end]
    if len(raw) >= 3 and all(32 <= c < 127 for c in raw):
        return raw.decode()
    return None


# What is known of the functions the built-ins call.
KNOWN = {
    0x824534B0: 'va',
    0x820CEBB8: 'hash a name',
    0x824FE120: 'set goal weight',
    0x824FE410: 'set knob all',
    0x82500A40: 'set anim',
    0x82500B80: 'set anim limited',
    0x824F3F50: 'clear tree',
    0x82554D60: 'update model/tree',
    0x82555B98: 'set entity tree',
    0x823D0950: 'actor tree',
    0x82538A28: 'anim time',
    0x825389E8: 'anim weight',
    0x821276D0: 'free',
    0x82127A20: 'alloc',
    0x8234EBA0: 'memset',
    0x82144CA8: '(empty print)',
    0x82539518: '(empty print)',
}

# Names worked out by hand, by offset.
NAMES = {
    # animation (an anim argument is (anim list << 16) | entry)
    0x674: 'setflaggedanimknoball (sub_824FE410, with a notify)',
    0x678: 'the anim\'s frame count (class +68)',
    0x67C: 'getanimlength (class +56; a random node\'s chosen child)',
    0x680: 'animhasnotetrack (the entry\'s notetrack, by hash)',
    0x684: 'clearanim (anim, blend time)',
    0x688: 'setanimknob (siblings let go, then set anim)',
    0x68C: 'setanim (sub_82500A40)',
    0x690: 'setflaggedanimknob',
    0x694: 'setanimknoball (sub_824FE410)',
    0x698: 'setflaggedanim',
    0x69C: 'getanimtime (info record +16)',
    0x6A0: 'useanimtree ("cannot change the animtree of classname")',
    0x6A4: 'clearanimtree (tree dropped, freed at the frame end)',
    0x6B0: 'stops every child of a node at once',
    # the rest by what they do
    0x4: 'println (the print is empty in this build)',
    0x8: 'nothing (an empty function)',
    0x118: 'isalive (actor +784, or the entity\'s health +776)',
    0x11C: 'isdefined for an entity reference',
    0x140: 'randomint (a linear congruential generator)',
    0x144: 'randomfloat',
    0x14C: 'randomfloatrange',
    0x42C: 'gettime (the level\'s time, ms)',
    0x4E0: 'the system clock',
    0xAC4: 'delete (the script heap, else the general one)',
    0xAC8: 'new (the script heap)',
    0xACC: 'free (the script heap)',
    0x5DC: 'plays a sound alias (sub_824927C0)',
    0xF8: 'plays a sound alias or a spoken line (sub_824B3FF0)',
    0x58: 'endon (a type 3 record of entity and name in the event system)',
    0x5C: 'whether the event system exists',
    0x1C: 'sprintf (the C runtime\'s)',
    0x124: 'isai (the entity has an actor at +532)',
    0x148: 'randomintrange',
    0x590: 'linkto',
    0x5A0: 'delete (sub_82540D78 frees the entity)',
    0xAAC: 'fadeovertime (a HUD element)',
    0xAC0: 'new, from the fixed pools first',
    0x0: 'print (formatted; the print is empty in this build)',
    0xC: 'nothing (an empty function)',
    0x10: 'nothing (an empty function)',
    0x14: 'nothing (an empty function)',
    0x18: 'nothing (an empty function)',
    0x20: "strcpy (the C runtime's)",
    0x24: "strncpy (the C runtime's)",
    0x48: 'nothing (an empty function)',
    0x64: 'nothing (an empty function)',
    0x68: 'nothing (an empty function)',
    0x8C: 'waittill (a record of entity and event name in the event system)',
    0x90: 'notify (entity, event name)',
    0x94: 'getent / getentarray (value, key: classname, targetname, target, groupname)',
    0x98: "the entity's model (+500)",
    0xA0: "sets or clears flag 2 on the entity model's record (sub_8251B798)",
    0xA4: 'getnodearray (path nodes by value and key, sub_823C9948)',
    0xA8: 'the path nodes nearest a point within a radius (sub_823C3688)',
    0xAC: 'getnode (sub_823C96B0)',
    0xB0: 'the number of clients in use',
    0xB4: 'nothing (an empty function)',
    0xB8: 'playloopsound on an entity, unless it has one already (+904)',
    0xBC: 'playsoundatposition (sub_82535788)',
    0xC0: 'playsound on an entity, with a notify (sub_82535460)',
    0xC4: 'playloopsound on an entity, stopping the one it had (+904)',
    0xC8: 'playsound on an entity (sub_82535568)',
    0xCC: 'stopsound (a sound handle)',
    0xD0: 'whether a sound handle still plays (sub_82528E38)',
    0xD4: 'a switch of the sound system (sub_8252D5B8, flag in r4)',
    0xD8: "sets a sound handle's volume (sub_82523350)",
    0xDC: "plays one of a vehicle's eleven sounds (+528, sub_82534ED0)",
    0xEC: "a zone by name hash: sub_82452308 on the bank set's +400 list",
    0xF0: "a zone by name hash: sub_82452270 on the bank set's +400 list",
    0xF4: 'sets the global flag byte at 0x82A2A262',
    0xFC: 'plays a sound alias or a spoken line, without waiting (sub_824B3FF0)',
    0x100: 'whether a sound alias exists (sub_82114B80)',
    0x104: 'sets the float at 0x825CC3E0, no lower than a floor',
    0x108: 'sets the float at 0x825CC3E4, no lower than a floor',
    0x10C: 'clears the three words at 0x82A2A30C',
    0x110: "a trace from the entity's origin (sub_8247EA20)",
    0x114: 'sets the level\'s "tosser" settings by name (sub_824814C8)',
    0x120: 'isvehicle (+528)',
    0x128: 'isplayer (+536)',
    0x12C: 'isturret (+540)',
    0x130: 'the turret is of type 2',
    0x134: 'the actor is in state 11',
    0x138: 'a handle is set (not 0, not -1)',
    0x13C: 'a handle is not -1',
    0x174: 'abs',
    0x184: 'distance',
    0x188: 'distancesquared',
    0x190: 'lengthsquared',
    0x198: 'vectordot',
    0x38: "starts the level's first script thread (a 56-byte thread object: function r6, self r7; run at once if r8)",
    0x44: "sub_82499EA8 on the script VM's +48 list with a table of the level's data",
    0x60: 'isdefined for an entity reference (a second copy)',
    0x6C: "the running thread's handle (+20), made on first use",
    0x70: 'kills a thread by its handle (flags 4, 8 and 64)',
    0x88: "sub_8252A4A8 on the entity's +512 object",
    0x194: 'closer: whether a is nearer b than c',
    0x19C: 'vectornormalize (out, in)',
    0x1B4: "the screen's size as two floats (sub_820E9210, sub_820E8C58)",
    0x1B8: 'sets the float at 0x825D0F70',
    0x1BC: "sets a flag (+1452) on the local player's vehicle, and the byte at 0x82A2A0CB",
    0x1C0: 'nothing (an empty function)',
    0x1C4: 'nothing (an empty function)',
    0x1C8: 'adds an entity to, or takes it from, a list of 32 at 0x82A92390 (entity +636 flag 128)',
    0x24C: 'getdvar as a string',
    0x250: "getdvarint (the dvar's integer at +32)",
    0x254: "getdvarfloat (the dvar's float at +28)",
    0x258: 'setdvar (a string)',
    0x25C: 'setdvar (an integer, "%d")',
    0x260: 'setdvar (a float, "%f")',
    0x264: 'spawn (classname, origin, spawn flags)',
    0x268: 'spawn, with a model (sub_82543568)',
    0x26C: 'spawn a trigger with a box (mins, maxs)',
    0x270: 'spawn a trigger box by name hash',
    0x274: 'spawn a character (a "cdChar" model)',
    0x278: 'spawnturret ("leftarc", "rightarc")',
    0x27C: 'spawnturret, with a model',
    0x280: 'musicplay (sub_824897A0 on the music player at *(0x82A2A2CC))',
    0x284: 'musicstop',
    0x288: 'musicplay with a fade time (sub_824895C0)',
    0x28C: "the music's volume over a time (player +20, +24, +28)",
    0x290: 'plays a 2D sound by alias, at a volume (sub_82487DC8 on the mixer at *(0x82A2A2C4))',
    0x294: 'stops a 2D sound by its handle (sub_82478A58)',
    0x298: 'sets a sound mix, second slot only (sub_82484EC8)',
    0x29C: 'sets a sound mix over a time (sub_82484EC8)',
    0x2A0: 'sets a sound mix, both slots (sub_82484EC8)',
    0x2A4: 'a sound alias by name hash: sub_82119A18',
    0x2A8: "sets a named channel's volume over a time (sub_82465E90)",
    0x2AC: 'fades a named channel (sub_82465F08)',
    0x2B0: 'a sound alias by name hash: sub_82119A98',
    0x2B4: 'a sound alias by name hash: sub_82119AC8',
    0x2B8: 'a sound alias by name hash: sub_82119B18',
    0x2BC: 'a sound alias by name hash: sub_82119B88',
    0x2C0: 'setreverb ("Preset_Alley" and the rest, sub_824661F0)',
    0x2C4: 'sets the global flag byte at 0x82A2A0D3',
    0x2C8: 'loads a sound bank (.wbk; sub_82494E90 on *(0x82A2A2C0))',
    0x2CC: 'unloads a sound bank (sub_82494C70)',
    0x2D4: "a path node's +4, by node reference",
    0x2D8: "sets the player's integer at +1920",
    0x2DC: "sets the player's float at +1352",
    0x2F4: 'autosave (a checkpoint by name, e.g. "post_igc"; sub_82476B20 on the game state at *(0x82A2A300))',
    0x2F8: 'a saved game variable as a vector, by name hash',
    0x2FC: 'a saved game variable as an integer, by name hash (0xFEFEFEFE when absent)',
    0x300: 'a saved game variable as a float, by name hash',
    0x304: 'sets a saved game variable to a vector',
    0x308: 'sets a saved game variable to an integer',
    0x30C: 'sets a saved game variable to a float',
    0x328: 'nothing (an empty function)',
    0x32C: 'a game statistic (sub_8251B100, when statistics are on)',
    0x330: 'a game statistic counter (0x829C2910 table), optionally plus its base',
    0x334: 'sets a game statistic counter',
    0x338: 'adds one to a game statistic counter',
    0x33C: 'takes one from a game statistic counter, not below 0',
    0x340: 'sets a bit in the game statistics bit field',
    0x344: 'sub_82519410 on the game statistics',
    0x348: 'a game statistic update (sub_8251AF08)',
    0x34C: 'starts a timed shake at an origin (strength, time, radius; a pool at 0x82A9F280)',
    0x350: "moves a timed shake's origin",
    0x354: 'stops a timed shake',
    0x358: "sets this slot's byte in a table of 14 at 0x82A8E4F8",
    0x35C: 'sets three render floats at 0x825A2338',
    0x360: 'sets the render float at 0x825CC5A4',
    0x364: 'sets the render floats at 0x825A2344 and 0x825CCE94',
    0x368: 'sets the render float at 0x825A2318',
    0x36C: 'sets a render block at 0x825FBEF0: on, 0, then five floats (a fog)',
    0x370: 'moves the render value at 0x825CDA9C to f2 over f1 seconds (at once when f1 is 0)',
    0x374: 'sets the render float at 0x825CDABC',
    0x378: 'sets an entry (r3, r4) of a table of 100 at 0x82A11D90 to f1 (sub_82416260, sub_82415468)',
    0x37C: 'sets an entry (r3, r4) of a table of 500 at 0x82A158F8 (sub_82415140, sub_82414F68)',
    0x380: 'sets the render float at 0x825CDBE0',
    0x384: 'sets a render ramp at 0x825CDB38 (five floats: from, to, over, and a second pair)',
    0x388: 'sub_82415BB8 with the table at 0x82A11D4C',
    0x38C: 'sets a render block at 0x82909D70: on, r10, seven floats',
    0x390: 'sets two floats of the render block at 0x82909D70 (+64, +68)',
    0x394: "sets a float of the world's render settings (*(0x82A2AD7C) +128 -> +20)",
    0x398: "sub_823F6758 on the world's render settings, and sets 0x825A2158",
    0x39C: "sets the entity's render flag 1 (+644)",
    0x3A0: "sets or clears the entity's render flag 4 (+644)",
    0x3A4: 'sets the render values at 0x825CC590 (an integer and three floats)',
    0x3A8: "sets the entity's render flag 8 (+644)",
    0x3AC: 'nothing (an empty function)',
    0x3B0: "sets or clears the entity's render flag 16 (+644)",
    0x3B8: 'sub_82147098 with a flag and three floats kept at 0x82A2A0C8',
    0x3C0: 'nothing (an empty function)',
    0x3C4: 'sets the render float at 0x825CDBA8',
    0x3C8: 'nothing (an empty function)',
    0x3CC: "sub_820A9BF0 on the entity's model instance (+508)",
    0x3D0: "sets the entity's flags 0x02000000 (+632) and 8 (+228)",
    0x3D4: "sets two half-words of the level's settings (level +20 -> +4, +8)",
    0x3D8: "sets two words of the level's settings (level +20 -> +4, +12)",
    0x3DC: "whether the entity's model instance has bit 0 of +196 clear",
    0x3E0: 'sets a float and a word at 0x82A565EC',
    0x3E4: 'sets the state of a HUD slot (3 or 0; sub_824D1C10, sub_824CB000)',
    0x3E8: 'a HUD element by name hash (sub_824D9670)',
    0x3EC: "sets three of the entity's flags (+632: 0x10000000 and two more)",
    0x3F0: "sets or clears the entity's flags 0x04100000 (+632)",
    0x3F4: "sets or clears the entity's flag 0x20000000 (+632) and a half-word of its +548 object",
    0x3F8: 'applies an integer and a float to the matching entry of every loaded bank (sub_82404F38)',
    0x400: 'a 36-byte "ANFY" record with two name hashes (sub_820CE818)',
    0x404: 'three words of a named record (sub_8251A370 on *(0x82A2A198), by name hash)',
    0x414: "sets a float (+232) of the entity's +548 object",
    0x418: 'sub_8246F4D0 on two entities',
    0x41C: 'sub_820A1838 with seven floats (a physics push?)',
    0x420: 'sub_820A1838 at two positions with forces',
    0x424: "adds an id to, or takes it from, the game state's list of 256 (+856, count +1368)",
    0x428: "whether an id is in the game state's list of 256",
    0x430: 'the float at 0x825F4E14',
    0x434: 'a string from the table at 0x825D0F60 by the index at 0x829B1B7C',
    0x45C: 'nothing (an empty function)',
    0x460: 'nothing (an empty function)',
    0x464: 'the entity and origin of the record at *(0x82A2ACE4)',
    0x468: 'getstartorigin (origin, angles, anim; sub_824FE5B0)',
    0x46C: 'getstartangles (origin, angles, anim; sub_824FE5B0)',
    0x470: "an anim's start relative to an origin (sub_8252E838, sub_824FE5B0)",
    0x474: "getmovedelta (an anim's translation; sub_824FE698)",
    0x478: "getangledelta (an anim's rotation; sub_824FE698, sub_82531A00)",
    0x47C: "the local player's float at +5712 (a table of 5840-byte entries)",
    0x484: 'a trace between two points (mask 0x0280E033, or 0x00802033 with the flag; sub_8247EA20)',
    0x488: 'a trace with more options (sub_82485D38)',
    0x48C: 'nothing (an empty function)',
    0x490: 'nothing (an empty function)',
    0x494: 'a message of category 9 (sub_82513C98)',
    0x498: 'radiusdamage (origin, range, max, min; sub_82567510)',
    0x49C: 'radiusdamage with an attacker entity',
    0x4A0: "sets the game state's word at +3104 (0x82A4F3A0)",
    0x4B4: 'earthquake (scale, duration, origin, radius; sub_82439058)',
    0x4B8: 'a timed shake with its defaults (the pool at 0x82A9F280)',
    0x4BC: 'sets the global flag byte at 0x82A2ABEA',
    0x4C0: 'sets the script string at 0x82A55B50',
    0x4C4: "sets the game state's word at +3096",
    0x4C8: 'sub_825411F0 (with 12)',
    0x4CC: "sets the game state's float at +3084 (scaled)",
    0x4D0: 'sub_82438AB8 with two positions',
    0x4D8: 'nothing (an empty function)',
    0x4DC: 'nothing (an empty function)',
    0x4E4: 'a null entry (no function)',
    0x4E8: "a registry entry by name for the level's bank (sub_8242F4A8 on *(0x82A2ABB8))",
    0x4EC: "a registry entry by name for the level's bank, with its values (sub_8242F4A8)",
    0x504: 'a path node lookup by name (sub_823CB668, sub_823D0230)',
    0x510: 'takes a free slot of five kinds in a table of 124-byte entries at 0x829B01B8',
    0x514: "sub_823CC598 on the level's path nodes",
    0x518: 'a zone by name (sub_82459D20 on the bank set)',
    0x51C: "the zone of an entity's bank (+496, or the level's)",
    0x520: 'the zone that holds a point (sub_8241C0F8)',
    0x524: 'whether a zone is active (its +196 is 0)',
    0x528: 'sets a float (+68) of an object and counts the change at 0x825CE6BC',
    0x40: "starts a script thread waiting for an entity's notify (an AeThreadEntityNotifyState)",
    0x4C: 'wait (an AeThreadWaitState of f1 seconds, then the thread yields)',
    0x50: 'waits a number of frames (an AeThreadWaitFramesState)',
    0x54: 'waits for a pak (a zone or bank) to be ready, with a time limit (an AeThreadPakNotifyState)',
    0x52C: 'resets a float (+68) of an object to its default (1)',
    0x530: 'activates a zone (sub_82462FB0)',
    0x538: 'nothing (an empty function)',
    0x53C: 'nothing (an empty function)',
    0x540: 'nothing (an empty function)',
    0x544: 'nothing (an empty function)',
    0x548: 'nothing (an empty function)',
    0x54C: '0x550 with a string argument held on the thread',
    0x550: 'plays an animation on the entity with a notify (sub_824B22C0)',
    0x554: "ends the actor's scripted state (state 15; sub_823C8D40, entity +924)",
    0x558: 'puts the actor in state 8 (sub_823C7760, sub_823C8DA8)',
    0x55C: 'takes the actor from state 8 to 2',
    0x560: 'attach (model, tag, with a bank; sub_825585E8)',
    0x564: 'attach (model, tag)',
    0x568: 'attach (model, no tag)',
    0x56C: 'detach (model, tag)',
    0x570: 'detach (model, no tag)',
    0x574: 'detachall (the seven attachments at +932; sub_82558598)',
    0x578: 'getattachsize (the seven slots at +928)',
    0x57C: 'getattachmodelname',
    0x580: 'getattachtagname',
    0x584: "whether bit r4 of the entity's +621 is set (a hidden part)",
    0x588: 'linkto with a tag and offsets',
    0x58C: 'linkto with a tag',
    0x594: 'puts the entity on a vehicle, copying its origin (sub_8254A928)',
    0x598: 'puts the entity on a vehicle by tag (sub_8254A928)',
    0x59C: 'puts the entity on a vehicle, untagged (sub_8254A928)',
    0x10D8: "registers an object on the running thread's stack with its destructor (a BrocDtor<T>, e.g. for Broc::string), so a killed thread still destroys it (sub_824A6040)",
    0x10DC: "unregisters an object from the running thread's list (sub_82499D38)",
    0x3C: "thread: starts a script thread at a function of the level's code (with the .bro file, line and function name for debugging)",
    0x9C: 'registers a level function for a named AI behaviour state ("anim_main_init", "anim_main_combat"...; sub_8251BBC8 on *(0x82A2A1A0))',
    0x3FC: 'registers a level function for a notetrack name ("end", "finished", "footstep"...)',
    0x408: 'starts drones along a spline (name, counts, spacing; sub_8251E520 on the drone system at *(0x82A2A1A0))',
    0x40C: 'a drone spline by name ("spline_event1_center"...; sub_8251E7A0)',
    0x410: 'a drone group by name ("ev7_ally_drn_grp_1"...; sub_8251E6C0)',
    0x534: "prof_begin (a stub that returns 0; the argument is the function's name)",
    0xAD4: 'assert (disabled: file, line, message such as "out of bounds")',
    0xAD8: 'a script warning (disabled: file, line, message)',
    0xADC: 'a script error (disabled: file, line, message such as "Cannot wait for undefined time")',
    0x150: 'sine and cosine of an angle in degrees (sin to *r4, cos to *r5)',
    0x154: 'sin (radians; checked by running it)',
    0x158: 'cos (radians; checked by running it)',
    0x15C: 'tan (radians; checked by running it)',
    0x160: 'asin (checked by running it)',
    0x164: 'acos (checked by running it)',
    0x168: 'atan (checked by running it)',
    0x170: 'log (natural; checked by running it)',
    0x180: "sub_8234C850 (the C runtime's)",
    0x1A0: 'vectortoangles (out, in; checked by running it)',
    0x1A4: 'an up vector from pitch and roll, the yaw left out (checked by running it)',
    0x1A8: 'anglestoright (out, angles; checked by running it)',
    0x1AC: 'anglestoforward (out, angles; checked by running it)',
    0x1B0: 'anglevectors: forward, right and up from angles (checked by running it)',
    0x2D0: 'whether a weapon exists, by name (sub_82464FD0 looks it up in the weapon table *(0x82A2A2E0))',
    0x3BC: 'a grenade-type projectile at a position by weapon name ("grenade" handled apart; sub_823F79C0)',
    0x438: 'getaiarray (a team; "getai")',
    0x440: 'getaiarray (every team)',
    0x444: 'getspawnerteamarray ("getspawnerteamarray")',
    0x448: 'a weapon\'s class, checked ("unknown weapon \'%s\' in getWeaponClassname")',
    0x44C: "a weapon's display name (its definition's +1432)",
    0x450: 'getweaponclassname',
    0x454: 'getweaponclassname for a second weapon slot',
    0x458: 'two names for one (sub_82509840), appended to a string',
    0x4A4: 'missionsuccess / changelevel: the next level\'s name to the game state\'s +2780, the next mission unlocked unless it is "creditsdone", +2756 set for "gamefinished"',
    0x4A8: "missionfailed (reason): the failure message to the game state's +2768, +2764 set, +2744 the time plus 500 ms",
    0x4AC: "the mission's end with a time only (the game state's +2776)",
    0x4B0: "the mission's end with a text (copied to the game state's +2780; +2752 and +2760 set)",
    0x4D4: 'magicbullet (weapon, start, end; "MagicBullet called with unknown weapon name")',
    0x4F0: "a weapon's float by name (its definition in the weapon table)",
    0x4F4: "a weapon's value by name",
    0x4F8: "a weapon's integer by name",
    0x4FC: "a weapon's second integer by name",
    0x500: "a weapon's string by name",
    0x508: 'badplace_cylinder (name, time, origin, radius, height; "badplace_cylinder")',
    0x50C: 'badplace (a second shape)',
    0x5A4: 'whether the entity is a live trigger_multiple',
    0x614: 'disconnectpaths ("cannot disconnect paths. Make sure you have a collmap")',
    0x618: 'connectpaths ("cannot connect paths. Make sure you have a collmap")',
    0x5A8: 'dospawn with a flag (sub_824B92A8: "dospawn can only be called on actor spawners")',
    0x5AC: 'dospawn with a flag, the name held on the thread',
    0x5B0: 'dospawn (sub_824B9118)',
    0x5B4: 'dospawn, the name held on the thread (562 calls in the levels)',
    0x5B8: "a point of the entity's bounds (+236, +272..+296)",
    0x5BC: 'geteye ("BIP01 HEAD"; the player\'s or actor\'s eye, sub_823D5308)',
    0x5C0: "sub_824D1500 on the level's HUD (level +20 -> +4)",
    0x5C4: "sub_824C9B50 on the level's HUD (level +20 -> +4)",
    0x5C8: 'sub_820A0840 on two entities',
    0x5CC: "istouching (the other entity's bounds; sub_82516268)",
    0x5D0: "whether the entity was hit this frame (+568 against the level's time)",
    0x5D4: 'whether the entity was hit this frame (a second test)',
    0x5D8: 'whether the entity was hit this or the last frame, and its +884',
    0x5E0: 'sub_824721C8 on the entity, with a flag',
    0x5E4: 'whether a registry entry exists for a name in a bank (sub_8242F4A8)',
    0x5E8: "a registry entry for the entity's bank by name (sub_8242F4A8)",
    0x5EC: 'setmodel (a "cdChar" model; the team from "allies", "axis", "neutral")',
    0x5F0: "sets the byte +252 of the entity's sub_824721C8 record",
    0x5F4: "the entity's health as a fraction (a vehicle's of its +1448)",
    0x5F8: 'setnormalhealth ("setNormalHealth must be greater than 0")',
    0x5FC: 'moves a vehicle toward a point at a speed (sub_82564888; "player_tank")',
    0x600: "sets the entity's +624 (takedamage)",
    0x604: 'sub_8246BFB0 on the entity with a flag',
    0x608: 'show (clears flag 0x400 of +632)',
    0x60C: 'hide (sets flag 0x400 of +632)',
    0x610: 'setcontents (the old +324 returned; sub_82513198)',
    0x61C: 'a turret method (+544)',
    0x620: 'a turret method (+544)',
    0x624: 'a turret method with a target entity (sub_8256BE08)',
    0x62C: 'a turret method returning a value (+544, +619)',
    0x630: "the entity's +432",
    0x634: "sets the entity's +432 from another entity",
    0x638: 'a turret method with a second entity',
    0x63C: 'a turret method (+544)',
    0x640: 'a turret method (+544)',
    0x644: 'setturretteam ("axis", "allies")',
    0x648: 'a turret method (+544)',
    0x64C: 'a turret method (+544)',
    0x650: "sets a turret's float (+544)",
    0x654: 'a turret method (+544)',
    0x658: 'a turret method returning a float (+544)',
    0x65C: "a turret method with the entity's +500 (+544)",
    0x660: 'setcursorhint ("HINT_INHERIT", "List of valid hint type strings")',
    0x664: "the use hint's text by index (+444, +2668)",
    0x668: 'sethintstring ("Too many different hintstring values")',
    0x66C: 'sethintstring by a localized name hash',
    0x670: 'a localized line\'s key ("MOVIE", "%s_%s"; sub_824DEC18)',
    0x6A8: 'nothing (an empty function)',
    0x6AC: 'the level\'s script animtree ("script_animtree", "script_name")',
    0x6B4: 'animscripted (an anim from an origin and angles; sub_824F8F70, sub_8254B398)',
    0x6B8: 'takes an actor off its vehicle (sub_82541430; actor +2532)',
    0x6BC: 'puts an actor on a vehicle seat by tag ("tag_passenger1"; sub_82541530)',
    0x6C0: 'whether the actor is on a vehicle (+2536)',
    0x6C4: "lets go of an actor's vehicle (+2537; sub_82540D78)",
    0x6C8: 'takes an actor off its vehicle, checked',
    0x6CC: 'puts an actor on a vehicle seat, checked ("tag_driver", "tag_gunner", "tag_passenger1")',
    0x6D0: 'an AI state change ("ignoring AI state transition"; sub_8254BD78, sub_823C8DA8)',
    0x6D4: 'an AI state change (sub_823C8DA8)',
    0x6D8: "sets up a turret's arcs and speeds (three floats; a record for +540, sub_82547EC8)",
    0x6DC: 'sub_82547EC8 on the turret (+540)',
    0x6E0: "the actor's +440",
    0x6E4: "the vehicle's name as a script string (+528)",
    0x6E8: 'magicgrenade ("MagicGrenade: None of the sScrMethods worked")',
    0x6EC: "magicgrenade, from the level's player",
    0x6F0: 'throws a grenade from an actor ("grenadethrow"; f1 the fuse)',
    0x6F4: "throws a grenade, from the level's player",
    0x6F8: 'fires a rifle grenade ("m1garand_RG", with g_gravity)',
    0x6FC: 'a turret method (+544)',
    0x700: 'the actor\'s grenade toss ("aiTosser")',
    0x704: 'gettagorigin (by tag; sub_824AF658)',
    0x708: 'gettagorigin by tag name',
    0x70C: 'gettagangles (by tag; sub_82532898)',
    0x710: 'gettagangles by tag name',
    0x714: 'shellshock (a type such as "death", "pain", "default"; "duration %g should be >= 0 and <= 60")',
    0x718: "stopshellshock (clears the player's +1288..+1296)",
    0x71C: 'viewkick ("viewkick: damage %g < 0")',
    0x720: 'nothing (an empty function)',
    0x724: 'nothing (an empty function)',
    0x728: 'a small entity method (no fields beyond the lookup)',
    0x72C: 'a small entity method (no fields beyond the lookup)',
    0x730: "a level-time test of the entity's +616",
    0x734: 'a direction from the entity to a point (sub_82530EA8)',
    0x738: 'sets the entity\'s +500 (called with "prone")',
    0x73C: "sets one of the entity's +632 flags",
    0x740: "sets one of the entity's +632 flags",
    0x744: "sets one of the entity's +632 flags",
    0x748: "sets one of the entity's +632 flags",
    0x74C: "sets the entity's contents and bounds (+228..+264, +324; sub_82513198)",
    0x750: 'setteam ("unknown team \'%s\', should be axis, allies, or neutral"; +884)',
    0x754: 'sets a turret float (+544)',
    0x758: 'sets a turret float (+544)',
    0x75C: 'sets a turret arc (+544)',
    0x760: 'sets a turret arc (+544)',
    0x764: 'a turret method (+544)',
    0x768: 'a turret method (+544)',
    0x76C: 'setmodel (a wrapper of 0x5EC)',
    0x770: 'loads an animation by name (".anim", "generic_human"; sub_824FA748)',
    0x774: 'a localized string by name hash ("_XBOX360", "STRING MISSING"; sub_8252A638)',
    0x310: 'a player profile entry by name, off (sub_823FA7D8 on *(0x825CCD7C): achievements and progress)',
    0x314: 'a player profile entry by name, on (sub_823FA7D8)',
    0x318: 'writes the player profile (sub_823FA528)',
    0x31C: 'campaign progress ("PROGRESS_COMPLETE", "HARDCORE_COMPLETE", "PROGRESS_US"...; sub_823FA8D8)',
    0x320: 'rich presence ("RICH PRESENCE: Setting player on controller %d"; sub_823FB088)',
    0x324: 'a player profile entry by name (sub_823FBB18)',
    0x43C: 'getaiarray, the team held on the thread',
    0x628: 'setmode for a turret ("manual", "auto_ai", "manual_ai", "auto_nonai")',
    0xA88: 'a HUD element\'s text ("localized string")',
    0xA8C: 'setshader on a HUD element ("width %i < 0", "height %i < 0")',
    0xA90: 'settimer ("setTimer")',
    0xA94: 'settimerup ("setTimerUp")',
    0xA98: 'settenthstimer ("setTenthsTimer")',
    0xA9C: 'settenthstimerup ("setTenthsTimerUp")',
    0xAA0: 'setclock ("setClock")',
    0xAA4: 'setclockup ("setClockUp")',
    0xAB0: 'scaleovertime ("scale time %g <= 0")',
    0xAB4: 'moveovertime ("move time %g <= 0")',
    0xABC: "a HUD element's config string (sub_824AABA8)",
    0xAD0: "memset (the title's, also in the table)",
}


def pairs(start, count):
    return [(word(start + 8 * i), word(start + 8 * i + 4)) for i in range(count)]


entries = [('A', o, f) for o, f in pairs(0x825F5638, 265)] + [('B', o, f) for o, f in pairs(0x825F5E80, 222)]

# The generated code: each function's instructions (as the comments give them).
func_re = re.compile(r'^PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\)')
insn_re = re.compile(r'^\s*// (\S+)\s*(.*)$')
code = {}
for path in sorted(glob.glob(os.path.join(ROOT, 'CoD3RecompLib', 'ppc', 'ppc_recomp.*.cpp'))):
    current = None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = func_re.match(line)
        if m:
            current = int(m.group(1), 16)
            code[current] = []
            continue
        m = insn_re.match(line)
        if m and current is not None:
            code[current].append((m.group(1), m.group(2)))

STORES = re.compile(r'^(st[bhwd]|stf[sd]|stw|stmw|stv|stb|sth)')
ALL_SOURCES = re.compile(r'^(cmp|fcmp|mtctr|mtlr|mtcrf|tw|td|dcb)')
reg_re = re.compile(r'\b([rf])(\d+)\b')


def arguments(instructions):
    """The argument registers read before they are written (a linear pass)."""
    written, read = set(), []
    for op, operands in instructions:
        regs = reg_re.findall(operands)
        base = op.rstrip('.+-')
        if base in ('bl', 'blr', 'bctr', 'bctrl', 'b'):
            if base == 'bl':
                written.update({('r', str(i)) for i in range(3, 13)} | {('f', str(i)) for i in range(0, 14)})
            continue
        if STORES.match(base) or ALL_SOURCES.match(base):
            sources, dests = regs, []
        else:
            dests, sources = regs[:1], regs[1:]
        for kind, number in sources:
            n = int(number)
            key = (kind, number)
            if key not in written and ((kind == 'r' and 3 <= n <= 10) or (kind == 'f' and 1 <= n <= 8)) and key not in read:
                read.append(key)
        for key in dests:
            written.add(key)
    ints = sorted(int(n) for k, n in read if k == 'r')
    floats = sorted(int(n) for k, n in read if k == 'f')
    return ' '.join(['r%d' % n for n in ints] + ['f%d' % n for n in floats])


def calls(instructions):
    found = []
    for op, operands in instructions:
        if op == 'bl':
            target = int(operands.split()[0], 16)
            if target in KNOWN and KNOWN[target] not in found:
                found.append(KNOWN[target])
    return ', '.join(found)


def strings(instructions):
    high, found = {}, []
    for op, operands in instructions:
        m = re.match(r'r(\d+),(-?\d+)$', operands.replace(' ', ''))
        if op == 'lis' and m:
            high[int(m.group(1))] = (int(m.group(2)) << 16) & 0xFFFFFFFF
            continue
        m = re.match(r'r(\d+),r(\d+),(-?\d+)$', operands.replace(' ', ''))
        if op == 'addi' and m and int(m.group(2)) in high:
            t = text((high[int(m.group(2))] + int(m.group(3))) & 0xFFFFFFFF)
            if t and t not in found:
                found.append(t)
    return found


# How often the level's code calls through each offset.
level_calls = collections.Counter()
load_re = re.compile(r'^\s*// lwz r(\d+),(\d+)\(r\d+\)')
for path in sorted(glob.glob(os.path.join(ROOT, 'CoD3RecompLib', 'levels', level, 'ppc_recomp.*.cpp'))):
    lines = [l for l in open(path, encoding='utf-8', errors='replace') if l.lstrip().startswith('//')]
    for i, line in enumerate(lines):
        m = load_re.match(line)
        if m and i + 1 < len(lines) and ('mtctr r%s' % m.group(1)) in lines[i + 1]:
            level_calls[int(m.group(2))] += 1

rows = []
for table, offset, function in entries:
    body = code.get(function, [])
    texts = strings(body)
    rows.append((offset, function, table, len(body), level_calls[offset], arguments(body), calls(body),
                 '; '.join(texts[:2])[:90], NAMES.get(offset, '')))
rows.sort(key=lambda r: r[0])

out = []
out.append('| offset | function | table | size | %s calls | reads | calls | text | name |' % level)
out.append('|---|---|---|---|---|---|---|---|---|')
for r in rows:
    out.append('| 0x%X | sub_%08X | %s | %d | %d | %s | %s | %s | %s |' % (
        r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7].replace('|', '/'), r[8]))
result = '\n'.join(out)
if markdown:
    header = ('# The level scripts\' built-ins\n\n'
              'Generated by `scripts/builtins.py` from the executable and the %s level; what the '
              'table is and how it was read is in [scripts.md](scripts.md). "calls" counts the '
              'level\'s calls through each offset; "reads" are the argument registers the function '
              'reads before writing them.\n\n' % level)
    open(markdown, 'w', encoding='utf-8', newline='\n').write(header + result + '\n')
    print('%d entries written to %s' % (len(rows), markdown))
else:
    print(result)
