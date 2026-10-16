#!/usr/bin/env python3
"""Menu button ids (enum mpm_button in src/mpmenu.h) of a worktree, as JSON {NAME: id} (NAME without MPB_)."""
import json, os, re, sys
src = open(os.path.join(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__) + '/../../..', 'src', 'mpmenu.h')).read()
body = src[src.index('enum mpm_button {') + 17: src.index('MPB_COUNT')]
body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
ids, n = {}, 0
for tok in [t.strip() for t in body.split(',') if t.strip()]:
    if '=' in tok:
        name, v = [x.strip() for x in tok.split('=')]; n = int(v)
    else:
        name = tok
    ids[name[4:]] = n; n += 1
ids['COUNT'] = n
print(json.dumps(ids))
