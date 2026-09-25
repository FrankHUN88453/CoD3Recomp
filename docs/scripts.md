# The level scripts

CoD3 does not run GSC. Its level scripts are C++, written against a script
library Treyarch called "Broc" (`c:\cod\code\script\include\BrocEntity.inl`,
`Threads.inl`, `Strings.inl`, `Arrays.inl`, `Entrypoint.inl`,
`ExtendedEntity.cpp`, `Broc.cpp`), and compiled into each level's DLL: the
`sp/<level>/<level>.dll` that is recompiled into `CoD3RecompLib/levels`.
What the IW engine's scripts would be (`maps\_anim`, `_spawner`, `_drones`)
are C++ namespaces there: the Chambois DLL names `_anim::simple_anim`,
`chambois_drones::drone_combat_cover`, `chambois_event5::gunner_only_player_damage`
and 150 more in its messages. The script threads the recompilation runs
as host fibers (coroutines.cpp) are these functions waiting.

## The built-ins

A level reaches the engine through one table of function pointers, at
`*(0x82A2A2A0)`. At start the executable fills it from two lists of
(byte offset, function) pairs in its data: sub_824BB028 copies 265 from
0x825F5638 and sub_824B9D70 222 from 0x825F5E80; a few slots are set one
by one after. The level's code calls

    lwz r11, offset(table)
    mtctr r11
    bctrl

with the arguments already in registers, the ordinary calling convention:
an entity reference in r3 (the low 14 bits index the table of 4096
entities at 0x82A2DCD0, the rest a number that must match the slot's),
integers and handles in r4 to r10, floats in f1 up. An animation is one
32-bit value: the anim list's index in the table at `*(0x82A2A204)` in
the high half and the entry in the low half.

The built-ins have no names anywhere, in the executable or in a loaded
level: the C++ headers' inline wrappers compiled down to the offsets. They
can be named only by what they do. `scripts/builtins.py` lists all 487
with what can be read off each (the function, how often Chambois calls it,
the argument registers it reads, the functions it calls, the text it
refers to) and the names worked out so far: 445 of the 487, which carry 99% of the
calls Chambois makes. The list is [builtins-table.md](builtins-table.md).

The most used, with what they do:

| offset | calls | does |
|---|---|---|
| 0xAC8 / 0xAC4 / 0xACC | 829 / 209 / 29 | new, delete and free on the script heap |
| 0x11C | 801 | whether an entity reference is valid |
| 0x4 | 800 | println; the print is empty in this build |
| 0x4E0 | 434 | the system clock |
| 0x58 | 278 | endon: a record of type 3 (entity, name) in the event system, which ends the thread when the entity is notified |
| 0x534 / 0xAD4 / 0xAD8 | 213 / 123 / 63 | return 0 |
| 0x10D8 / 0x10DC | 194 / 88 | the event system's records made and let go |
| 0x42C | 188 | gettime, the level's time in milliseconds (level +156) |
| 0x140 / 0x144 / 0x14C / 0x148 | 109 / 44 / 40 / 10 | randomint, randomfloat, randomfloatrange, randomintrange (one linear congruential generator, ×214013 + 2531011) |
| 0x118 | 101 | isalive |
| 0x5DC / 0xF8 | 100 / 89 | play a sound alias; the second also a spoken line |
| 0x6A0 / 0x6A4 | 62 / 5 | useanimtree, clearanimtree |
| 0x258 / 0x250 | 60 / 37 | setdvar, getdvar |
| 0x674 to 0x69C | 15 to 42 each | the animation calls (setanim, setflaggedanim, setanimknob, setanimknoball and their flagged forms, clearanim, getanimtime, getanimlength, animhasnotetrack) |

The animation ones reach the functions in [animation.md](animation.md).
