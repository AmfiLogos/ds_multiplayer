#!/usr/bin/env python3
"""For a hooked method: Mac caller methods whose Windows twin has no direct call to the Windows target
(the call was inlined or the code differs).  inline_missing.py <win_rva> ..."""
import sys, subprocess, json, collections, os
W = os.path.dirname(os.path.abspath(__file__))
m2w = {}; w2m = {}
for l in open(f"{W}/win_map.tsv"):
    f = l.rstrip("\n").split("\t")
    if f[0] == "method" and f[3]: w2m[int(f[3], 16)] = int(f[2], 16)
sjm = json.load(open(os.path.expanduser("~/ds_skill_truth/dump_25846389/script.json")))["ScriptMethod"]
sjw = json.load(open(f"{W}/25846389/dump/script.json"))["ScriptMethod"]
import re
MI = re.compile(r"MethodInfo_[0-9A-F]+")
wby = collections.defaultdict(list)
for m in sjw: wby[(m["Name"], MI.sub("", m["Signature"]))].append(m["Address"])
mname = {}
for m in sjm: mname.setdefault(m["Address"], (m["Name"], MI.sub("", m["Signature"])))
for wt in [int(x, 16) for x in sys.argv[1:]]:
    mt = w2m[wt]
    mc = subprocess.run(["python3", f"{W}/mac_callers.py", hex(mt)], capture_output=True, text=True).stdout.splitlines()
    wc = subprocess.run(["python3", f"{W}/win_callers.py", hex(wt)], capture_output=True, text=True).stdout.splitlines()
    wnames = collections.Counter(l.split(None, 3)[3] for l in wc if len(l.split(None, 3)) > 3)
    mnames = collections.Counter(l.split(None, 3)[3] for l in mc if len(l.split(None, 3)) > 3)
    print(f"== {hex(wt)} mac {sum(mnames.values())} win {sum(wnames.values())}")
    for n, c in mnames.items():
        if wnames.get(n, 0) < c: print(f"   mac caller {n} x{c}, windows x{wnames.get(n, 0)}")
