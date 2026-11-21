"""Checks LEKMOD's trait loader can carry two leaders' abilities at once (Second Leader's row-level merge).

A player's abilities are loaded trait by trait in CvPlayerTraits::InitPlayerTraits. A field written there with a
plain assignment keeps only the last trait's value, so a merged second ability would wipe the first's. This fails
until every such field combines (adds, takes the larger, or ORs), slots that hold one thing take the first trait that
fills them with the leader's own traits first, and bans come from the leader's own traits only.

Usage: python3 check_trait_loader.py [path to the LEKMOD clone]   (default ~/repos/civ5-dll/lekmod)
"""
import os, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/repos/civ5-dll/lekmod')
SRC = os.path.join(ROOT, 'LEKMOD_DLL', 'CvGameCoreDLL_Expansion2')
BANS = ('m_abNoTrain', 'm_abNoBuild', 'm_abBannedUnitMissions', 'm_bNoAnnexing')
SLOT_GETTERS = ('GetFreeBuilding', 'GetFreeCapitalBuilding', 'GetFreeBuildingOnConquest', 'GetYieldSettleUnit',
                'GetFreeBuildingPrereqTech', 'GetCapitalFreeBuildingPrereqTech', 'GetBuildTimeOverride')


def body(src, signature):
    m = re.search(re.escape(signature) + r'[^{;]*\{', src)
    if not m: return None
    i, depth = m.end(), 1
    while depth and i < len(src):
        depth += {'{': 1, '}': -1}.get(src[i], 0)
        i += 1
    return src[m.end():i]


def region(src, signature):
    """The text of a function, from its signature to the next CvPlayerTraits function."""
    i = src.find(signature)
    if i < 0: return ''
    m = re.search(r'\n[A-Za-z][\w:<>\*& ]*\s+\w+::\w+\(', src[i + len(signature):])
    return src[i:i + len(signature) + (m.start() if m else len(src))]


def check(src_dir):
    """The problems in the loader under src_dir (a list of sentences)."""
    failures = []
    traits = open(os.path.join(src_dir, 'CvTraitClasses.cpp'), encoding='latin-1').read()
    infos_h = open(os.path.join(src_dir, 'CvInfos.h'), encoding='latin-1').read()
    if 'bool hasOwnTrait(int i) const;' not in infos_h:
        failures.append('CvLeaderHeadInfo has no hasOwnTrait: the loader cannot tell the leader\'s own traits from added ones')
    pre = body(traits, 'void CvPlayerTraits::PrecomputeLeaderTraits()') or ''
    if not re.search(r'hasOwnTrait\(iI\)\s*!=\s*\(iPass\s*==\s*0\)\)\s*continue', ' '.join(pre.split())):
        failures.append('PrecomputeLeaderTraits does not list the leader\'s own traits first')
    init = body(traits, 'void CvPlayerTraits::InitPlayerTraits()') or ''
    flat = ' '.join(init.split())
    if not re.search(r'bool bOwn = m_pPlayer != NULL && m_pPlayer->getLeaderInfo\(\)\.hasOwnTrait\(iI\); if\(bOwn != \(iPass == 0\)\) continue;', flat):
        failures.append('InitPlayerTraits does not apply each trait once, the leader\'s own first')
    lines = init.split('\n')
    written = set()
    for n, line in enumerate(lines):
        for m in re.finditer(r'(?<![.\w])(m_\w+)((?:\[[^\]]*\])*)\s*(\+=|-=|=(?!=)|\.push_back\(|\.insert\()', line):
            written.add(m.group(1))
        guard = ' '.join(l.strip() for l in lines[max(0, n - 2):n]) + ' ' + line
        for member in BANS:
            if re.search(r'\b' + member + r'\b[^;]*=(?!=)', line) and 'bOwn' not in guard:
                failures.append(f'{member} is set from every trait: only the leader\'s own may ban ({line.strip()[:80]})')
        m = re.match(r'\s*(m_\w+)((?:\[[^\]]*\])*)\s*=\s*trait->', line)
        if not m or m.group(1) in BANS: continue
        member = m.group(1)
        if not (re.search(r'\bif\s*\(', guard) and member in guard.replace(line, '')):
            failures.append(f'{member} is assigned from each trait in turn: a later trait wipes an earlier one ({line.strip()[:90]})')
    # Reset() starts with Uninit(); both clear. Read as text up to the next function: their #ifdef branches leave the
    # braces uneven.
    reset = region(traits, 'void CvPlayerTraits::Reset()') + region(traits, 'void CvPlayerTraits::Uninit()')
    for member in sorted(written):
        if member in ('m_vLeaderHasTrait', 'm_vPotentiallyActiveLeaderTraits'): continue
        if not re.search(r'\b' + member + r'\b', reset):
            failures.append(f'{member} is filled on every tech, but Reset() never clears it: it grows each time')
    for name in SLOT_GETTERS:
        b = body(traits, 'CvPlayerTraits::' + name + '(')
        if b is None: failures.append(f'{name} not found'); continue
        if 'm_vPotentiallyActiveLeaderTraits' not in b:
            failures.append(f'{name} reads traits in id order, not the leader\'s own first')
    # The capital's free building and the tech that gates it come from the same trait. (FreeBuildingPrereqTech also
    # gates free chosen buildings, which are not GetFreeBuilding's, so it keeps its own reading.)
    b = body(traits, 'CvPlayerTraits::GetCapitalFreeBuildingPrereqTech(') or ''
    if 'GetFreeCapitalBuilding()' not in b:
        failures.append('GetCapitalFreeBuildingPrereqTech can take its tech from a different trait than the building it gates')
    b = body(traits, 'CvPlayerTraits::GetFreeBuildingPrereqTech(') or ''
    if 'GetFreeBuildingPrereqTech() != NO_TECH' not in b:
        failures.append('GetFreeBuildingPrereqTech takes the first trait even when it has none (NO_TECH is -1, so true)')
    if re.search(r'm_iNearbyImprovement(CombatBonus|BonusRange) \+= trait->', flat) and 'bKeepsImprovement' not in flat:
        failures.append('the bonus beside an improvement adds up while only the first trait\'s improvement is kept')
    return failures


