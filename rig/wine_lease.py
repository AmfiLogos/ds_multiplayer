#!/usr/bin/env python3
"""One Windows (CrossOver bottle DSMP) test copy machine-wide: the only way to take rig/wine.lock.

  wine_lease.py take <owner> [--pid N] [--wait S]   take the lock; exit 1 if held or the bottle is busy
  wine_lease.py release <owner>                     kill the bottle's leftover processes (winedbg too), then free the lock;
                                                    refused (exit 1) when <owner> does not hold it
  wine_lease.py status                              lock owner + the bottle's running processes; exit 0 = free and idle

take: atomic mkdir of rig/wine.lock + an owner file (owner, pid, time). Then the busy check: any process of the DSMP
bottle (by its path or its wineserver folder /tmp/.wine-<uid>/server-<dev>-<inode>/, never by name or age) = busy:
the lock is given back at once and take exits 1. Other CrossOver bottles never count; a leaked test copy always does.
--pid: the process that holds the lock (default: the caller, i.e. the parent of this command). When that process is
dead, the next take reclaims the lock (one log line). --wait S: retry every 10 s for up to S seconds.
Env (tests): WINE_LEASE_LOCK = lock folder, DSMP_BOTTLE = bottle folder.
Old owner files go to rig/old/ (never deleted)."""
import os, signal, subprocess, sys, time

ROOT = os.path.expanduser('~/ds_multiplayer')
LOCK = os.environ.get('WINE_LEASE_LOCK') or os.path.join(ROOT, 'rig', 'wine.lock')
OLD = os.path.join(os.path.dirname(LOCK), 'old')
BOTTLE = os.environ.get('DSMP_BOTTLE') or os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/DSMP')
NOOWNER_GRACE = 60          # a lock folder with no owner file for this long = a taker that died between its two steps


def log(*a):
    print('wine_lease:', *a, flush=True)


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def read_owner():
    """(owner, pid or None, taken) of the lock, or None when there is no lock folder"""
    if not os.path.isdir(LOCK):
        return None
    try:
        lines = open(os.path.join(LOCK, 'owner')).read().split('\n')
    except FileNotFoundError:
        return ('', None, '')
    pid = int(lines[1]) if len(lines) > 1 and lines[1].strip().isdigit() else None
    return (lines[0].strip(), pid, lines[2].strip() if len(lines) > 2 else '')


def ancestors():
    out, p = {os.getpid()}, os.getpid()
    while p > 1:
        r = subprocess.run(['ps', '-o', 'ppid=', '-p', str(p)], capture_output=True, text=True).stdout.strip()
        if not r.isdigit():
            break
        p = int(r)
        out.add(p)
    return out


def bottle_procs():
    """pids of the DSMP bottle's processes: a file, folder or cwd in the bottle, or the bottle's wineserver folder"""
    b = os.path.realpath(BOTTLE)
    keys = [b + '/']
    try:
        st = os.stat(b)
        keys.append('/.wine-%d/server-%x-%x/' % (os.getuid(), st.st_dev, st.st_ino))
    except FileNotFoundError:
        pass
    r = subprocess.run(['lsof', '-nP', '-w', '-Fpn'], capture_output=True, text=True).stdout
    pids, p = set(), None
    for l in r.split('\n'):
        if l.startswith('p'):
            p = int(l[1:])
        elif l.startswith('n') and p is not None and (l[1:] == b or any(k in l for k in keys)):
            pids.add(p)
    return sorted(pids - ancestors())


def describe(pids):
    if not pids:
        return 'none'
    r = subprocess.run(['ps', '-o', 'pid=,etime=,command=', '-p', ','.join(map(str, pids))], capture_output=True, text=True).stdout
    return '; '.join(' '.join(x.split())[:110] for x in r.strip().split('\n') if x.strip())


def stash_owner(tag):
    """move the owner file aside (rig/old/) and remove the empty lock folder"""
    os.makedirs(OLD, exist_ok=True)
    f = os.path.join(LOCK, 'owner')
    if os.path.exists(f):
        os.replace(f, os.path.join(OLD, 'wine.lock.owner.%s.%d.%d' % (tag or 'none', time.time(), os.getpid())))
    os.rmdir(LOCK)


def reclaim_if_stale():
    """a lock whose holder process is dead is freed (under a short mutex so two reclaimers never free a new lock)"""
    mtx = LOCK + '.reclaim'
    try:
        os.mkdir(mtx)
    except FileExistsError:
        return False
    try:
        o = read_owner()
        if o is None:
            return True
        owner, pid, taken = o
        if pid is None:
            if owner or time.time() - os.stat(LOCK).st_mtime < NOOWNER_GRACE:
                return False                      # an old-style owner file (no pid) or a taker between its two steps
        elif alive(pid):
            return False
        log('reclaiming a STALE wine.lock of %r (pid %s dead, taken %s)' % (owner, pid, taken or '?'))
        stash_owner((owner or 'none') + '.stale')
        return True
    finally:
        os.rmdir(mtx)


def take_once(owner, pid):
    try:
        os.mkdir(LOCK)
    except FileExistsError:
        if not reclaim_if_stale():
            o = read_owner() or ('', None, '')
            return 'held by %r (pid %s, taken %s)' % o
        try:
            os.mkdir(LOCK)
        except FileExistsError:
            return 'held (taken by another process during the reclaim)'
    with open(os.path.join(LOCK, 'owner'), 'w') as f:
        f.write('%s\n%d\n%s\n' % (owner, pid, time.strftime('%Y-%m-%d %H:%M:%S')))
    busy = bottle_procs()
    if busy:
        stash_owner(owner + '.busy')
        return 'the DSMP bottle is busy (lock given back): ' + describe(busy)
    return None


def take(owner, pid, wait):
    end = time.time() + wait
    while True:
        why = take_once(owner, pid)
        if why is None:
            log('taken by %s (pid %d)' % (owner, pid))
            return 0
        if time.time() >= end:
            log('NOT taken:', why)
            return 1
        time.sleep(10)


def release(owner):
    o = read_owner()
    if o is None:
        log('NOT released: no lock')
        return 1
    if o[0] != owner:
        log('NOT released: held by %r, not %r' % (o[0], owner))
        return 1
    pids = bottle_procs()
    if pids:
        log('stopping leftover bottle processes:', describe(pids))
        for sig, pause in ((signal.SIGTERM, 5), (signal.SIGKILL, 2)):
            for p in pids:
                try:
                    os.kill(p, sig)
                except ProcessLookupError:
                    pass
            t = time.time() + pause
            while time.time() < t and any(alive(p) for p in pids):
                time.sleep(0.2)
            pids = [p for p in pids if alive(p)]
            if not pids:
                break
        if pids:
            log('NOT released: processes survived SIGKILL:', describe(pids))
            return 1
    stash_owner(owner)
    log('released by', owner)
    return 0


def status():
    o, pids = read_owner(), bottle_procs()
    if o is None:
        print('lock: free')
    else:
        print('lock: %r pid %s (%s) taken %s' % (o[0], o[1], 'alive' if o[1] and alive(o[1]) else 'DEAD' if o[1] else 'no pid', o[2]))
    print('DSMP bottle processes:', describe(pids))
    return 0 if o is None and not pids else 1


def main(a):
    if len(a) >= 2 and a[0] == 'take':
        pid = int(a[a.index('--pid') + 1]) if '--pid' in a else os.getppid()
        wait = float(a[a.index('--wait') + 1]) if '--wait' in a else 0
        return take(a[1], pid, wait)
    if len(a) == 2 and a[0] == 'release':
        return release(a[1])
    if a == ['status']:
        return status()
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
