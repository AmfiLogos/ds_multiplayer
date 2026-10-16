#!/bin/zsh
# zipscan.sh <zip>...   G12: a release zip holds no saves, no game files, no templates and no user paths / user names.
# Unpacks each zip into a new temp folder (moved to $TMPDIR/zipscan_old after, never removed), then (python, raw bytes
# of every file, also inside binaries; no shell grep: the login shell's grep is a wrapper with false hits):
#   names: no Saves/, *.save, OutGame.json, UserSetting.json, UserCampaignLog.json, GameAssembly*, *.bundle, *.assets,
#          global-metadata.dat, data.unity3d, DungeonSettlers.exe/.app game binaries, template / base_*.json, dumps
#   bytes: no owner user / account names anywhere (case-insensitive), no "/Users/<owner>" or "C:\Users\<owner>".
#          Other users' build paths inside third-party binaries (runner = GitHub Actions, bokken = Unity, authors of
#          upstream .NET libraries) are listed as "info", not a failure.
# Prints one line per zip: "PASS <zip> <n> files <sha256>" or "FAIL <zip> (<n> files) <reasons>"; exit 1 when any fails.
set -u
rc=0
for z in "$@"; do
  T=$(mktemp -d); ditto -x -k "$z" $T 2>/dev/null || { echo "FAIL $z cannot unpack"; rc=1; continue; }
  SHA=$(shasum -a 256 "$z" | cut -d' ' -f1)
  python3 -I - "$T" "$z" "$SHA" <<'PY' || rc=1
import os, re, sys
root, zname, sha = sys.argv[1:4]
OK_USERS = {b'runner', b'bokken', b'Shared'}
OK_WIN = {b'crossover', b'public', b'default', b'all users'}
NAMES = [x.strip().lower().encode() for x in [os.environ.get('USER', '')] + os.environ.get('GATE_NAMES', '').split(',') if x.strip()]   # GATE_NAMES = more names, comma list
BAD_NAME = re.compile(r'(^|/)(Saves|.*\.save|OutGame\.json|UserSetting\.json|UserCampaignLog\.json|GameAssembly.*|.*\.bundle|.*\.assets|'
                      r'global-metadata\.dat|data\.unity3d|DungeonSettlers\.exe|DungeonSettlers\.app|.*template.*\.json|base_.*\.json|'
                      r'dump\.cs|il2cpp\.h|script\.json)$', re.I)
bad, n, info = [], 0, set()
for d, dirs, files in os.walk(root):
    for x in dirs + files:
        rel = os.path.relpath(os.path.join(d, x), root)
        if BAD_NAME.search(rel): bad.append(f'game/save name: {rel}')
    for x in files:
        n += 1; p = os.path.join(d, x); rel = os.path.relpath(p, root); b = open(p, 'rb').read(); low = b.lower()
        us = {m.group(1) for m in re.finditer(rb'/Users/([A-Za-z0-9_.-]+)', b)} - OK_USERS
        ws = {m.group(1).lower() for m in re.finditer(rb'[Cc]:[\\/]+Users[\\/]+([A-Za-z0-9_. -]+?)(?=[\\/"\x00])', b)} - OK_WIN
        own = sorted(x.decode(errors='replace') for x in (us | ws) if x.lower() in NAMES)
        if own: bad.append(f'{rel}: user path {",".join(own)}')
        nm = [x.decode() for x in NAMES if x in low]
        if nm and not own: bad.append(f'{rel}: name {",".join(nm)}')
        for x in sorted((us | ws) - {y.encode() for y in own}): info.add(x.decode(errors='replace'))
if bad: print(f'FAIL {zname} ({n} files) ' + '; '.join(bad[:12]) + (f'; ... {len(bad)} in all' if len(bad) > 12 else '')); sys.exit(1)
print(f'PASS {zname} {n} files {sha}' + (f'  (info: third-party build paths of {",".join(sorted(info))})' if info else ''))
PY
  mkdir -p ${TMPDIR:-/tmp}/zipscan_old; mv $T ${TMPDIR:-/tmp}/zipscan_old/
done
exit $rc
