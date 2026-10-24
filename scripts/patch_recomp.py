"""Patches the recompiled code where the recompiler's model does not reach.

XenonRecomp turns every `blr` into a return and can only enter a function at
its start. The title's script threads are coroutines that resume by loading
their registers and branching to wherever they left off, which is neither.
The runtime runs them on host fibers (CoD3Host/coroutines.cpp), and these
two patches are the seams:

  main    The engine's resume point at the end of sub_824A6150: the branch
          into the thread becomes Coroutines::Resume, keyed by the thread
          object the function was called with.
  levels  A level's yield functions call the engine's suspend through the
          interface table (slots 76 and 80) and never expect it to return;
          the recompiler carried on into the next function's code after the
          call. A return is put after each such call, so that a resumed
          thread returns to the script that yielded.

Run after XenonRecomp:  python scripts/patch_recomp.py main
                        python scripts/patch_recomp.py saint_lo
Both are idempotent.
"""
import pathlib
import re
import sys
import csv
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parent.parent
TAB = "\t"


def patch_main(folder=ROOT / "CoD3RecompLib" / "ppc", resume="824A6150", start_fn="823C0B98", label="main") -> int:
    for path in folder.glob("ppc_recomp.*.cpp"):
        text = path.read_text(encoding="ascii")
        start = text.find(f"PPC_FUNC_IMPL(__imp__sub_{resume})")
        if start < 0:
            continue
        if "Coroutines::Resume" in text:
            print(f"{label}: already patched")
            return 0
        end = text.find("\n}\n", start)
        body = text[start:end]

        switch = TAB + "ctx.r1.u64 = ctx.r11.u64 | ctx.r10.u64;\n"
        at = body.find(switch)
        if at < 0:
            print("main: the stack switch in sub_824A6150 was not found", file=sys.stderr)
            return 1
        key = TAB + "const uint32_t coroutineKey = PPC_LOAD_U32(ctx.r1.u32 + 436);\n"
        body = body[:at] + key + body[at:]

        # The engine's own registers, kept across the thread's run: a thread
        # that finishes returns, on the console, through the frames of the
        # call that began it, into the code after that call with the
        # engine's registers as they were. Here that is a jump to the same
        # place with the same registers.
        body = body[:at] + TAB + "const PPCContext engineRegisters = ctx;\n" + body[at:]
        at += len(TAB + "const PPCContext engineRegisters = ctx;\n")

        # The first return after the switch is the branch into the thread.
        ret = body.find(TAB + "return;\n", at + len(key) + len(switch))
        if ret < 0:
            print("main: the branch after the stack switch was not found", file=sys.stderr)
            return 1
        body = (body[:ret]
                + TAB + "Coroutines::Resume(ctx, base, coroutineKey);\n"
                + TAB + "if (Coroutines::Finished(coroutineKey)) { ctx = engineRegisters; ctx.r1.u32 = engineFrame - 8192; goto loc_thread_finished; }\n"
                + body[ret:])

        # The thread's first run: the engine calls its start on the stack
        # region it reserved, and the thread's first yield longjmps back. That
        # call runs on the thread's fiber from the beginning.
        birth = TAB + f"sub_{start_fn}(ctx, base);\n"
        at = body.find(birth)
        if at < 0:
            print("main: the thread start call in sub_824A6150 was not found", file=sys.stderr)
            return 1
        body = (body[:at] + TAB + "Coroutines::Begin(ctx, base, PPC_LOAD_U32(ctx.r1.u32 + 8192 + 436), sub_" + start_fn + ");\n"
                + "loc_thread_finished:\n"
                + body[at + len(birth):])

        # The frame's own stack pointer, from the prologue.
        prologue = TAB + "ctx.r1.u32 = ea;\n"
        at = body.find(prologue)
        if at < 0:
            print("main: the prologue of sub_824A6150 was not found", file=sys.stderr)
            return 1
        body = body[:at + len(prologue)] + TAB + "const uint32_t engineFrame = ctx.r1.u32;\n" + body[at + len(prologue):]

        declaration = ("namespace Coroutines {\n"
                       "    void Resume(::PPCContext& ctx, uint8_t* base, uint32_t key);\n"
                       "    void Begin(::PPCContext& ctx, uint8_t* base, uint32_t key, PPCFunc* entry);\n"
                       "    bool Finished(uint32_t key);\n"
                       "}\n")
        text = text[:start] + body + text[end:]
        text = text.replace('#include "ppc_recomp_shared.h"\n',
                            '#include "ppc_recomp_shared.h"\n' + declaration, 1)
        path.write_text(text, encoding="ascii", newline="\n")
        print(f"{label}: patched {path.name}")
        return 0
    print("main: sub_824A6150 was not found", file=sys.stderr)
    return 1


YIELD = re.compile(
    r"(\tctx\.r11\.s64 = (-?\d+);\n(?:\t//[^\n]*\n)?"
    r"\tctx\.r11\.s64 = ctx\.r11\.s64 \+ (-?\d+);\n(?:\t//[^\n]*\n)?"
    r"\tctx\.r11\.u64 = PPC_LOAD_U32\(ctx\.r11\.u32 \+ (?:76|80)\);\n(?:\t//[^\n]*\n)?"
    r"\tctx\.ctr\.u64 = ctx\.r11\.u64;\n(?:\t//[^\n]*\n)?"
    r"\tctx\.lr = 0x[0-9A-F]+;\n"
    r"\tPPC_CALL_INDIRECT_FUNC\(ctx\.ctr\.u32\);\n)(?!\treturn;   // the suspend)")