# Broken loaders the check must catch (the 2026-11-17 review's mutants and the bugs it found).
MUTANTS = [
    ('NoTrain from every trait', 'if(bOwn && trait->NoTrain(', 'if(trait->NoTrain('),
    ('NoAnnexing from every trait', 'if(bOwn && trait->IsNoAnnexing())', 'if(trait->IsNoAnnexing())'),
    ('no own-first order', 'if(m_pPlayer->getLeaderInfo().hasOwnTrait(iI) != (iPass == 0))\n\t\t\t\t\tcontinue;', ''),
    ('every trait in both passes', 'if(bOwn != (iPass == 0))\n\t\t\tcontinue;', 'if(false)\n\t\t\tcontinue;'),
    ('a plain assignment', 'm_iFreeCityYield[iYield] += trait->GetYieldChange(iYield);', 'm_iFreeCityYield[iYield] = trait->GetYieldChange(iYield);'),
    ('Reset forgets the golden-age thresholds', 'm_sGoldenAgeYieldThreshold.clear();', ''),
]


def self_test():
    import shutil, tempfile
    bad = []
    for name, old, new in MUTANTS:
        with tempfile.TemporaryDirectory() as d:
            for f in ('CvTraitClasses.cpp', 'CvInfos.h'): shutil.copy(os.path.join(SRC, f), d)
            p = os.path.join(d, 'CvTraitClasses.cpp')
            text = open(p, encoding='latin-1').read()
            if old not in text: bad.append(f'mutant "{name}": its code is not in the loader'); continue
            open(p, 'w', encoding='latin-1').write(text.replace(old, new, 1))
            if not check(d): bad.append(f'mutant "{name}" passes the check')
    return bad


failures = check(SRC) + self_test()
for f in failures: print('FAIL', f)
print(f'{len(failures)} problem(s)' if failures else f'trait loader: all checks pass, and the check catches {len(MUTANTS)} broken loaders')
sys.exit(1 if failures else 0)
