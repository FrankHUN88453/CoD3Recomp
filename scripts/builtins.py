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
    0x258: 'setdvar',
    0x42C: 'gettime (the level\'s time, ms)',
    0x4E0: 'the system clock',
    0x534: 'returns 0',
    0xAC4: 'delete (the script heap, else the general one)',
    0xAC8: 'new (the script heap)',
    0xACC: 'free (the script heap)',
    0xAD4: 'returns 0',
    0x5DC: 'plays a sound alias (sub_824927C0)',
    0xF8: 'plays a sound alias or a spoken line (sub_824B3FF0)',
    0x10D8: 'the event system: a record made (sub_824A6040)',
    0x10DC: 'the event system: a record let go (sub_82499D38)',
    0x58: 'endon (a type 3 record of entity and name in the event system)',
    0x5C: 'whether the event system exists',
    0x1C: 'sprintf (the C runtime\'s)',
    0x124: 'isai (the entity has an actor at +532)',
    0x148: 'randomintrange',
    0x250: 'getdvar (the dvar\'s value at +32)',
    0x264: 'spawn ("unable to spawn \\"%s\\" entity")',
    0x590: 'linkto',
    0x5A0: 'delete (sub_82540D78 frees the entity)',
    0x600: 'sets the entity\'s byte at +624',
    0x618: 'connectpaths',
    0x624: 'a turret method ("is not a turret")',
    0xAAC: 'fadeovertime (a HUD element)',
    0xAC0: 'new, from the fixed pools first',
    0xAD8: 'returns 0',
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