def patch_level(name: str, folder=None) -> int:
    folder = folder or ROOT / "CoD3RecompLib" / "levels" / name
    if not folder.is_dir():
        print(f"{name}: missing {folder}", file=sys.stderr)
        return 1
    sites = 0
    tables = set()
    for path in folder.glob("ppc_recomp.*.cpp"):
        text = path.read_text(encoding="ascii")

        def replacement(match: re.Match) -> str:
            nonlocal sites
            tables.add((int(match.group(2)) + int(match.group(3))) & 0xFFFFFFFF)
            sites += 1
            return match.group(1) + TAB + "return;   // the suspend does not return; a resumed thread returns to its caller\n"

        updated = YIELD.sub(replacement, text)
        if updated != text:
            path.write_text(updated, encoding="ascii", newline="\n")
    if sites == 0:
        print(f"{name}: no yield sites found (already patched, or the level has none)")
    elif len(tables) != 1:
        print(f"{name}: yield sites use {len(tables)} different tables, which is not expected", file=sys.stderr)
        return 1
    else:
        print(f"{name}: {sites} yield sites patched, interface table at {next(iter(tables)):#x}")
    return 0


# The graphics library's cuts of addresses to 29 bits (`clrlwi rX,rY,3`,
# generated as `ctx.rX.u64 = ctx.rY.u32 & 0x1FFFFFFF;`), each routed through
# GpuMask (CoD3Host/gpu_address.cpp) with its function and its order there:
# the same result, counted, to find which of them only ever see addresses
# (the first step of graphics memory past 512 MB).
WINDOW = re.compile(r"^(\t// addis (r\d+),(r\d+),-16384\n)\tctx\.(r\d+)\.s64 = ctx\.(r\d+)\.s64 \+ -1073741824;$", re.M)
LIBRARY = (0x822A0000, 0x822E0000)   # the Xbox graphics library in the multiplayer executable
MASK = re.compile(r"^(\t// clrlwi (r\d+),(r\d+),3\n)\tctx\.(r\d+)\.u64 = ctx\.(r\d+)\.u32 & 0x1FFFFFFF;$", re.M)
FUNC = re.compile(r"^PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]{8})\)", re.M)
# The library's other cuts to 29 bits: `rlwinm rX,rY,0,3,22/29/30`, the
# address with its top three bits and some low ones cleared (a processor
# address made physical, then (address >> 20) + 0x200 & 0x1000 added: the
# 0xE0000000 window's page), written into the GPU's command packets.
BITS = re.compile(r"^(\t// rlwinm (r\d+),(r\d+),0,3,(?:22|29|30)\n)\tctx\.(r\d+)\.u64 = __builtin_rotateleft64\(ctx\.(r\d+)\.u32 \| \(ctx\.(r\d+)\.u64 << 32\), 0\) & (0x1FFFF[0-9A-F]{3});$", re.M)


def patch_gpu_masks(folder) -> int:
    total = 0
    for path in sorted(folder.glob("ppc_recomp.*.cpp")):
        text = path.read_text(encoding="ascii")
        if "GpuMask(" in text:
            continue
        starts = [(m.start(), m.group(1)) for m in FUNC.finditer(text)]
        counts = {}
        out = []
        last = 0
        for m in MASK.finditer(text):
            if m.group(2) != m.group(4) or m.group(3) != m.group(5):
                continue
            owner = None
            for at, name in starts:
                if at > m.start():
                    break
                owner = name
            if owner is None:
                continue
            place = counts.get(owner, 0)
            counts[owner] = place + 1
            out.append(text[last:m.start()])
            out.append(f"{m.group(1)}\tctx.{m.group(4)}.u64 = GpuMask(ctx.{m.group(5)}.u32, 0x{owner}u, {place}u);")
            last = m.end()
        out.append(text[last:])
        text = "".join(out)
        # Its conversions back (`addis rX,rY,-16384`: a GPU address to the
        # processor's window at 0xC0000000), in the library's functions.
        starts = [(m.start(), m.group(1)) for m in FUNC.finditer(text)]
        def window(m):
            owner = None
            for at, name in starts:
                if at > m.start():
                    break
                owner = name
            if owner is None or not (LIBRARY[0] <= int(owner, 16) < LIBRARY[1]) or m.group(2) != m.group(4) or m.group(3) != m.group(5):
                return m.group(0)
            counts["window"] = counts.get("window", 0) + 1
            return f"{m.group(1)}\tctx.{m.group(4)}.s64 = GpuWindow(ctx.{m.group(5)}.s64);"
        text = WINDOW.sub(window, text)
        starts = [(m.start(), m.group(1)) for m in FUNC.finditer(text)]
        def bits(m):
            owner = None
            for at, name in starts:
                if at > m.start():
                    break
                owner = name
            if owner is None or not (LIBRARY[0] <= int(owner, 16) < LIBRARY[1]) or m.group(2) != m.group(4) or m.group(3) != m.group(5) or m.group(5) != m.group(6):
                return m.group(0)
            counts["bits"] = counts.get("bits", 0) + 1
            return f"{m.group(1)}\tctx.{m.group(4)}.u64 = GpuMaskBits(ctx.{m.group(5)}.u32, {m.group(7)}u);"
        text = BITS.sub(bits, text)
        if not counts:
            continue
        text = text.replace('#include "ppc_recomp_shared.h"\n',
                            '#include "ppc_recomp_shared.h"\nuint32_t GpuMask(uint32_t value, uint32_t function, uint32_t place);\nint64_t GpuWindow(int64_t value);\nuint32_t GpuMaskBits(uint32_t value, uint32_t mask);\n', 1)
        path.write_text(text, encoding="ascii", newline="\n")
        total += sum(counts.values())
    print(f"gpu masks: {total} address cuts and window conversions routed through GpuMask and GpuWindow")
    return 0


