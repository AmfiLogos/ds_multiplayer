#!/usr/bin/env python3
"""Owner rule: a custom unit (My Units library / wiki link) never enters a regular save (fe_save.c fe_apply_results).
Builds two fe_cli binaries with ASan + UBSan (this branch, and main's src/fe as the baseline for the regression case),
then: (a) a result of custom units only is refused, home bytes unchanged, no new file; (b) mixed custom + normal: the
normal unit applied, the custom one reported and absent from the new save; (c) normal only: same output as main.
Run: python3 ext/foreign_expedition/tests/test_custom_guard.py   (from the worktree root or anywhere)
Baseline: FE_BASE_SRC = a folder holding fe sources; else tag FE_BASE_REF (default: the newest bundle-v* release tag) of CM_REPO (default
~/ds_custommodes), extracted with git archive into the run's out folder. FAIL when baseline and tree under test are one folder.
Fixtures: ext/foreign_expedition/fixtures, else ~/ds_multiplayer/ext/foreign_expedition/fixtures (not in git)."""
import copy, glob, hashlib, json, os, subprocess, sys, time, uuid

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(ROOT))
sys.path.insert(0, ROOT)
import fe_save as fe

FIX = os.path.join(ROOT, 'fixtures')
if not os.path.exists(os.path.join(FIX, 'home_Elven.json')):
    FIX = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/fixtures')
HOME_P, NORM_P = os.path.join(FIX, 'home_Elven.json'), os.path.join(FIX, 'hub_Moonlight_normal.json')
def fe_dir(repo):                                     # M4 split: FE moved from src/fe to custom/fe
    if os.path.isdir(os.path.join(repo, 'custom', 'fe')):
        return os.path.join(repo, 'custom', 'fe')
    cm = os.environ.get('CM_REPO') or os.path.expanduser('~/ds_custommodes')   # post-v13: Custom Modes' own repo
    return os.path.join(cm, 'fe') if os.path.isdir(os.path.join(cm, 'fe')) else os.path.join(repo, 'src', 'fe')
BUILD = os.path.join(ROOT, 'build'); os.makedirs(BUILD, exist_ok=True)
CLI, BASE = os.path.join(BUILD, 'fe_cli'), os.path.join(BUILD, 'fe_cli_base')
OUT = os.path.join(ROOT, 'out', 'test_custom_guard_' + time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT)
NEW_SRC = fe_dir(REPO)
def base_src():                                       # pinned release, never the tree under test
    if os.environ.get('FE_BASE_SRC'): return os.environ['FE_BASE_SRC']
    cm = os.environ.get('CM_REPO') or os.path.expanduser('~/ds_custommodes')
    tags = subprocess.run(['git', '-C', cm, 'tag', '-l', 'bundle-v*', '--sort=-v:refname'], capture_output=True, text=True).stdout.split()
    ref = os.environ.get('FE_BASE_REF') or (tags[0] if tags else '')
    if not ref: print('FAIL baseline: no bundle-v* release tag in', cm, '(git fetch --tags there, or set FE_BASE_REF)'); sys.exit(1)
    print('baseline: Custom Modes', ref)
    d = os.path.join(OUT, 'base_' + ref); os.makedirs(d)
    a = subprocess.run(['git', '-C', cm, 'archive', ref, 'fe'], capture_output=True)
    x = subprocess.run(['tar', '-x', '-C', d], input=a.stdout, capture_output=True)
    if a.returncode or x.returncode: print('FAIL baseline extract', ref, a.stderr.decode()[-300:], x.stderr.decode()[-300:]); sys.exit(1)
    return os.path.join(d, 'fe')
BASE_SRC = base_src()
o = lambda n: os.path.join(OUT, n)
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
fails = []


def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name + (f'  [{detail}]' if detail else ''), flush=True)
    if not cond: fails.append(name)


