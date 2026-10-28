"""Run Bazaar's hook generator over LEKMOD's source, so LEKMOD becomes the host that loads plugins.

Bazaar's generator is written for its own copy of the game source. This points it at LEKMOD's instead:
the engine folder, the project file that lists its sources, a build folder of our own, and our own
hook-id registry (seeded from Bazaar's, so functions both have keep Bazaar's numbers).

Paths the compiler's headers live at are passed through the environment, because the analysis runs in
worker processes that import the generator afresh.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BAZAAR = os.path.expanduser("~/repos/civ5-dll/bazaar")
GENERATOR = os.path.join(BAZAAR, "tools", "generator")
LEKMOD = os.path.expanduser("~/repos/civ5-dll/lekmod/LEKMOD_DLL/CvGameCoreDLL_Expansion2")
VCPROJ = os.path.join(LEKMOD, "CvGameCoreDLL_Expansion2.vcproj")
PREFIX = os.path.expanduser("~/repos/civ5-dll/.winebuild/drive_c")
BUILD = os.path.join(REPO, "build", "civgen")
REGISTRY = os.path.join(HERE, "hook-ids.txt")

os.environ["BAZAAR_VS2008_INCLUDE"] = os.pathsep.join(
    [os.path.join(PREFIX, "vc9", "include"), os.path.join(PREFIX, "sdk6", "Include")]
)
sys.path.insert(0, GENERATOR)
import common  # noqa: E402

common.SOURCE_ROOT = LEKMOD
common.ENGINE_DIR = LEKMOD
common.VCPROJ = VCPROJ
common.BUILD_DIR = BUILD
common.VS2008_INCLUDE = os.environ["BAZAAR_VS2008_INCLUDE"].split(os.pathsep)


def sources():
    """The .cpp files LEKMOD's project compiles, relative to its engine folder."""
    with open(VCPROJ, encoding="cp1252") as f:
        text = f.read()
    rels = re.findall(r'RelativePath="\.\\([^"]+\.cpp)"', text)
    return [r.replace("\\", os.sep) for r in rels]


SUPPORT_FOLDERS = ("CvWorldBuilderMap", "CvGameCoreDLLUtil", "CvLocalization", "CvGameDatabase", "FirePlace", "ThirdPartyLibs")


class LekmodProject(common.Project):
    """LEKMOD keeps the support libraries' folders inside the engine folder, where Bazaar keeps them beside it.
    Their headers are Firaxis's, not LEKMOD's, so a parse error in them is not the project's to answer for, just
    as it is not in Bazaar's layout where the folders fall outside the project root."""

    def contains(self, path):
        return common.Project.contains(self, path) and not common.under(
            path, [os.path.join(self.root, folder) for folder in SUPPORT_FOLDERS]
        )


def project():
    # LEKMOD keeps the support libraries' folders inside the engine folder, so SOURCE_ROOT is the engine folder.
    includes = [LEKMOD] + [os.path.join(LEKMOD, inc.replace("\\", os.sep)) for inc in common.SUPPORT_INCLUDE]
    return LekmodProject(LEKMOD, sources(), includes, BUILD, REGISTRY, "CvGameCoreDLLPCH.h", None)


if __name__ == "__main__":
    import analyze
    import generate

    p = project()
    print("engine:", p.root)
    print("sources:", len(p.sources))
    sys.stdout.flush()
    results = analyze.run(p)
    errors = analyze.project_errors(p, results)
    print("NEW parse errors outside function bodies:", len(errors))
    for e in sorted(errors)[:80]:
        print("  %s:%d %s" % (os.path.relpath(e[0], LEKMOD), e[1], e[2]))
    sys.stdout.flush()
    if errors and "--generate" not in sys.argv:
        print("stopping before generate; rerun with --generate to force")
        sys.exit(1)
    hooks = generate.run(p)
    print("DONE hook points:", len(hooks))
