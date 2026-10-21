#!/usr/bin/env python3
"""exploration: does UnityUtil.IsPointerOverUI see our hidden window's UI at all, and does the pause panel cover the world?"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import us_lib as L
from us_lib import c, st, mp, log, wait
L.rig.DYLIB = os.path.join(L.WT, 'build', 'mouse', 'dsrig.dylib')


def probe(Y, x, y, label):
    c(Y, cmd='ui_teams', action='mouse', x=x, y=y, button=5)     # button 5: no press, position only
    time.sleep(1.5)
    s = c(Y, cmd='ui_selshow')
    log(label, (x, y), 'over_ui', s.get('over_ui'), 'mouse', s.get('mouse'))


def body(X, Y):
    L.setup(X, Y, window=True)
    c(Y, cmd='ui_teams', action='close'); time.sleep(1)
    for lab, xy in (('portrait bar', (612, 500)), ('skill bar', (480, 15)), ('players panel', (290, 420)), ('world centre', (480, 270)), ('world left', (200, 300))):
        probe(Y, *xy, 'RUNNING ' + lab)
    c(X, cmd='mp_pause', on=1); wait('paused', lambda: mp(Y).get('paused'), timeout=60); time.sleep(3)
    for lab, xy in (('portrait bar', (612, 500)), ('world centre', (480, 270)), ('world left', (200, 300)), ('pause banner', (480, 395))):
        probe(Y, *xy, 'PAUSED ' + lab)
    L.results['explored'] = True


if __name__ == '__main__':
    sys.exit(L.run_main('explore_ui', body, window=True))
