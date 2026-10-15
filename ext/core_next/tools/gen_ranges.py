#!/usr/bin/env python3
"""Build ../build/det_ranges.tsv for the determinism layer (rig_det.c) and ../build/methods.tsv for desync_diff.py.

det_ranges.tsv: one line per GameAssembly method that matters for RNG routing:
  <start hex> <end hex> <class> <name>     class 1 = view (draws go to the view stream even inside a sim tick)
                                           class 2 = helper (RandomUtil...: look one frame further up)
methods.tsv: <start hex>\t<name> for every method (sorted), used to name return addresses.
Source: ~/ds_skill_truth/dump_25846389/script.json (build 25846389).

core_next lane_rand copy (2026-10-15): writes to ~/ds_multiplayer/ext/core_next/build (OUT below), and
  - every segment of the class name after its namespace is tested (nested classes: PostProcessingManager.<Co_Shake>d__4);
    the namespace comes from dump.cs "// Namespace:" lines (longest prefix), so namespace words are not tested;
  - generic argument lists are cut off first (List<SomeView.Data> is not view code because of its argument);
  - PostProcessingManager is a view class word (camera shake, random_scan.md 1). Not "PostProcessing": Edgar's
    DungeonGeneratorPostProcessing* classes are dungeon generation (gameplay);
  - the old rule (last segment of the full name, as in rig/tools/gen_ranges.py) still applies, so this copy only adds
    view methods, it never removes one;
  - VIEW_METHODS: single methods that are view code in a non-view class (loading tips, random_scan.md 3).
Windows: det_ranges_win.tsv is written by win/port/gen_port.py with its own copy of these rules (needs the same change)."""
import json, os, re
SJ = os.path.expanduser('~/ds_skill_truth/dump_25846389/script.json')
OUT = os.path.expanduser('~/ds_multiplayer/ext/core_next/build')
DUMP_CS = os.path.expanduser('~/ds_skill_truth/dump_25846389/dump.cs')
d = json.load(open(SJ))
ms = sorted({(m['Address'], m['Name']) for m in d['ScriptMethod']})
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, 'methods.tsv'), 'w') as f:
    for a, n in ms:
        f.write('%x\t%s\n' % (a, n))
HELPER = re.compile(r'^Refactor\.(RandomUtil|WeightedRandomUtil)\b')
VIEW_NS = re.compile(r'^(Refactor\.View\.|Refactor\.UI\.|Refactor\.FX\.|Febucci\.|AllIn1SpriteShader\.|DG\.Tweening|TMPro\.|Refactor\.Sound\.|UnityEngine\.|Unity\.)')
VIEW_CLS = re.compile(r'(FX|SFX|Sound|BGM|Ambience|Graphic|HUD|MapCloud|SpriteManager|Camera|PostProcessingManager|Presenter|Animator|Tween|Particle|Speech|UI$|^UI|View$|Tooltip)')
VIEW_METHODS = {'Refactor.Util.TextSheet$$GetRandomTip', 'Refactor.Util.DataSheetManager$$GetRandomTip'}
NAMESPACES = sorted({l.split('// Namespace:', 1)[1].strip() for l in open(DUMP_CS) if l.startswith('// Namespace:')} - {''}, key=len, reverse=True)
def strip_generic_args(s):
    """cut '<...>' when it follows a name character (generic arguments); keep compiler names like '<Co_Shake>d__4'"""
    out, depth = [], 0
    for i, ch in enumerate(s):
        if depth:
            depth += (ch == '<') - (ch == '>')
            continue
        if ch == '<' and out and (out[-1].isalnum() or out[-1] in '_`'):
            depth = 1
            continue
        out.append(ch)
    return ''.join(out)
def class_parts(cls_name):
    """the class name without its namespace and generic arguments, split into outer + nested class names"""
    cls_name = strip_generic_args(cls_name)
    for ns in NAMESPACES:
        if cls_name.startswith(ns + '.'):
            return cls_name[len(ns) + 1:].split('.')
    return cls_name.split('.')
n1 = n2 = 0
with open(os.path.join(OUT, 'det_ranges.tsv'), 'w') as f:
    for i, (a, n) in enumerate(ms):
        end = ms[i + 1][0] if i + 1 < len(ms) else a + 4
        if end <= a:
            continue
        cls_name = n.split('$$')[0]
        k = 0
        if HELPER.match(cls_name):
            k = 2
        elif VIEW_NS.match(cls_name) or VIEW_CLS.search(cls_name.split('.')[-1]) or n in VIEW_METHODS or any(VIEW_CLS.search(s) for s in class_parts(cls_name)):
            k = 1
        if k:
            f.write('%x %x %d %s\n' % (a, end, k, n.replace(' ', '_')))
            n1 += k == 1; n2 += k == 2
print('view methods', n1, 'helper methods', n2)
