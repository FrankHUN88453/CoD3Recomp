"""Makes a recompiled level DLL linkable next to the title's own code.

XenonRecomp names guest functions by address, so a level's sub_89xxxxxx
functions never clash with the title's sub_82xxxxxx ones. What it names the
same in every output are the register save/restore helpers and the function
table, and both would collide at link time. This renames them with the
level's name. Run after XenonRecomp:  python scripts/rename_level.py saint_lo
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: rename_level.py <level>", file=sys.stderr)
        return 1
    level = sys.argv[1]
    folder = ROOT / "CoD3RecompLib" / "levels" / level
    if not folder.is_dir():
        print(f"missing {folder}", file=sys.stderr)
        return 1

    helpers = re.compile(r"__(save|rest)(gprlr|fpr|vmx)_")
    changed = 0
    for path in list(folder.glob("*.cpp")) + list(folder.glob("*.h")):
        text = path.read_text(encoding="ascii")
        updated = helpers.sub(lambda m: f"__{level}_{m.group(1)}{m.group(2)}_", text)
        # The entry point is named _xstart in every output.
        updated = updated.replace("_xstart", f"_{level}_xstart")
        if path.name == "ppc_func_mapping.cpp":
            updated = updated.replace("PPCFuncMapping PPCFuncMappings[]",
                                      f"PPCFuncMapping PPCFuncMappings_{level}[]")
        if updated != text:
            path.write_text(updated, encoding="ascii", newline="\n")
            changed += 1
    print(f"{level}: {changed} files renamed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
