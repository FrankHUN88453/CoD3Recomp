# 0.5.202: the player-limit site catalogue: every instruction in the MP
# executable that names a 24-player structure or the number 24 in a player
# sense. Output: CSV rows kind,function,address,instruction.
import sys, re, csv
sys.path.insert(0,'.')
from lib import F, consts
PS_BASE, PS_END, PS_STRIDE = 0x82C2ECF0, 0x82C2ECF0 + 24*6352, 6352
SVS = 0x82B3FDE0
rows = []
def add(kind, f, i, t): rows.append((kind, '%08X' % f, '%08X' % (f + 4*i), t))
for f, ins in F.items():
    for i, a, k in consts(ins):
        if PS_BASE <= a < PS_END: add('player_states_address', f, i, ins[i])
        if SVS <= a < SVS + 0x40: add('svs_field', f, i, ins[i])
    for i, t in enumerate(ins):
        m = re.match(r'(mulli|addi|li|lis|ori|cmpwi|cmplwi) (?:cr\d,)?(r\d+),(?:r\d+,)?(-?\d+)$', t)
        if not m: continue
        op, v = m.group(1), int(m.group(3))
        if op == 'mulli' and v == 6352: add('player_state_stride', f, i, t)
        if op == 'mulli' and v == 4992: add('server_client_stride', f, i, t)
        if op == 'mulli' and v == 784: add('session_player_stride', f, i, t)
        if op in ('addi',) and v in (4112, 4116, 4120, 4124): add('session_player_offset', f, i, t)
        if op in ('cmpwi', 'cmplwi') and v == 24: add('compare_24', f, i, t)
        if op == 'li' and v == 24:
            nxt = ins[i+1] if i + 1 < len(ins) else ''
            if nxt.startswith(('stb', 'sth', 'stw')): add('store_24', f, i, t + ' ; ' + nxt)
            elif nxt.startswith('mtctr'): add('loop_24', f, i, t + ' ; ' + nxt)
        if op in ('lis', 'ori') and t.endswith(',1') and op == 'lis':
            nxt = ins[i+1] if i + 1 < len(ins) else ''
            if nxt.endswith(',54272'): add('server_alloc_0x1D400', f, i, t + ' ; ' + nxt)
w = csv.writer(open(sys.argv[1], 'w', newline=''))
w.writerow(['kind', 'function', 'address', 'instruction'])
for r in sorted(rows): w.writerow(r)
from collections import Counter
c = Counter(r[0] for r in rows); fc = {k: len({r[1] for r in rows if r[0] == k}) for k in c}
for k in sorted(c): print('%-24s %5d sites in %4d functions' % (k, c[k], fc[k]))
print('total', len(rows), 'sites in', len({r[1] for r in rows}), 'functions')
