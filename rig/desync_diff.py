#!/usr/bin/env python3
"""Compare two lockstep runs (copies A and B) and show where they first differ.

  desync_diff.py [dirA] [dirB]      default: ~/ds_multiplayer/instances/A and .../B

Reads <dir>/fp.log (one line per sim tick: tick all units pos rng nunits npos guids srands) and
<dir>/trace.bin (32-byte records written by src/rig_det.c: every RNG draw, Guid, System.Random,
path start and injected order, with GameAssembly return addresses). Prints the first tick whose
fingerprint differs, which part differs, and the first differing trace record with method names
(resolved through build/methods.tsv, made by rig/tools/gen_ranges.py)."""
import bisect, os, struct, sys

ROOT = os.path.expanduser('~/ds_multiplayer')
KINDS = {1: 'Random.Range(f)', 2: 'Random.Range(i)', 3: 'Random.RandomRangeInt', 4: 'Random.value', 5: 'Guid.NewGuid',
         6: 'new System.Random()', 7: 'AstarPath.StartPath', 8: 'order',
         9: 'MoveThing(x=val,y=c2)', 10: 'OnSpawnThing(x=val,y=c2)', 11: 'GetWorldPosition(guid=c2)', 12: 'GetCenterOfWorldOccupies(guid=c2)',
         13: 'SetCombatTarget(target=val, owner=c2)',
         14: 'AddHostile(entity=val, owner=c2)', 15: 'RemoveHostile(entity=val, owner=c2)',
         16: 'System.Random draw', 17: 'CombatTargetApplyData(target=val, owner=c2)',
         18: 'DecideTarget(ret=val, skill=c2)', 19: 'FindNewTarget(ret=val, skill=c2)', 20: 'GetSpecificTarget(ret=val, skill=c2)',
         21: 'GetClosestSingleEnemy(ret=val, user=c2)', 22: 'DecideSkill(skillhash=val, owner=c2)',
         23: 'ExecutionContext.Convert(target=val, user=c2)', 24: 'ExecutionContext.SetTarget(target=val, user=c2)'}
FL_VIEW, FL_OUT = 1, 2

_addrs, _names = [], []


def name(rva):
    if rva in (0, 0xFFFFFFFF):
        return '-' if rva == 0 else '(outside GameAssembly)'
    if not _addrs:
        for line in open(os.path.join(ROOT, 'build', 'methods.tsv')):
            a, n = line.rstrip('\n').split('\t', 1)
            _addrs.append(int(a, 16)); _names.append(n)
    i = bisect.bisect_right(_addrs, rva) - 1
    return f'{_names[i]}+0x{rva - _addrs[i]:x}' if i >= 0 else hex(rva)


def read_fp(d):
    out = {}
    for line in open(os.path.join(d, 'fp.log')):
        p = line.split()
        if len(p) >= 5:
            out[int(p[0])] = p[1:]
    return out


def read_trace(d):
    p = os.path.join(d, 'trace.bin')
    if not os.path.exists(p):
        return []
    b = open(p, 'rb').read()
    b = b[:len(b) // 32 * 32]
    return list(struct.iter_unpack('<qHHI3II', b))     # tick, kind, flags, seq, c0, c1, c2, val


def fmt(r):
    t, k, fl, sq, c0, c1, c2, v = r
    tag = ('VIEW ' if fl & FL_VIEW else '') + ('OUT ' if fl & FL_OUT else '')
    return f'tick {t} #{sq} {tag}{KINDS.get(k, k)} val=0x{v:08x}\n      at {name(c0)}\n      <- {name(c1)}\n      <- {name(c2)}'


def main():
    a = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'instances', 'A')
    b = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'instances', 'B')
    fa, fb = read_fp(a), read_fp(b)
    common = sorted(set(fa) & set(fb))
    print(f'fingerprint lines: A {len(fa)}, B {len(fb)}, common ticks {len(common)}'
          + (f' ({common[0]}..{common[-1]})' if common else ''))
    first = None
    for t in common:
        if fa[t] != fb[t]:
            first = t; break
    if first is None:
        print('RESULT: IDENTICAL on every common tick')
    else:
        parts = ['all', 'units', 'positions', 'rng+tick', 'nunits', 'npos', 'guids', 'srands']
        diff = [parts[i] for i in range(min(len(fa[first]), len(fb[first]), len(parts))) if fa[first][i] != fb[first][i]]
        print(f'RESULT: FIRST DIFFERENT TICK {first} (differs: {", ".join(diff)})')
        print('  A:', ' '.join(fa[first])); print('  B:', ' '.join(fb[first]))
    ta, tb = read_trace(a), read_trace(b)
    sim = lambda rs: [r for r in rs if not (r[2] & FL_VIEW)]      # sim-routed: in cycles and at load (tick -1)
    sa, sb = sim(ta), sim(tb)
    print(f'trace records (sim-routed, incl. load): A {len(sa)}, B {len(sb)}; view-routed in sim: A {sum(1 for r in ta if r[0] >= 0 and r[2] & FL_VIEW)}, '
          f'B {sum(1 for r in tb if r[0] >= 0 and r[2] & FL_VIEW)}')
    n = min(len(sa), len(sb))
    for i in range(n):
        ra, rb = sa[i], sb[i]
        if (ra[0], ra[1], ra[4], ra[5], ra[6], ra[7]) != (rb[0], rb[1], rb[4], rb[5], rb[6], rb[7]):
            print(f'FIRST DIFFERENT TRACE RECORD (index {i}):')
            for j in range(max(0, i - 3), i):
                print('  same: ' + fmt(sa[j]))
            print('  A:    ' + fmt(ra)); print('  B:    ' + fmt(rb))
            break
    else:
        if len(sa) != len(sb):
            print(f'trace: same first {n} records, then one side has more (run lengths differ?)')
        else:
            print('trace: sim records identical')
    return 0 if first is None and common else 1


if __name__ == '__main__':
    sys.exit(main())
