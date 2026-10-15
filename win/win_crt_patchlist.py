#!/usr/bin/env python3
"""Windows CRT math entry points for the dsmath swap: for each libm symbol the Mac game code calls, take the Mac
methods that call it, look at their Windows twins, and vote for the CRT function (.text, < 0x50D000) they call.
Output math/crt_patch_list.tsv: symbol, win rva, votes, voters."""
import sys, collections, re
sys.argv = ["x", "--all"]
src = open("floatscan_xplat.py").read().split("scope = re.compile")[0]
exec(src)
TEXT_END = 0x50D000
SKIP = {0x443AF0, 0x443E20, 0x443D40, 0x443D90}
LIBM = re.compile(r"^_+(sin|cos|tan|asin|acos|atan|atan2|pow|exp|exp2|log|log10|cosh|sinh|tanh|hypot|sincos)f?(_stret)?$")
votes = collections.defaultdict(collections.Counter); voters = collections.Counter()
for (nm, sig), maddrs in mb_n.items():
    w = wb_n.get((nm, sig))
    if not w: continue
    ma, wa = maddrs[0], w[0]
    e = end_of(m_starts, ma); syms = set()
    for ins in csa.disasm(mraw[ma:e], ma):
        if ins.mnemonic in ("bl", "b") and ins.op_str.startswith("#"):
            t = int(ins.op_str[1:], 16)
            if t in stub2sym and LIBM.match(stub2sym[t]): syms.add(stub2sym[t].lstrip("_"))
    if len(syms) != 1: continue
    s = syms.pop()
    we = PDATA.get(wa) or end_of(w_starts, wa)
    tg = set()
    for ins in csx.disasm(wbytes(wa, we - wa), wa):
        if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            t = int(ins.op_str, 16)
            if t < TEXT_END and t not in SKIP and not (wa <= t < we): tg.add(t)
    votes[s].update(tg); voters[s] += 1
with open("math/crt_patch_list.tsv", "w") as o:
    o.write("symbol\twin_rva\tvotes\tvoters\n")
    for s in sorted(votes):
        t, c = votes[s].most_common(1)[0]
        o.write(f"{s}\t{t:#x}\t{c}\t{voters[s]}\n")
print(open("math/crt_patch_list.tsv").read())
