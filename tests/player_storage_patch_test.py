"""Guard the three relocation seams and reject incomplete generated inputs."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch_recomp", ROOT / "scripts/patch_recomp.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

class PlayerStoragePatchTest(unittest.TestCase):
    def fixture(self, folder):
        for file, functions in [("82", [("82506540", "r3")]),
                                ("90", [("82593FA8", "r29"), ("82595118", "r11")])]:
            text = '#include "ppc_recomp_shared.h"\n'
            for fn, dst in functions:
                text += (f'PPC_FUNC_IMPL(__imp__sub_{fn}) {{\n'
                         '\tctx.r11.s64 = -2101411840;\n'
                         f'\tctx.{dst}.s64 = ctx.r11.s64 + 8656;\n'
                         '\tctx.r10.s64 = 24;\n}\n')
            text += 'PPC_FUNC_IMPL(__imp__sub_82594FD0) {\n\tctx.r11.s64 = -2101346304;\n\tctx.r11.s64 = ctx.r11.s64 + 14928;\n}\n'
            (folder / f"ppc_recomp.{file}.cpp").write_text(text)

    def test_relocation_is_idempotent_and_keeps_limits_and_neighbor(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); self.fixture(p)
            patch.patch_player_records(p)
            first = {f.name: f.read_bytes() for f in p.iterdir()}
            patch.patch_player_records(p)
            self.assertEqual(first, {f.name: f.read_bytes() for f in p.iterdir()})
            combined = ''.join(f.read_text() for f in p.iterdir())
            self.assertEqual(combined.count('= MpPlayerRecords(base);'), 3)
            self.assertEqual(combined.count('ctx.r10.s64 = 24;'), 3)
            self.assertEqual(combined.count('ctx.r11.s64 + 14928;'), 2)

    def test_missing_site_does_not_partially_patch_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); self.fixture(p)
            f = p / 'ppc_recomp.90.cpp'
            f.write_text(f.read_text().replace('ctx.r11.s64 + 8656;', 'ctx.r11.s64 + 8657;'))
            before = {f.name: f.read_bytes() for f in p.iterdir()}
            with self.assertRaises(RuntimeError): patch.patch_player_records(p)
            self.assertEqual(before, {f.name: f.read_bytes() for f in p.iterdir()})

    def test_changed_upper_half_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); self.fixture(p)
            f = p / 'ppc_recomp.82.cpp'
            f.write_text(f.read_text().replace('-2101411840', '-2101346304'))
            with self.assertRaises(RuntimeError): patch.patch_player_records(p)

if __name__ == '__main__': unittest.main()
