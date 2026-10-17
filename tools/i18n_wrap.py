#!/usr/bin/env python3
"""i18n_wrap.py - wrap the player-visible text of src/ui_*.c in TR()/TRN() (lane i18n_mpgame).

  python3 tools/i18n_wrap.py [--check] [file ...]     (default: every file that has tools/i18n_rules/<stem>.py)

Re-runnable on a newer main: run it on the untouched main files, then build and read the diff.
Per file, tools/i18n_rules/<stem>.py gives:
  EDITS  list of (old, new): exact text changes made first (restructured sentences, helpers, by-value fixes).
         Already applied (new found, old gone) = skipped; old missing = reported as MISS (fix the rule by hand).
  SINKS  {function: (arg index, kind)}: every string literal inside that argument and the ones after it is wrapped
         in TR(). kind "fmt": the argument is a printf format; when the call then has no other argument,
         fmt(TR("x")) becomes fmt("%s", TR("x")) so the compiler sees a literal format.
         kind "text": a plain text argument.
  DENY   regexes: a literal that matches one (its C source text, quotes included) is never wrapped.
  hook   optional def hook(text, log) -> text, run after EDITS (e.g. i18n_wrap.wrap_region for label tables).
  COLOR  True: TR("<color=#rrggbb>Text</color>") (no % or tags inside) becomes ui_col("#rrggbb", TR("Text")),
         so the colour tag stays out of the translated text (needs a ui_col helper in that file, added by EDITS).
Skipped always: literals already inside TR/TRN/TR_NOOP, literals without letters once tags and conversions are
removed, and every call of rig_log / strcmp / strstr / bput (they are not in SINKS).
Last: \\uXXXX escapes inside TR texts are written as the UTF-8 character (i18n_tool.py extract does not decode \\u).
--check: change nothing, print what would change (exit 1 when something would).
"""
import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RULES = os.path.join(ROOT, "tools", "i18n_rules")
LIT = r'"(?:[^"\\\n]|\\.)*"'
LITS = re.compile(rf'{LIT}(?:\s*{LIT})*')


