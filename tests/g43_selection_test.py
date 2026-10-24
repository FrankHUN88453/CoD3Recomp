"""Check saved selection compatibility against the unmodified Git checkpoint."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]

def array(source, name):
    match = re.search(r'\b' + name + r'\[\]\s*=\s*\{(.*?)\};', source, re.S)
    assert match, name
    return re.findall(r'"([^"\n]*)"', match[1])

for file, names in {
    'mp_fixes.cpp': ['CustomWeapons'],
    'class_editor.cpp': ['Weapons', 'Sidearms'],
    'overlay.cpp': ['weapons', 'sidearms'],
}.items():
    relative = 'CoD3Host/' + file
    before = subprocess.check_output(['git', 'show', 'HEAD:' + relative], cwd=root, text=True)
    after = (root / relative).read_text()
    for name in names:
        old, new = array(before, name), array(after, name)
        assert new[:-3] == old, f'{file}:{name}: existing saved IDs changed'
        assert new[-3].lower().replace(' ', '') == 'gewehr43'
        assert new[-2].lower().replace(' ', '') == 'mg42'
        assert new[-1].lower().replace(' ', '').replace('-', '').replace('_', '').replace('(', '').replace(')', '') in ('leeenfieldscoped', 'leeenfieldsniper')
        assert len(new) == (24 if 'sidearm' in name.lower() else 22)

settings = (root / 'CoD3Host/settings.cpp').read_text()
assert 'v.classPrimary[i] > 21' in settings
assert 'v.classSidearm[i] > 23' in settings
print('PASS: all five selection tables append G43, MG42, then the scoped Lee-Enfield; prior IDs and saved bounds agree')