def build(src_dir, out):
    cmd = ['clang', '-fsanitize=address,undefined', '-g', '-O1', '-Wall', '-D_DARWIN_C_SOURCE',
           os.path.join(src_dir, 'test', 'fe_cli.c'), os.path.join(src_dir, 'fe_save.c'), os.path.join(src_dir, 'fe_json.c'), '-o', out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    check('build ' + os.path.basename(out), r.returncode == 0, (r.stderr or '')[-600:])


def cli(*a, exe=CLI, ok=True, seed='7'):
    r = subprocess.run([exe, *a], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', FE_SEED=seed))
    if ok and r.returncode: print('CLI', a[0], 'rc', r.returncode, r.stdout[-400:], r.stderr[-1500:])
    if 'ERROR: AddressSanitizer' in r.stderr or 'runtime error' in r.stderr: fails.append('sanitizer ' + a[0]); print(r.stderr[-2000:])
    return r


def dump(obj, name):
    json.dump(obj, open(o(name), 'w')); return o(name)


check('baseline is not the tree under test', os.path.realpath(BASE_SRC) != os.path.realpath(NEW_SRC), f'{BASE_SRC} vs {NEW_SRC}')
build(NEW_SRC, CLI)
build(BASE_SRC, BASE)
if fails: sys.exit(1)
HOME_SHA = sha(HOME_P)

# ---- entries: two normal units of the home save, plus custom copies with their own guids (as mu_entries / pb_entry make)
home = fe.load(HOME_P)
units = sorted(fe.clan_units(home), key=lambda u: -len(fe.item_guids_of(fe.ec_of(home)[u[0]])))
pick = [units[0][0], units[1][0]]
cli('export', HOME_P, 'uid7', o('entries.json'), *pick)
normal = json.load(open(o('entries.json')))


def custom_of(e, eid, **man):
    c = copy.deepcopy(e)
    m = {e['manifest']['unit_guid']: str(uuid.uuid4())}
    m.update({ig: str(uuid.uuid4()) for ig in e['closure']['items']})
    c['closure'] = fe.deep_replace(c['closure'], m)
    c['manifest'].update(export_id=eid, unit_guid=m[e['manifest']['unit_guid']], home_save_name='wiki link', **man)
    return c


lib_u = custom_of(normal[1], 'c' * 32, source='wiki', library_id='0123456789abcdef', home_campaign_guid=fe.ZERO)
wiki_u = custom_of(normal[0], 'd' * 32, source='wiki', home_campaign_guid='0' * 32)
custom_guids = [lib_u['manifest']['unit_guid'], wiki_u['manifest']['unit_guid']]
mans = {e['manifest']['export_id']: e['manifest'] for e in normal + [lib_u, wiki_u]}
mp = dump(mans, 'manifests.json')

# ---- the predicate
probe = {'n': normal[0]['manifest'],
         'src_wiki': dict(normal[0]['manifest'], source='wiki'), 'src_library': dict(normal[0]['manifest'], source='library'),
         'lib_id': dict(normal[0]['manifest'], library_id='0123456789abcdef'),
         'zero_dash': dict(normal[0]['manifest'], home_campaign_guid=fe.ZERO), 'zero_plain': dict(normal[0]['manifest'], home_campaign_guid='0' * 32),
         'no_home': {k: v for k, v in normal[0]['manifest'].items() if k != 'home_campaign_guid'}}
r = cli('custom', dump(probe, 'probe.json'))
got = dict(l.split() for l in r.stdout.splitlines())
check('fe_manifest_custom: normal 0, wiki / library / library_id / zero guid (both forms) / no home 1',
      got == {'n': '0', 'src_wiki': '1', 'src_library': '1', 'lib_id': '1', 'zero_dash': '1', 'zero_plain': '1', 'no_home': '1'}, str(got))


def run_hub(tag, ents, bump=None):
    """hub from the Normal fixture, the end state (level +1 for `bump` units), results for uid7"""
    cli('hub', NORM_P, dump(ents, f'ents_{tag}.json'), 'a1b2c3d4' + tag.encode().hex()[:8].ljust(24, '0'), o(f'hub_{tag}.json'), o(f'run_{tag}.json'))
    hub, run = fe.load(o(f'hub_{tag}.json')), json.load(open(o(f'run_{tag}.json')))
    for row in run['units']:
        if bump and row['home_guid'] in bump: fe.comp(fe.ec_of(hub)[row['hub_guid']], 0)['ProgressionStats']['Level'] += 1
    end = o(f'end_{tag}.json'); json.dump(hub, open(end, 'w'))
    cli('extract', end, o(f'run_{tag}.json'), 'uid7', '0,1,0', dump({}, f'loot_{tag}.json'), '-', o(f'res_{tag}.json'))
    return o(f'res_{tag}.json')


def new_files(d): return sorted(glob.glob(os.path.join(d, '*'))) if os.path.isdir(d) else []


# (a) custom units only: refused, nothing written
res_a = run_hub('a', [lib_u, wiki_u])
check('(a) result made for the two custom units', len(json.load(open(res_a))['units']) == 2)
r = cli('apply', HOME_P, res_a, mp, o('claims_a.json'), o('saves_a'), ok=False)
check('(a) all custom: refused with the owner text', r.returncode == 2 and r.stdout.strip() ==
      'REFUSED These units came from My Units or a wiki link: nothing to bring home (your saves are not changed).', r.stdout.strip())
check('(a) home save bytes unchanged', sha(HOME_P) == HOME_SHA)
check('(a) no new file in out_dir, no ledger', new_files(o('saves_a')) == [] and not os.path.exists(o('claims_a.json')), str(new_files(o('saves_a'))))

# (b) mixed: normal[0] + the library unit
res_b = run_hub('b', [normal[0], lib_u], bump={pick[0], lib_u['manifest']['unit_guid']})
r = cli('apply', HOME_P, res_b, mp, o('claims_b.json'), o('saves_b'))
path = r.stdout.splitlines()[0] if r.returncode == 0 else ''
check('(b) mixed: a new save', bool(path) and os.path.exists(path), r.stdout.strip()[:300])
if path and os.path.exists(path):
    new = fe.load(path); txt = open(path).read()
    lv = fe.comp(fe.ec_of(new)[pick[0]], 0)['ProgressionStats']['Level']
    check('(b) the normal unit applied (level +1)', lv == normal[0]['manifest']['level'] + 1, f'{lv} vs {normal[0]["manifest"]["level"]}')
    cg = lib_u['manifest']['unit_guid']
    check('(b) the custom unit is not in the new save (guid absent)', cg not in txt and not any(ig in txt for ig in lib_u['closure']['items']))
    check('(b) report names the custom unit', f'{cg[:8]}: custom unit (My Units / wiki link): stays out of your saves' in r.stdout, r.stdout.strip())
    p = fe.check_refs(new)
    check('(b) Python reference check of the new save clean', not p, '; '.join(p[:3]))
check('(b) home save bytes unchanged', sha(HOME_P) == HOME_SHA)

# (c) normal only: same new save and report as main (baseline binary), same seed
res_c = run_hub('c', normal, bump={pick[0]})
outs = []
for tag, exe in (('new', CLI), ('base', BASE)):
    r = cli('apply', HOME_P, res_c, mp, o(f'claims_c_{tag}.json'), o(f'saves_c_{tag}'), exe=exe)
    p_ = r.stdout.splitlines()[0] if r.returncode == 0 else ''
    s = fe.load(p_) if p_ and os.path.exists(p_) else None
    if s: s['CampaignSaveHeader'].pop('SaveTime', None)
    outs.append((r.returncode, r.stdout.splitlines()[1:], s, os.path.basename(p_)))
check('(c) normal only: applied by both', outs[0][0] == 0 and outs[1][0] == 0, f'{outs[0][0]} {outs[1][0]}')
check('(c) same file name, same report as main', outs[0][3] == outs[1][3] and outs[0][1] == outs[1][1], f'{outs[0][1]} / {outs[1][1]}')
check('(c) same save content as main (SaveTime aside)', outs[0][2] is not None and outs[0][2] == outs[1][2])
check('(c) no custom line in the report', not any('custom unit' in l for l in outs[0][1]))
check('(c) home save bytes unchanged', sha(HOME_P) == HOME_SHA)

print(f'\n{"ALL PASS" if not fails else str(len(fails)) + " FAIL: " + ", ".join(fails)}  (out: {OUT})')
sys.exit(1 if fails else 0)
