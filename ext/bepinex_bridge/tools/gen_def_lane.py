#!/usr/bin/env python3
"""baselib_exports.def: forward every export of the game's baselib.dll to baselib_orig.dll."""
import os, pefile
H = os.path.expanduser("~")
pe = pefile.PE(f"{H}/ds_multiplayer/win/25846389/baselib.dll", fast_load=True)
pe.parse_data_directories(directories=[0])
with open(f"~/ds_multiplayer/ext/bepinex_bridge/build/win/baselib_exports.def", "w") as f:
    f.write("LIBRARY baselib.dll\nEXPORTS\n")
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        n = e.name.decode()
        f.write(f'    "{n}" = "baselib_orig.{n}" @{e.ordinal}\n')
    # symbols the mod looks up with dlsym(RTLD_DEFAULT, ...): exported from our DLL so the dlfcn shim finds them
    import re, glob
    srcs = [open(fn).read() for fn in glob.glob(f"~/ds_multiplayer/ext/bepinex_bridge/build/win/gen/*.c") + glob.glob(f"~/ds_multiplayer/ext/bepinex_bridge/build/win/gen/net/*.c")]
    wanted = sorted({n for s in srcs for n in re.findall(r'dlsym\(RTLD_DEFAULT, "(\w+)"', s)})
    # only names some module defines (a dlsym of a name nobody defines yet just returns NULL, as on the Mac):
    # a line with "name(" whose code part ends with "{" (a function body), not a call / pointer assignment
    # a function definition "name(params) {" (params may span lines; no ; or braces inside) or a global "name =" / "name;"
    def defined(n):
        pat = re.compile(r"\b%s\s*\([^;{}]*\)\s*\{|^[A-Za-z_][\w \t*]*\b%s\s*(\[[^]]*\])?\s*(=[^=]|;)" % (n, n), re.M)
        return any("dlsym" not in s[s.rfind("\n", 0, m.start()) + 1:m.start()] for s in srcs for m in pat.finditer(s))
    names = [n for n in wanted if defined(n)]
    for n in wanted:
        if n not in names: print("dlsym name with no definition (not exported, NULL at run time):", n)
    for n in names: f.write(f"    {n}\n")
print(len(pe.DIRECTORY_ENTRY_EXPORT.symbols), "forwarded exports +", len(names), "own exports (dlsym)")