def load_rules(stem):
    p = os.path.join(RULES, stem + ".py")
    spec = importlib.util.spec_from_file_location("rules_" + stem, p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def skip_lit_or_comment(t, i):
    """if t[i] starts a string/char literal or comment, return the index after it, else None"""
    c = t[i]
    if c == '"' or c == "'":
        j = i + 1
        while j < len(t) and t[j] != c:
            j += 2 if t[j] == "\\" else 1
        return j + 1
    if t.startswith("//", i):
        j = t.find("\n", i)
        return len(t) if j < 0 else j
    if t.startswith("/*", i):
        j = t.find("*/", i + 2)
        return len(t) if j < 0 else j + 2
    return None


def call_args(t, open_paren):
    """spans (start, end) of the top-level arguments of the call whose '(' is at open_paren, and the ')' index"""
    depth, i, start, args = 0, open_paren + 1, open_paren + 1, []
    while i < len(t):
        k = skip_lit_or_comment(t, i)
        if k is not None:
            i = k
            continue
        c = t[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append((start, i))
                return args, i
            depth -= 1
        elif c == "," and depth == 0:
            args.append((start, i))
            start = i + 1
        i += 1
    return None, None


def has_words(lits):
    s = re.sub(r"\\u[0-9A-Fa-f]{4}|\\x[0-9A-Fa-f]{2}|\\[0-7]{1,3}|\\.", " ", lits)
    s = re.sub(r"<[^<>]{0,64}>", " ", s)
    s = re.sub(r"#[0-9A-Fa-f]{3,8}\b", " ", s)            # colours
    if re.fullmatch(r'"(TEXTKEY_|Refactor|UnityEngine|TMPro)[^"]*"', lits.strip()):
        return False                                        # game text keys and class names
    s = re.sub(r"%[-+ #0-9.*$hljztLqI]*[a-zA-Z%]", " ", s)
    s = re.sub(r'"\s*"', " ", s)
    return re.search(r"[A-Za-z]{2,}|\b[A-Za-z]\b", s.replace('"', " ")) is not None


def wrapped_before(t, i):
    j = i - 1
    while j >= 0 and t[j] in " \t\n":
        j -= 1
    if j < 0 or t[j] != "(":
        return False
    k = j - 1
    while k >= 0 and t[k] in " \t":
        k -= 1
    return re.search(r"\b(TR|TRN|TR_NOOP)$", t[max(0, k - 8):k + 1]) is not None


def in_trn(t, i):
    """literal at i is the 2nd argument of a TRN( ... ) call"""
    m = re.search(r"\bTRN\s*\(\s*" + LIT + r"(?:\s*" + LIT + r")*\s*,\s*$", t[max(0, i - 400):i])
    return m is not None


def generic(t, rules, log):
    sinks = getattr(rules, "SINKS", {})
    deny = [re.compile(d) for d in getattr(rules, "DENY", [])]
    if not sinks:
        return t
    name_rx = re.compile(r"(?<![\w.>])(" + "|".join(map(re.escape, sinks)) + r")\s*\(")
    edits = []  # (start, end, replacement)
    i = 0
    while True:
        m = name_rx.search(t, i)
        if not m:
            break
        # skip matches inside comments / literals: cheap check, scan line start
        ls = t.rfind("\n", 0, m.start()) + 1
        line_head = t[ls:m.start()]
        if "//" in line_head or line_head.lstrip().startswith("*") or line_head.lstrip().startswith("/*"):
            i = m.end()
            continue
        if line_head.count('"') % 2 == 1:
            i = m.end()
            continue
        fn = m.group(1)
        args, close = call_args(t, m.end() - 1)
        if args is None:
            i = m.end()
            continue
        idx, kind = sinks[fn]
        if len(args) <= idx:
            i = m.end()
            continue
        for ai in range(idx, len(args)):
            a0, a1 = args[ai]
            for lm in LITS.finditer(t, a0, a1):
                s0, s1 = lm.start(), lm.end()
                lit = lm.group(0)
                if any(d.search(lit) for d in deny) or not has_words(lit):
                    continue
                if wrapped_before(t, s0) or in_trn(t, s0):
                    continue
                if ai == idx and kind == "fmt" and len(args) == idx + 1 and t[a0:a1].strip() == lit:
                    if "%%" in lit:
                        log.append(f"  NOTE {lit[:60]}: no-arg format with %%, wrapped as TR() format")
                        rep = f"TR({lit})"
                    else:
                        rep = f'"%s", TR({lit})'
                else:
                    rep = f"TR({lit})"
                edits.append((s0, s1, rep))
        i = close + 1 if close else m.end()
    for s0, s1, rep in sorted(edits, reverse=True):
        t = t[:s0] + rep + t[s1:]
    if edits:
        log.append(f"  wrapped {len(edits)} literal(s)")
    return t


def wrap_region(t, start, end, macro="TR_NOOP", log=None):
    """wrap every worded literal between the anchors start..end (first match of each) in macro( ) (tables of labels)"""
    a = t.find(start)
    b = t.find(end, a + len(start)) if a >= 0 else -1
    if a < 0 or b < 0:
        if log is not None:
            log.append(f"  MISS region {start[:60]!r}")
        return t
    seg, n = t[a:b], 0

    def rep(m):
        nonlocal n
        s = m.group(0)
        if not has_words(s):
            return s
        n += 1
        return f"{macro}({s})"
    out, i = [], 0
    for m in LITS.finditer(seg):
        if wrapped_before(seg, m.start()):
            continue
        out.append(seg[i:m.start()])
        out.append(rep(m))
        i = m.end()
    out.append(seg[i:])
    if log is not None and n:
        log.append(f"  {macro} on {n} table literal(s)")
    return t[:a] + "".join(out) + t[b:]


TRCALL = re.compile(r"\b(TR|TRN|TR_NOOP)(\s*\(\s*)(" + LITS.pattern + r")(?:(\s*,\s*)(" + LITS.pattern + r"))?")


def raw_unicode(t, log):
    """\\uXXXX inside TR/TRN/TR_NOOP literals -> the UTF-8 character itself (the extractor reads the source text)"""
    n = 0

    def fix(s):
        nonlocal n
        def one(m):
            nonlocal n
            n += 1
            return chr(int(m.group(1), 16))
        return re.sub(r"\\u([0-9A-Fa-f]{4})", one, s)

    def rep(m):
        return m.group(1) + m.group(2) + fix(m.group(3)) + ((m.group(4) + fix(m.group(5))) if m.group(5) else "")
    t = TRCALL.sub(rep, t)
    if n:
        log.append(f"  {n} \\u escape(s) in TR texts written as UTF-8")
    return t


COLRX = re.compile(r'TR\("<color=(#[0-9A-Fa-f]{6})>([^"%<>\\]*)</color>"\)')


def color_split(t, log):
    n = 0

    def rep(m):
        nonlocal n
        n += 1
        return f'ui_col("{m.group(1)}", TR("{m.group(2)}"))'
    t = COLRX.sub(rep, t)
    if n:
        log.append(f"  colour tags moved out of {n} text(s)")
    return t


def run(path, check):
    stem = os.path.splitext(os.path.basename(path))[0]
    rules = load_rules(stem)
    src = open(path, encoding="utf-8").read()
    t, log = src, []
    for old, new in getattr(rules, "EDITS", []):
        if new and (new in t or raw_unicode(new, []) in t):
            continue
        if old in t:
            c = t.count(old)
            if c > 1:
                log.append(f"  MISS (old found {c} times, not unique): {old[:90]!r}")
                continue
            t = t.replace(old, new, 1)
        else:
            log.append(f"  MISS: {old[:110]!r}")
    if hasattr(rules, "hook"):
        t = rules.hook(t, log)
    t = generic(t, rules, log)
    if getattr(rules, "COLOR", False):
        t = color_split(t, log)
    t = raw_unicode(t, log)
    changed = t != src
    print(f"{os.path.relpath(path, ROOT)}: {'changes' if changed else 'no change'}")
    for line in log:
        print(line)
    if changed and not check:
        open(path, "w", encoding="utf-8").write(t)
    return changed, any("MISS" in x for x in log)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv
    files = args or [os.path.join(ROOT, "src", f[:-3] + ".c") for f in sorted(os.listdir(RULES))
                     if f.endswith(".py") and not f.startswith("_")]
    anych = anymiss = False
    for f in files:
        ch, miss = run(os.path.abspath(f), check)
        anych |= ch
        anymiss |= miss
    sys.exit(1 if (check and anych) or anymiss else 0)


if __name__ == "__main__":
    main()