def patch_player_records(folder):
    """Relocate the three proven absolute gclient bases; retain 24 loops.

    82594FD0 is a DIFFERENT array (stride 6224) starting at the old end.
    Relative users read level.clients/entity+512 and follow the new base.
    Validate every expected site before writing any generated source.
    """
    sites = [("ppc_recomp.82.cpp", "82506540", "r3", "r11"),
             ("ppc_recomp.90.cpp", "82593FA8", "r29", "r11"),
             ("ppc_recomp.90.cpp", "82595118", "r11", "r11")]
    pending = {}
    for name, function, dst, src in sites:
        path = folder / name
        text = pending.get(path, path.read_text(encoding="ascii"))
        start = text.index(f"PPC_FUNC_IMPL(__imp__sub_{function})")
        end = text.index("\n}\n", start)
        body = text[start:end]
        old = f"ctx.{dst}.s64 = ctx.{src}.s64 + 8656;"
        new = f"ctx.{dst}.s64 = MpPlayerRecords(base); // player storage 0.5.208"
        if body.count(new) == 1 and old not in body:
            continue
        if body.count(old) != 1:
            raise RuntimeError(f"player storage: unexpected base in {function}")
        # Ensure this really is the 0x82BF upper half, not another +8656.
        before = body[:body.index(old)]
        if f"ctx.{src}.s64 = -2101411840;" not in before[-240:]:
            raise RuntimeError(f"player storage: upper half changed in {function}")
        body = body.replace(old, new, 1)
        text = text[:start] + body + text[end:]
        declaration = "int64_t MpPlayerRecords(uint8_t* base);\n"
        if declaration not in text:
            text = text.replace('#include "ppc_recomp_shared.h"\n', '#include "ppc_recomp_shared.h"\n' + declaration, 1)
        pending[path] = text
    for path, text in pending.items():
        path.write_text(text, encoding="ascii", newline="\n")
    print("player storage: 3 bases relocated; gameplay limit remains 24")
    return 0


def patch_server_client_storage(folder):
    """Allocate and clear room for 32 server clients without enabling slots.

    sub_825336F0 allocates the list and clears it with the same 0x1D400 size.
    Both constants must become 32 * 4992 (0x27000); all player loops stay 24.
    """
    found = False
    pending = None
    for path in folder.glob("ppc_recomp.*.cpp"):
        text = path.read_text(encoding="ascii")
        start = text.find("PPC_FUNC_IMPL(__imp__sub_825336F0)")
        if start < 0:
            continue
        end = text.index("\n}\n", start)
        body = text[start:end]
        for register in ("r4", "r5"):
            old_high = f"ctx.{register}.s64 = 65536;"
            old_low = f"ctx.{register}.u64 = ctx.{register}.u64 | 54272;"
            new_high = f"ctx.{register}.s64 = 131072;"
            new_low = f"ctx.{register}.u64 = ctx.{register}.u64 | 28672;"
            old_count = body.count(old_high), body.count(old_low)
            new_count = body.count(new_high), body.count(new_low)
            if old_count == (1, 1) and new_count == (0, 0):
                body = body.replace(old_high, new_high, 1).replace(old_low, new_low, 1)
            elif old_count != (0, 0) or new_count != (1, 1):
                raise RuntimeError(f"server client storage: unexpected {register} size in sub_825336F0")
        text = text[:start] + body + text[end:]
        pending = path, text
        found = True
        break
    if not found:
        raise RuntimeError("server client storage: sub_825336F0 was not found")
    path, text = pending
    path.write_text(text, encoding="ascii", newline="\n")
    print("server client storage: allocated and cleared 32 x 4992; active limit remains 24")
    return 0


SESSION_ADDI = re.compile(
    r"(?m)^(?P<indent>\t*)// addi r(?P<dst>\d+),r(?P<src>\d+),(?P<offset>4112|4116|4120|4124)\r?\n"
    r"(?P=indent)ctx\.r(?P=dst)\.s64 = ctx\.r(?P=src)\.s64 \+ (?P=offset);$"
)


