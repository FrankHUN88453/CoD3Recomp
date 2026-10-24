import importlib.util
from pathlib import Path
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch_recomp", root / "scripts/patch_recomp.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def fixture():
    # Synthetic generated-C shape: six reads, six strides, one step of six.
    read = "ctx.r10.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);\n"
    stride = "ctx.r31.s64 = ctx.r31.s64 + 4992;\n"
    return ('#include "ppc_recomp_shared.h"\n'
            'PPC_FUNC_IMPL(__imp__sub_824046D8) {\n'
            '// preserve before\n'
            'loc_82404850:\n' + (read + stride) * 6 +
            'ctx.r11.s64 = ctx.r11.s64 + 6;\n'
            'ctx.cr6.compare<int32_t>(ctx.r11.s32, 24, ctx.xer);\n'
            'loc_824048E4:\n'
            'ctx.cr6.compare<int32_t>(ctx.r11.s32, 24, ctx.xer);\n'
            '// preserve setup and connect paths\n}\n')


def run_tests():
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        source = folder / "ppc_recomp.0.cpp"
        original = fixture()
        source.write_text(original)
        patch.patch_server_client_scan(folder)
        edited = source.read_text()
        assert edited.count("MpFindFreeServerClient(ctx, base);") == 1
        assert edited.count("void MpFindFreeServerClient(") == 1
        assert "// preserve before" in edited
        assert edited.endswith(original[original.index("loc_824048E4:"):])
        assert "ctx.r31.s64 = ctx.r31.s64 + 4992;" not in edited
        patch.patch_server_client_scan(folder)
        assert source.read_text() == edited
        for broken in (original.replace(' + 6;', ' + 5;'),
                       original.replace('// preserve before', 'goto loc_82404850;')):
            source.write_text(broken)
            try:
                patch.patch_server_client_scan(folder)
            except RuntimeError:
                pass
            else:
                raise AssertionError("unexpected scan must be rejected")
            assert source.read_text() == broken
    print("server client scan patch: idempotence, retained paths and rejection tests passed")

if __name__ == '__main__':
    run_tests()
