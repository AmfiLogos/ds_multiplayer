#!/usr/bin/env python3
"""owner_backup.py - the owner's game data on the Windows test VM (his Steam account), before and after a Steam-on run.

  owner_backup.py backup              copy + sha256 of the trees below to C:\\dsm\\backup_<time> (guest) and ~/ds_winbot/backup_<time>
  owner_backup.py compare <time>      sha256 now vs that backup: changed / missing / new files, Steam Cloud pending uploads
  owner_backup.py restore <time>      put back every changed or missing file from the backup; new files are moved to
                                      C:\\dsm\\to_delete\\owner_new_<time> (never deleted)
Trees: LocalLow\\CanOpener\\Dungeon Settlers (whole), the modded profiles (if any), Steam userdata\\<id>\\2798330 (remote +
remotecache.vdf). Read-only except restore. Nothing is ever deleted."""
import json, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import winrig as W                                                   # noqa: E402

STEAM = r'C:\Program Files (x86)\Steam\userdata'
APP = '2798330'


def trees(w):
    t = {'LocalLow': r'%USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers'}
    if w.exists(W.ROOT + r'\profiles'):
        t['profiles'] = W.ROOT + r'\profiles'
    for e in w.a('ls', path=STEAM).get('entries', []):
        if e['dir'] and w.exists(STEAM + '\\' + e['name'] + '\\' + APP):
            t['steam_' + e['name']] = STEAM + '\\' + e['name'] + '\\' + APP
    return t


HASH_PS = r'''param([string]$root, [string]$out)
$r = (Resolve-Path $root).Path
$h = @{}
Get-ChildItem -LiteralPath $r -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
  $h[$_.FullName.Substring($r.Length).TrimStart('\')] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower() }
$h | ConvertTo-Json -Compress | Set-Content -LiteralPath $out -Encoding UTF8'''


def hashes(w, root):
    ps = W.WORK + r'\winbot\hash_tree.ps1'; out = W.WORK + r'\winbot\hash_tree.json'
    w.write(ps, HASH_PS)
    w.a('run', exe=r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe', wait_ms=600000, capture=True,
        args=f'-NoProfile -ExecutionPolicy Bypass -File "{w.expand(ps)}" -root "{w.expand(root)}" -out "{w.expand(out)}"')
    return w.json_file(out) or {}


def backup(w):
    t = time.strftime('%Y%m%d-%H%M%S'); gb = W.WORK + r'\backup_' + t; mb = os.path.expanduser(f'~/ds_winbot/backup_{t}')
    man = {}
    for name, root in trees(w).items():
        r = w.a('run', exe=r'C:\Windows\System32\Robocopy.exe', wait_ms=900000, capture=True,
                args=f'"{w.expand(root)}" "{w.expand(gb)}\\{name}" /E /COPY:DAT /R:1 /W:1 /NP /NFL /NDL')
        if r.get('exit', 99) >= 8:
            raise SystemExit(f'robocopy {name}: exit {r.get("exit")}: {r.get("out", "")[-300:]}')
        h = hashes(w, root); man[name] = {'root': root, 'files': h}
        for f in h:                                                  # the Mac copy
            dst = os.path.join(mb, name, *f.split('\\'))
            w.get(root + '\\' + f, dst)
    os.makedirs(mb, exist_ok=True)
    json.dump(man, open(os.path.join(mb, 'manifest.json'), 'w'), indent=1)
    w.write(gb + r'\manifest.json', json.dumps(man, indent=1))
    print(json.dumps({'backup': t, 'guest': gb, 'mac': mb, 'files': {k: len(v['files']) for k, v in man.items()}}))
    return t


def compare(w, t):
    man = json.load(open(os.path.expanduser(f'~/ds_winbot/backup_{t}/manifest.json')))
    diff = {}
    for name, m in man.items():
        now = hashes(w, m['root']); was = m['files']
        d = {'changed': sorted(f for f in was if f in now and now[f] != was[f]), 'missing': sorted(f for f in was if f not in now),
             'new': sorted(f for f in now if f not in was)}
        if any(d.values()):
            diff[name] = d
    pending = []
    for name, m in man.items():
        if name.startswith('steam_'):
            v = w.text(m['root'] + r'\remotecache.vdf') or ''
            cur = None
            for line in v.splitlines():
                s = line.strip().strip('"')
                if line.strip().startswith('"') and line.strip().count('"') == 2 and s and not s.isdigit():
                    cur = s
                if '"syncstate"' in line and line.strip().split('"')[-2] == '3':
                    pending.append(cur)
    r = {'backup': t, 'diff': diff, 'remotecache_syncstate3': pending}
    print(json.dumps(r, indent=1))
    return r


def restore(w, t):
    man = json.load(open(os.path.expanduser(f'~/ds_winbot/backup_{t}/manifest.json')))
    gb = W.WORK + r'\backup_' + t; done = []
    for name, m in man.items():
        now = hashes(w, m['root']); was = m['files']
        for f in sorted(f for f in was if now.get(f) != was[f]):
            w.write(m['root'] + '\\' + f, open(os.path.expanduser(f'~/ds_winbot/backup_{t}/{name}/' + f.replace('\\', '/')), 'rb').read())
            done.append(f'{name}: put back {f}')
        for f in sorted(f for f in now if f not in was):
            w.move(m['root'] + '\\' + f, W.WORK + r'\to_delete\owner_new_' + t + '\\' + name + '\\' + f)
            done.append(f'{name}: new file moved out {f}')
    print(json.dumps(done, indent=1))
    return done


if __name__ == '__main__':
    a = sys.argv[1:]
    w = W.Win(); w.up()
    if a[0] == 'backup':
        backup(w)
    elif a[0] == 'compare':
        compare(w, a[1])
    elif a[0] == 'restore':
        restore(w, a[1])