def patch_session_players(folder, sites_path=None):
    """Route the 66 catalogued inline session-roster addresses through storage.

    The helper redirects only effective addresses inside the original 24 x
    784-byte roster; the session object's neighboring fields are untouched.
    Keep the gameplay limit at 24 until the sentinel, loops and UI are ready.
    """
    sites_path = sites_path or ROOT / "docs" / "player-limit" / "sites.csv"
    targets = defaultdict(list)
    with open(sites_path, newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            if row.get("kind") == "session_player_offset":
                targets[row["function"].upper()].append(int(row["address"], 16))
    if not targets:
        raise RuntimeError("session player storage: no catalogued offset sites")
    for addresses in targets.values():
        addresses.sort()

    pending = {}
    found_functions = set()
    for path in folder.glob("ppc_recomp.*.cpp"):
        text = path.read_text(encoding="ascii")
        for function, addresses in targets.items():
            start = text.find(f"PPC_FUNC_IMPL(__imp__sub_{function})")
            if start < 0:
                continue
            end = text.index("\n}\n", start)
            body = text[start:end]
            matches = list(SESSION_ADDI.finditer(body))
            redirected = body.count("MpSessionPlayerAddress(base,")
            if redirected == len(addresses) and not matches:
                found_functions.add(function)
                continue
            if len(matches) != len(addresses) or redirected:
                raise RuntimeError(
                    f"session player storage: {function} has {len(matches)} raw sites, "
                    f"{redirected} patched sites; expected {len(addresses)}"
                )
            for match in reversed(matches):
                indent, dst, src, offset = (match.group(key) for key in ("indent", "dst", "src", "offset"))
                replacement = (
                    f"{indent}// session roster 0.5.213: relocate the inline player entry\n"
                    f"{indent}ctx.r{dst}.u64 = MpSessionPlayerAddress(base, ctx.r{src}.u32, {offset}u);"
                )
                body = body[:match.start()] + replacement + body[match.end():]
            text = text[:start] + body + text[end:]
            pending[path] = text
            found_functions.add(function)

    missing = sorted(set(targets) - found_functions)
    if missing:
        raise RuntimeError("session player storage: functions missing from generated C: " + ", ".join(missing))
    declaration = "uint32_t MpSessionPlayerAddress(uint8_t* base, uint32_t source, uint32_t offset);\n"
    for path, text in list(pending.items()):
        if declaration not in text:
            text = text.replace('#include "ppc_recomp_shared.h"\n',
                                '#include "ppc_recomp_shared.h"\n' + declaration, 1)
            pending[path] = text
    for path, text in pending.items():
        path.write_text(text, encoding="ascii", newline="\n")
    print(f"session player storage: redirected {sum(map(len, targets.values()))} sites in {len(targets)} functions; active limit remains 24")
    return 0


def patch_server_client_scan(folder):
    """Replace the six-wide free-client scan; preserve the active 24 cap.

    Retail sub_824046D8 checks six states per iteration. Merely changing its
    bound to 32 reads slots 32-35. Replace only 82404850..824048E0 and keep
    the existing found/full, setup and connect paths at 824048E4 onward.
    """
    declaration = "void MpFindFreeServerClient(PPCContext& ctx, uint8_t* base);\n"
    marker = "// server client scan 0.5.214: bounded search"
    for path in folder.glob("ppc_recomp.*.cpp"):
        text = path.read_text(encoding="ascii")
        start = text.find("PPC_FUNC_IMPL(__imp__sub_824046D8)")
        if start < 0:
            continue
        end = text.index("\n}\n", start)
        body = text[start:end]
        if marker in body:
            if body.count(marker) != 1 or body.count("MpFindFreeServerClient(ctx, base);") != 1 or declaration not in text:
                raise RuntimeError("server client scan: incomplete patched state")
            return 0
        first, last = body.index("loc_82404850:\n"), body.index("loc_824048E4:\n")
        original = body[first:last]
        if (original.count("ctx.r10.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);") != 6 or
            original.count("ctx.r31.s64 = ctx.r31.s64 + 4992;") != 6 or
            original.count("ctx.r11.s64 = ctx.r11.s64 + 6;") != 1 or
            original.count("ctx.cr6.compare<int32_t>(ctx.r11.s32, 24, ctx.xer);") != 1):
            raise RuntimeError("server client scan: unexpected six-wide scan; no source written")
        for label in re.findall(r"(?m)^(loc_[0-9A-F]+):", original):
            if re.search(r"goto " + re.escape(label) + r";", body[:first] + body[last:]):
                raise RuntimeError("server client scan: external entry into replaced scan")
        replacement = (
            "\t" + marker + "\n"
            "\tMpFindFreeServerClient(ctx, base);\n"
        )
        body = body[:first] + replacement + body[last:]
        text = text[:start] + body + text[end:]
        if declaration not in text:
            include = '#include "ppc_recomp_shared.h"\n'
            if text.count(include) != 1:
                raise RuntimeError("server client scan: generated include changed")
            text = text.replace(include, include + declaration, 1)
        path.write_text(text, encoding="ascii", newline="\n")
        print("server client scan: bounded search installed; active cap remains 24")
        return 0
    raise RuntimeError("server client scan: sub_824046D8 not found")


# 0.5.218: EntityList (*0x82BCC7FC) is a 104-byte object: vtable, the 24
# player entities (+4..+96, client n at +4*(n+1)) and the level entity at
# +100 (sub_825638A0's spawn, stored by sub_82535A18 / sub_82593FA8). Slots
# past 24 would land on +100, so the level pointer moves to +132 in a
# 136-byte object (room for 32 players at +4..+128). Every read below was
# found by following EntityList loads and copies through each function
# (docs/player-limit/entitylist-level-sites.csv); (function, instruction,
# its occurrence in the function, how many the function has). While the
# active cap is 24 the two stores write BOTH +100 and +132, so a read that
# was missed still finds the level entity; the reads use +132.
ENTITY_LIST_LEVEL_READS = [
    ("820B9680", "lwz r3,100(r11)", 0, 1),   # 820B98E0
    ("824278E8", "lwz r11,100(r11)", 0, 1),   # 82427920
    ("82428DC8", "lwz r10,100(r11)", 0, 1),   # 824298EC
    ("82460390", "lwz r11,100(r11)", 0, 1),   # 82460400
    ("824604A8", "lwz r11,100(r11)", 0, 1),   # 82460518
    ("82462588", "lwz r11,100(r11)", 0, 1),   # 82462B14
    ("82463B78", "lwz r30,100(r11)", 0, 1),   # 82463F64
    ("824642D0", "lwz r3,100(r29)", 0, 1),   # 8246441C
    ("824799D8", "lwz r29,100(r11)", 0, 1),   # 82479A74
    ("8247AF70", "lwz r3,100(r11)", 0, 1),   # 8247B138
    ("8247B208", "lwz r28,100(r11)", 0, 1),   # 8247B24C
    ("8248EBC8", "lwz r9,100(r10)", 0, 1),   # 8248EBFC
    ("824AD3A0", "lwz r31,100(r11)", 0, 1),   # 824AD3C0
    ("824AD3F0", "lwz r31,100(r11)", 0, 1),   # 824AD410
    ("824AD6B8", "lwz r31,100(r11)", 0, 2),   # 824AD704
    ("824AD6B8", "lwz r31,100(r11)", 1, 2),   # 824AD744
    ("824AD6B8", "lwz r30,100(r11)", 0, 2),   # 824AD7D4
    ("824AD6B8", "lwz r30,100(r11)", 1, 2),   # 824AD818
    ("824BEA68", "lwz r11,100(r11)", 0, 1),   # 824BEA70
    ("824CA448", "lwz r10,100(r10)", 0, 1),   # 824CA530
    ("824CCAA0", "lwz r11,100(r11)", 0, 1),   # 824CCF60
    ("824CD770", "lwz r31,100(r11)", 0, 1),   # 824CD7CC
    ("824CDA60", "lwz r3,100(r11)", 0, 1),   # 824CDAC4
    ("824CF498", "lwz r11,100(r11)", 0, 1),   # 824CF560
    ("824CFD60", "lwz r5,100(r11)", 0, 1),   # 824CFDA0
    ("824D05D0", "lwz r3,100(r11)", 0, 1),   # 824D06DC
    ("82531860", "lwz r11,100(r11)", 0, 1),   # 825318E4
    ("82533B98", "lwz r11,100(r11)", 0, 1),   # 82533C04
    ("82534200", "lwz r11,100(r11)", 0, 2),   # 825342BC
    ("82534200", "lwz r11,100(r11)", 1, 2),   # 82534380
    ("82573E58", "lwz r11,100(r11)", 0, 1),   # 8257462C
    ("82578AD8", "lwz r11,100(r11)", 0, 1),   # 82578EBC
    ("825862F8", "lwz r11,100(r11)", 0, 1),   # 82586E1C
    ("82587268", "lwz r28,100(r11)", 0, 1),   # 825872B8
    ("8258BFD0", "lwz r11,100(r11)", 0, 1),   # 8258C1D0
    ("8258C6F0", "lwz r11,100(r11)", 0, 1),   # 8258C738
    ("8258C778", "lwz r11,100(r11)", 0, 1),   # 8258CCB0
    ("8258D550", "lwz r11,100(r11)", 0, 1),   # 8258D630
    ("8258F120", "lwz r11,100(r11)", 0, 1),   # 8258F274
    ("82590500", "lwz r25,100(r11)", 0, 1),   # 82590578
]
ENTITY_LIST_LEVEL_STORES = [
    ("82535A18", "stw r3,100(r31)", 0, 1),   # 82535CDC
    ("82593FA8", "stw r11,100(r31)", 0, 1),   # 8259419C
    ("824830E0", "stw r8,100(r3)", 0, 1),   # 82483128
    ("824B1B28", "stw r11,100(r28)", 0, 1),   # 824B1BF8
]
ENTITY_LIST_MARK = "// entity list 0.5.218"


def _function_body(folder, function, cache):
    for path in sorted(folder.glob("ppc_recomp.*.cpp")):
        text = cache.get(path)
        if text is None:
            text = path.read_text(encoding="ascii")
        start = text.find(f"PPC_FUNC_IMPL(__imp__sub_{function})")
        if start >= 0:
            cache[path] = text
            return path, start, text.index("\n}\n", start)
    raise RuntimeError(f"entity list: sub_{function} not found in generated C")


def _asm_lines(body, asm):
    lines = body.split("\n")
    return lines, [i for i, line in enumerate(lines) if line.strip() == "// " + asm]


def patch_entity_list_level(folder):
    """Move EntityList's level slot from +100 to +132 (object 104 -> 136).

    No change to play: 24 players as before. Prepares room for players
    24..31 at +100..+128. Idempotent; checks every site before writing.
    """
    cache = {}
    edits = defaultdict(list)   # (path, function) -> [(asm, occurrence, total, kind)]
    for function, asm, occurrence, total in ENTITY_LIST_LEVEL_READS:
        edits[function].append((asm, occurrence, total, "read"))
    for function, asm, occurrence, total in ENTITY_LIST_LEVEL_STORES:
        edits[function].append((asm, occurrence, total, "store"))
    edits["825583F8"].append(("li r4,104", 0, 1, "size"))
    edits["824830E0"].append(("li r9,24", 0, 1, "count"))
    expected_marks = {f: len(v) for f, v in edits.items()}
    done = 0
    for function, wanted in edits.items():
        path, start, end = _function_body(folder, function, cache)
        text = cache[path]
        body = text[start:end]
        marks = body.count(ENTITY_LIST_MARK)
        if marks == expected_marks[function]:
            done += 1
            continue
        if marks:
            raise RuntimeError(f"entity list: sub_{function} is partly patched ({marks} of {expected_marks[function]})")
        lines = body.split("\n")
        changes = []
        for asm, occurrence, total, kind in wanted:
            found = [i for i, line in enumerate(lines) if line.strip() == "// " + asm]
            if len(found) != total:
                raise RuntimeError(f"entity list: sub_{function} has {len(found)} '{asm}', expected {total}; nothing written")
            at = found[occurrence]
            code_line = lines[at + 1]
            indent = code_line[:len(code_line) - len(code_line.lstrip("\t"))]
            if kind == "read":
                if code_line.count(" + 100);") != 1 or "PPC_LOAD_U32" not in code_line:
                    raise RuntimeError(f"entity list: sub_{function} '{asm}' is not a +100 load: {code_line.strip()}")
                changes.append((at + 1, [indent + ENTITY_LIST_MARK + ": level slot +132", code_line.replace(" + 100);", " + 132);")]))
            elif kind == "store":
                if code_line.count(" + 100,") != 1 or "PPC_STORE_U32" not in code_line:
                    raise RuntimeError(f"entity list: sub_{function} '{asm}' is not a +100 store: {code_line.strip()}")
                changes.append((at + 1, [code_line, indent + ENTITY_LIST_MARK + ": level slot also at +132", code_line.replace(" + 100,", " + 132,")]))
            elif kind == "size":
                if code_line.strip() != "ctx.r4.s64 = 104;":
                    raise RuntimeError(f"entity list: allocation size changed: {code_line.strip()}")
                changes.append((at + 1, [indent + ENTITY_LIST_MARK + ": 136-byte object", indent + "ctx.r4.s64 = 136;"]))
            elif kind == "count":
                if code_line.strip() != "ctx.r9.s64 = 24;":
                    raise RuntimeError(f"entity list: constructor count changed: {code_line.strip()}")
                changes.append((at + 1, [indent + ENTITY_LIST_MARK + ": clear 32 player slots", indent + "ctx.r9.s64 = 32;"]))
        for index, replacement in sorted(changes, reverse=True):
            lines[index:index + 1] = replacement
        body = "\n".join(lines)
        cache[path] = text[:start] + body + text[end:]
    # Write only after every function checked out.
    written = {p for p in cache if cache[p] is not None}
    for path in written:
        original = path.read_text(encoding="ascii")
        if cache[path] != original:
            path.write_text(cache[path], encoding="ascii", newline="\n")
    if done == len(edits):
        print("entity list: level slot already at +132")
    else:
        print(f"entity list: level slot moved to +132 ({len(ENTITY_LIST_LEVEL_READS)} reads, {len(ENTITY_LIST_LEVEL_STORES)} stores, 136-byte object); active cap remains 24")
    return 0


CAP32_MARK = "// cap32 0.5.240"   # 0.5.241: same mark (sites added, file regenerated each build)
CAP32_LITERALS = {"N": 24, "N-1": 23, "N+1": 25, "N4": 96, "groups6": 4}


def _cap32_expression(kind, cast):
    n = f"{cast}(MpCapClients)"
    return {"N": n, "N-1": f"({n} - 1)", "N+1": f"({n} + 1)", "N4": f"({n} * 4)",
            "groups6": f"(({n} + 5) / 6)"}[kind]


def _cap32_rewrite(kind, code):
    """The generated line for one site with the player count made MpCapClients.

    With MpCapClients 24 every rewritten line computes what the original did.
    """
    stripped = code.strip()
    indent = code[:len(code) - len(code.lstrip("\t"))]
    if kind in CAP32_LITERALS:
        value = CAP32_LITERALS[kind]
        pattern = re.compile(r"(?<![\w.])" + str(value) + r"(?![\w])")
        if len(pattern.findall(stripped)) != 1:
            raise RuntimeError(f"cap32: expected one {value} in: {stripped}")
        cast = "int32_t" if "compare<int32_t>" in stripped else "uint32_t" if "compare<uint32_t>" in stripped else "int64_t"
        return indent + pattern.sub(_cap32_expression(kind, cast), stripped, count=1)
    if kind == "set4992":
        match = re.fullmatch(r"ctx\.(r\d+)\.u64 = ctx\.r\d+\.u64 \| 54272;", stripped)
        if not match:
            raise RuntimeError(f"cap32: unexpected 0x1D400 line: {stripped}")
        return f"{indent}ctx.{match.group(1)}.u64 = uint64_t(MpCapClients) * 4992u;"
    if kind == "delta2992":
        if stripped.count(" + 9076;") != 1:
            raise RuntimeError(f"cap32: unexpected record loop end: {stripped}")
        return indent + stripped.replace(" + 9076;", " + (9076 + (int64_t(MpCapClients) - 24) * 2992);")
    if kind == "guard100":
        if not (stripped.startswith("PPC_STORE_U32(") and " + 100," in stripped):
            raise RuntimeError(f"cap32: unexpected EntityList +100 store: {stripped}")
        return f"{indent}if (MpCapClients <= 24) {stripped}"
    if kind == "P504":
        match = re.fullmatch(r"ctx\.(r\d+)\.s64 = ctx\.(r\d+)\.s64 \+ (-32128|-32032);", stripped)
        if not match:
            raise RuntimeError(f"cap32: unexpected pool address: {stripped}")
        return (f"{indent}ctx.{match.group(1)}.u64 = MpPool504(base, uint32_t(ctx.{match.group(2)}.s64 + {match.group(3)}));")
    if kind == "P504d":
        match = re.fullmatch(r"PPC_STORE_U32\(ctx\.(r\d+)\.u32 \+ (96|100|104|108|112|116), (ctx\.r\d+\.u32)\);", stripped)
        if not match:
            raise RuntimeError(f"cap32: unexpected pool index store: {stripped}")
        offset = match.group(2)
        return f"{indent}PPC_STORE_U32(ctx.{match.group(1)}.u32 + (MpCapClients > 24 ? {int(offset) + 48} : {offset}), {match.group(3)});"
    if kind == "SINK337":
        # 0.5.242: 82580B40's "who damaged me" table (victim record +1348,
        # 24 words, index = the attacker's EntityList slot). An attacker past
        # the first 24 gets index -1 and his damage was added to record +1344.
        # With more than 24 players that goes to a spare word instead.
        if stripped != "ctx.r11.s64 = ctx.r10.s64 + 337;":
            raise RuntimeError(f"cap32: unexpected damage table index: {stripped}")
        return (f"{indent}ctx.r11.s64 = (MpCapClients > 24 && ctx.r10.s32 < 0) ? "
                f"int64_t((MpDamageSink(base) - ctx.r9.u32) >> 2) : ctx.r10.s64 + 337;")
    if kind in ("ROWFULL0", "ROWFULLNE", "ROWFULLNZ"):
        # 0.5.248: the scoreboards (823A0180, 8239EC38) keep 24 rows (r17 is
        # the row count). Their player loops now walk MpCapClients entities;
        # once 24 rows are filled the rest are skipped: ROWFULL0 makes the
        # entity look empty, ROWFULLNE makes "same team" fail and ROWFULLNZ
        # makes "only my team" true. Never true with 24 players.
        full = "(MpCapClients > 24 && ctx.r17.u32 >= 24)"
        match = re.fullmatch(r"ctx\.cr6\.compare<(u?int32_t)>\(ctx\.(r\d+)\.([us]32), (0|ctx\.(r\d+)\.s32), ctx\.xer\);", stripped)
        if not match:
            raise RuntimeError(f"cap32: unexpected scoreboard compare: {stripped}")
        cast, left, field, right = match.group(1), match.group(2), match.group(3), match.group(4)
        if kind == "ROWFULL0" and right == "0" and cast == "uint32_t":
            return f"{indent}ctx.cr6.compare<uint32_t>({full} ? 0u : ctx.{left}.u32, 0, ctx.xer);"
        if kind == "ROWFULLNZ" and right == "0" and cast == "uint32_t":
            return f"{indent}ctx.cr6.compare<uint32_t>({full} ? 1u : ctx.{left}.u32, 0, ctx.xer);"
        if kind == "ROWFULLNE" and right != "0" and cast == "int32_t":
            return (f"{indent}ctx.cr6.compare<int32_t>(ctx.{left}.s32, {full} ? ctx.{left}.s32 + 1 : {right}, ctx.xer);")
        raise RuntimeError(f"cap32: {kind} does not fit: {stripped}")
    if kind in ("SIDE12W", "SIDE12R"):
        # 0.5.251: the overhead-icon table (824F9410 writes, 82507D28 draws):
        # 24 entries of 12 bytes at HUD object +76, one per EntityList slot
        # 1..24, with the object's other fields right after (no room for 8
        # more). Entries 24..31 go to a side block of our own per object
        # (MpHudSide): when the loop index reaches 96 (slot 24) the entry
        # pointer jumps there instead of +12. Never taken with 24 players.
        index, pointer, obj, extra = {"SIDE12W": ("r17", "r27", "r14", 0),
                                      "SIDE12R": ("r24", "r29", "r25", 1)}[kind]
        if stripped != f"ctx.{pointer}.s64 = ctx.{pointer}.s64 + 12;":
            raise RuntimeError(f"cap32: unexpected icon entry step: {stripped}")
        return (f"{indent}ctx.{pointer}.u64 = (MpCapClients > 24 && ctx.{index}.u32 == 96u) ? "
                f"uint64_t(MpHudSide(base, ctx.{obj}.u32) + {extra}u) : uint64_t(ctx.{pointer}.u32 + 12u);")
    if kind.startswith("Rend"):
        offset = int(kind[4:])
        match = re.fullmatch(r"ctx\.(r\d+)\.s64 = ctx\.(r\d+)\.s64 \+ " + str(offset) + ";", stripped)
        if not match or offset != 22928:
            raise RuntimeError(f"cap32: unexpected roster end: {stripped}")
        return f"{indent}ctx.{match.group(1)}.u64 = MpSessionRosterEnd(base, ctx.{match.group(2)}.u32);"
    if kind.startswith("R"):
        offset = int(kind[1:])
        match = re.fullmatch(r"ctx\.(r\d+)\.s64 = ctx\.(r\d+)\.s64 \+ " + str(offset) + ";", stripped)
        if not match:
            raise RuntimeError(f"cap32: unexpected roster address: {stripped}")
        return f"{indent}ctx.{match.group(1)}.u64 = MpSessionPlayerAddress(base, ctx.{match.group(2)}.u32, {offset}u);"
    raise RuntimeError(f"cap32: unknown kind {kind}")


def patch_cap32(folder, sites_path=None):
    """Make the reviewed player-limit sites follow MpCapClients (24 or 32).

    docs/player-limit/cap32-sites.csv: the address in the retail code, its
    function, the asm text, its occurrence among the function's identical
    asm lines, how many there are, what it becomes (kind) and why. Runs before
    the probe; every site checked before anything is written. MpCapClients
    is 24 unless COD3_PLAYERS=32 (mp_fixes.cpp), so play is unchanged by
    default.
    """
    sites_path = sites_path or ROOT / "docs" / "player-limit" / "cap32-sites.csv"
    by_function = defaultdict(list)
    with open(sites_path, newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            by_function[row["function"]].append(row)
    cache, where = {}, {}
    for path in sorted(folder.glob("ppc_recomp.*.cpp")):
        text = path.read_text(encoding="ascii")
        for name in re.findall(r"PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]+)\)", text):
            if name in by_function:
                where[name] = path
                cache[path] = text
    missing = sorted(set(by_function) - set(where))
    if missing:
        raise RuntimeError("cap32: functions missing from generated C: " + ", ".join(missing[:10]))
    done = changed = 0
    for function, rows in by_function.items():
        path = where[function]
        text = cache[path]
        start = text.index(f"PPC_FUNC_IMPL(__imp__sub_{function})")
        end = text.index("\n}\n", start)
        body = text[start:end]
        marks = body.count(CAP32_MARK)
        if marks == len(rows):
            done += 1
            continue
        if marks:
            raise RuntimeError(f"cap32: sub_{function} is partly patched ({marks} of {len(rows)})")
        lines = body.split("\n")
        edits = []
        for row in rows:
            asm = row["asm"]
            found = [i for i, line in enumerate(lines) if line.strip() == "// " + asm]
            if len(found) != int(row["total"]):
                raise RuntimeError(f"cap32: sub_{function} has {len(found)} '{asm}', expected {row['total']}; nothing written")
            at = found[int(row["occurrence"])] + 1
            code = lines[at]
            indent = code[:len(code) - len(code.lstrip("\t"))]
            edits.append((at, [f"{indent}{CAP32_MARK}: {row['kind']} {row['address']} {row['note']}".rstrip(),
                               _cap32_rewrite(row["kind"], code)]))
        for index, replacement in sorted(edits, reverse=True):
            lines[index:index + 1] = replacement
        cache[path] = text[:start] + "\n".join(lines) + text[end:]
        changed += 1
    declarations = ("extern uint32_t MpCapClients;\n"
                    "uint32_t MpSessionPlayerAddress(uint8_t* base, uint32_t source, uint32_t offset);\n"
                    "uint32_t MpSessionRosterEnd(uint8_t* base, uint32_t object);\n"
                    "uint32_t MpPool504(uint8_t* base, uint32_t address);\n"
                    "uint32_t MpDamageSink(uint8_t* base);\n"
                    "uint32_t MpHudSide(uint8_t* base, uint32_t object);\n")
    include = '#include "ppc_recomp_shared.h"\n'
    for path, text in cache.items():
        if CAP32_MARK not in text:
            continue
        # 0.5.242: each declaration on its own, so a file patched by an
        # earlier build still gets the ones added since (MpDamageSink).
        missing_lines = "".join(line + "\n" for line in declarations.splitlines() if line not in text)
        if missing_lines:
            if text.count(include) != 1:
                raise RuntimeError(f"cap32: generated include changed in {path.name}")
            cache[path] = text.replace(include, include + missing_lines, 1)
    for path, text in cache.items():
        if text != path.read_text(encoding="ascii"):
            path.write_text(text, encoding="ascii", newline="\n")
    total = sum(map(len, by_function.values()))
    print(f"cap32: {total} sites in {len(by_function)} functions ({changed} patched now, {done} already); player count = MpCapClients (24 unless COD3_PLAYERS=32)")
    return 0


CAP_PROBE_MARK = "// cap probe 0.5.239"


def patch_cap_probe(folder, sites_path=None):
    """Count every run of the 911 instructions that compare with or load 23-25.

    No change to play. Each site in docs/player-limit/cap-probe-sites.csv
    (function, asm text, its occurrence among the function's identical asm
    lines, how many it has) gets MpCapProbe(id, compared register) before its
    code line (mp_fixes.cpp writes cap-probe.csv). Runs last: the occurrence
    counts were taken from the generated C after the patches above. Every
    site is checked before anything is written; a mismatch stops the build.
    """
    sites_path = sites_path or ROOT / "docs" / "player-limit" / "cap-probe-sites.csv"
    by_function = defaultdict(list)
    with open(sites_path, newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            by_function[row["function"]].append(row)
    cache, where = {}, {}
    for path in sorted(folder.glob("ppc_recomp.*.cpp")):
        text = path.read_text(encoding="ascii")
        for name in re.findall(r"PPC_FUNC_IMPL\(__imp__sub_([0-9A-F]+)\)", text):
            if name in by_function:
                where[name] = path
                cache[path] = text
    missing = sorted(set(by_function) - set(where))
    if missing:
        raise RuntimeError("cap probe: functions missing from generated C: " + ", ".join(missing[:10]))
    done = changed = 0
    for function, rows in by_function.items():
        path = where[function]
        text = cache[path]
        start = text.index(f"PPC_FUNC_IMPL(__imp__sub_{function})")
        end = text.index("\n}\n", start)
        body = text[start:end]
        marks = body.count(CAP_PROBE_MARK)
        if marks == len(rows):
            done += 1
            continue
        if marks:
            raise RuntimeError(f"cap probe: sub_{function} is partly patched ({marks} of {len(rows)})")
        lines = body.split("\n")
        inserts = []
        for row in rows:
            asm = row["asm"]
            found = [i for i, line in enumerate(lines) if line.strip() == "// " + asm]
            if len(found) != int(row["total"]):
                raise RuntimeError(f"cap probe: sub_{function} has {len(found)} '{asm}', expected {row['total']}; nothing written")
            at = found[int(row["occurrence"])]
            while lines[at + 1].strip().startswith("//"):   # a cap32 note before the code line
                at += 1
            code_line = lines[at + 1]
            indent = code_line[:len(code_line) - len(code_line.lstrip("\t"))]
            mnemonic = asm.split()[0]
            if mnemonic in ("cmpwi", "cmplwi"):
                if "compare<" not in code_line or f"ctx.{row['reg']}." not in code_line:
                    raise RuntimeError(f"cap probe: sub_{function} '{asm}' code changed: {code_line.strip()}")
                value = f"ctx.{row['reg']}.u32"
            else:
                if not code_line.strip().startswith("ctx."):
                    raise RuntimeError(f"cap probe: sub_{function} '{asm}' code changed: {code_line.strip()}")
                value = "0u"
            inserts.append((at + 1, f"{indent}MpCapProbe({row['id']}u, {value}); {CAP_PROBE_MARK}"))
        for index, line in sorted(inserts, reverse=True):
            lines.insert(index, line)
        cache[path] = text[:start] + "\n".join(lines) + text[end:]
        changed += 1
    declaration = "void MpCapProbe(uint32_t id, uint32_t value);\n"
    include = '#include "ppc_recomp_shared.h"\n'
    for path, text in cache.items():
        if CAP_PROBE_MARK in text and declaration not in text:
            if text.count(include) != 1:
                raise RuntimeError(f"cap probe: generated include changed in {path.name}")
            cache[path] = text.replace(include, include + declaration, 1)
    for path, text in cache.items():
        if text != path.read_text(encoding="ascii"):
            path.write_text(text, encoding="ascii", newline="\n")
    total = sum(map(len, by_function.values()))
    print(f"cap probe: {total} sites in {len(by_function)} functions ({changed} patched now, {done} already); no change to play")
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_recomp.py main|<level>", file=sys.stderr)
        return 1
    if sys.argv[1] == "main":
        return patch_main()
    if sys.argv[1] == "mp":
        # The multiplayer executable has both seams in one module: the
        # engine's resume point (the same function as the single player
        # one's, instruction for instruction, at another address) and the
        # game types' script code, which yields through the interface table
        # the way a level's does.
        folder = ROOT / "CoD3RecompLib" / "ppc_mp"
        return patch_main(folder, "824C5298", "823911E8", "mp") or patch_level("mp", folder) or patch_gpu_masks(folder) or patch_player_records(folder) or patch_server_client_storage(folder) or patch_session_players(folder) or patch_server_client_scan(folder) or patch_entity_list_level(folder) or patch_cap32(folder) or patch_cap_probe(folder)
    return patch_level(sys.argv[1])


if __name__ == "__main__":
    sys.exit(main())
