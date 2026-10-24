import csv
import importlib.util
import pathlib
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("patch_recomp", ROOT / "scripts" / "patch_recomp.py")
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


def write_sites(path, rows):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["kind", "function", "address", "status"])
        writer.writerows(rows)


def test_redirects_exact_sites_and_is_idempotent():
    with tempfile.TemporaryDirectory() as tmp:
        folder = pathlib.Path(tmp)
        sites = folder / "sites.csv"
        source = folder / "ppc_recomp.00.cpp"
        source.write_text(
            '#include "ppc_recomp_shared.h"\n'
            "PPC_FUNC_IMPL(__imp__sub_00000001) {\n"
            "\t// addi r29,r4,4116\n\tctx.r29.s64 = ctx.r4.s64 + 4116;\n"
            "\t// addi r3,r11,4112\n\tctx.r3.s64 = ctx.r11.s64 + 4112;\n}\n"
            "PPC_FUNC_IMPL(__imp__sub_00000002) {\n"
            "\t// addi r7,r3,4124\n\tctx.r7.s64 = ctx.r3.s64 + 4124;\n}\n",
            encoding="ascii",
        )
        write_sites(sites, [
            ["session_player_offset", "00000001", "00000010", "todo"],
            ["session_player_offset", "00000001", "00000020", "todo"],
            ["session_player_offset", "00000002", "00000030", "todo"],
        ])
        PATCH.patch_session_players(folder, sites)
        patched = source.read_text(encoding="ascii")
        assert patched.count("MpSessionPlayerAddress(base,") == 3
        assert "ctx.r29.u64 = MpSessionPlayerAddress(base, ctx.r4.u32, 4116u);" in patched
        assert "ctx.r3.u64 = MpSessionPlayerAddress(base, ctx.r11.u32, 4112u);" in patched
        assert "ctx.r7.u64 = MpSessionPlayerAddress(base, ctx.r3.u32, 4124u);" in patched
        PATCH.patch_session_players(folder, sites)
        assert source.read_text(encoding="ascii") == patched


def test_rejects_missing_site_before_writing():
    with tempfile.TemporaryDirectory() as tmp:
        folder = pathlib.Path(tmp)
        sites = folder / "sites.csv"
        source = folder / "ppc_recomp.00.cpp"
        original = (
            'PPC_FUNC_IMPL(__imp__sub_00000001) {\n'
            "\t// addi r3,r4,4112\n\tctx.r3.s64 = ctx.r4.s64 + 4112;\n}\n"
        )
        source.write_text(original, encoding="ascii")
        write_sites(sites, [
            ["session_player_offset", "00000001", "00000010", "todo"],
            ["session_player_offset", "00000001", "00000020", "todo"],
        ])
        try:
            PATCH.patch_session_players(folder, sites)
        except RuntimeError:
            pass
        else:
            raise AssertionError("expected incomplete site catalogue to be rejected")
        assert source.read_text(encoding="ascii") == original


if __name__ == "__main__":
    test_redirects_exact_sites_and_is_idempotent()
    test_rejects_missing_site_before_writing()
    print("session player patch tests: 2 passed")
