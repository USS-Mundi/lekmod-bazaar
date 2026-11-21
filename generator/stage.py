"""Lay out the two mod folders the game loads, and the script that installs them.

LEKMOD for Bazaar is Bazaar's host mod with the LEKMOD-based DLL in place of Bazaar's: the same screens, the same
plugin table, a mod id of its own. It blocks Bazaar itself and the old Local MCP Server DLL, since the game
must be handed one DLL path only, and it does not depend on the VMC Bootstrap, which serves Bazaar's engine
rather than LEKMOD's.

MCP Server for LEKMOD is the plugin's own mod, with the DLL rebuilt against this host, its dependency pointed at
this host instead of Bazaar, and an id and name of its own, so it can sit beside the Workshop's MCP Server for Bazaar.

Nothing here touches the game. build/stage/install.sh does, when run by hand.
"""
import getpass
import hashlib
import os
import re
import shutil
import sys
import zipfile
from xml.sax.saxutils import escape

HOME = os.path.expanduser("~")
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(REPO, "build", "stage")
OUT = os.path.join(HOME, "repos", "civ5-dll", ".winebuild", "drive_c", "lek", "out")
BAZAAR_PKG = os.path.join(REPO, "sdk-download", "packages", "host")
MCP_REPO = os.path.join(REPO, "mcp-plugin")
PLUGINS = os.path.join(REPO, "plugins")
LEKMOD_DLC = os.path.join(HOME, "repos", "lekmod-mac", "lekmod-dlc-parked")
LEKMAP = os.path.join(HOME, "repos", "civ5-dll", "lekmod", "Lekmap")
LEKMOD_LICENSE = os.path.join(HOME, "repos", "civ5-dll", "lekmod", "LICENSE")
GAME = os.path.join(HOME, "Library", "Application Support", "CrossOver", "Bottles", "Steam", "drive_c",
                    "Program Files (x86)", "Steam", "steamapps", "common", "Sid Meier's Civilization V")
MODS = os.path.join(HOME, "Library", "Application Support", "CrossOver", "Bottles", "Steam", "drive_c",
                    "users", "crossover", "Documents", "My Games", "Sid Meier's Civilization 5", "MODS")

HOST_ID = "3b0f6a0e-7d2c-4c58-9b1e-5a4f2e8d6c01"
HOST_VERSION = 1
MCP_ID = "5a3011d0-272d-4f00-83b2-1ddbd4679132"
MCP_VERSION = 1
BAZAAR_ID = "b829cadb-b570-46c5-a0ba-419434eb6398"
OLD_MCP_ID = "48e7f398-9447-434a-8b11-9e7e38cde8c6"
BNW_DLC_ID = "6DA07636-4123-4018-B643-6575B4EC336B"


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest().upper()


def modinfo(mod_id, version, name, teaser, description, authors, deps, refs, blocks, files, actions, entry_points):
    """files: [(relative path, import flag)]; deps/refs/blocks: [(kind, id, min, max, title)]."""
    assoc = lambda kind, mid, lo, hi, title: '    <%s id="%s" minversion="%s" maxversion="%s"%s />' % (
        kind, mid, lo, hi, ' title="%s"' % escape(title) if title else "")
    lines = ['<?xml version="1.0" encoding="utf-8"?>', '<Mod id="%s" version="%d">' % (mod_id, version), "  <Properties>",
             "    <Name>%s</Name>" % escape(name), "    <Stability>Experimental</Stability>",
             "    <Teaser>%s</Teaser>" % escape(teaser), "    <Description>%s</Description>" % escape(description),
             "    <Authors>%s</Authors>" % escape(authors), "    <HideSetupGame>0</HideSetupGame>",
             "    <AffectsSavedGames>1</AffectsSavedGames>", "    <MinCompatibleSaveVersion>0</MinCompatibleSaveVersion>",
             "    <SupportsSinglePlayer>1</SupportsSinglePlayer>", "    <SupportsMultiplayer>0</SupportsMultiplayer>",
             "    <SupportsHotSeat>0</SupportsHotSeat>", "    <SupportsMac>0</SupportsMac>",
             "    <ReloadAudioSystem>0</ReloadAudioSystem>", "    <ReloadLandmarkSystem>0</ReloadLandmarkSystem>",
             "    <ReloadStrategicViewSystem>0</ReloadStrategicViewSystem>", "    <ReloadUnitSystem>0</ReloadUnitSystem>",
             "  </Properties>", "  <Dependencies>"]
    lines += [assoc(*d) for d in deps] + ["  </Dependencies>", "  <References>"]
    lines += [assoc(*r) for r in refs] + ["  </References>", "  <Blocks>"]
    lines += [assoc(*b) for b in blocks] + ["  </Blocks>", "  <Files>"]
    lines += ['    <File md5="%s" import="%d">%s</File>' % (m, imp, escape(rel)) for rel, imp, m in files]
    lines += ["  </Files>", "  <Actions>"] + actions + ["  </Actions>", "  <EntryPoints>"] + entry_points
    lines += ["  </EntryPoints>", "</Mod>", ""]
    return "\r\n".join(lines)


def builder():
    """The builder's user name, which no release file may contain."""
    return getpass.getuser().encode()


def scrubbed(src, dst):
    """A copy of a built DLL with the builder's home folder blanked out of the source paths the compiler wrote into
    it (assert messages and the like): same length, so nothing in the file moves."""
    user = builder()
    data = open(src, "rb").read()
    data = re.sub(rb"(?i)(users[\\/])" + re.escape(user) + rb"(?=[\\/])", lambda m: m.group(1) + b"x" * len(user), data)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    shutil.copystat(src, dst)
    return dst


def stage_mod(folder, sources, mod_id, version, **kw):
    """Copies sources [(src path, rel path, import)] into the mod folder and writes its modinfo. A plugin DLL loads
    only through a DllPlugins row, so a plugin DLL (one not imported, as the game core is) that no staged SQL registers
    stops the build."""
    sql = "".join(open(src, encoding="utf-8").read() for src, rel, _ in sources if rel.lower().endswith(".sql"))
    for _, rel, imp in sources:
        name = os.path.basename(rel)
        if name.lower().endswith(".dll") and not imp and "INSERT INTO DllPlugins (File) VALUES ('%s')" % name not in sql:
            sys.exit("%s: no SQL registers %s in DllPlugins, so the game would never load it" % (kw["name"], name))
    files = []
    for src, rel, imp in sources:
        dst = os.path.join(folder, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        files.append((rel, imp, md5(dst)))
    name = kw["name"]
    with open(os.path.join(folder, "%s (v %d).modinfo" % (name, version)), "w", encoding="utf-8", newline="") as f:
        f.write(modinfo(mod_id, version, files=files, **kw))
    return files


def write_zip(path, host_dll, mod_folders):
    """The release download: the host DLL for LEKMOD's DLC folder, the mod folders under MODS, and LEKMOD's license."""
    if not os.path.isfile(LEKMOD_LICENSE):
        sys.exit("LEKMOD's license not found at %s" % LEKMOD_LICENSE)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(host_dll, "CvGameCore_Expansion2.dll")
        z.write(LEKMOD_LICENSE, "LEKMOD-LICENSE.txt")
        for folder in mod_folders:
            for root, _, files in os.walk(folder):
                for f in sorted(files):
                    full = os.path.join(root, f)
                    z.write(full, os.path.join("MODS", os.path.relpath(full, os.path.dirname(folder))))
    # The release must not name whoever built it.
    with zipfile.ZipFile(path) as z:
        named = [n for n in z.namelist() if builder().lower() in z.read(n).lower()]
    if named:
        os.remove(path)
        sys.exit("the build user's name is in %s; no release zip written" % ", ".join(named))
    print("wrote", path)


def main():
    shutil.rmtree(os.path.join(STAGE, "MODS"), ignore_errors=True)
    os.makedirs(os.path.join(STAGE, "MODS"))
    host_dll = os.path.join(OUT, "CvGameCore_Expansion2.dll")
    if not os.path.isfile(host_dll):
        sys.exit("no host DLL at %s; run build_host.sh first" % host_dll)
    host_dll = scrubbed(host_dll, os.path.join(STAGE, "dll", "CvGameCore_Expansion2.dll"))
    # Second Leader and Time of Day ship inside LEKMOD for Bazaar: one mod to turn on. Each is built by its build.sh.
    built = {}
    for name, rel in (("SecondLeader.dll", "sl"), ("ColorGrade.dll", "cg")):
        path = os.path.join(OUT, rel, name)
        if not os.path.isfile(path):
            sys.exit("no %s at %s; run plugins/%s/build.sh first" % (name, path, "second-leader" if rel == "sl" else "time-of-day"))
        built[name] = scrubbed(path, os.path.join(STAGE, "dll", name))
    sl = os.path.join(PLUGINS, "second-leader", "mod")
    tod = os.path.join(PLUGINS, "time-of-day", "mod")
    extras = ([(built["SecondLeader.dll"], "SecondLeader.dll", 0)]
              + [(os.path.join(sl, f), f, 0) for f in ("SecondLeader.sql", "SecondLeaderScripts.lua", "SecondLeaderScripts.xml")]
              + [(built["ColorGrade.dll"], "ColorGrade.dll", 0)]
              + [(os.path.join(tod, f), f, 0) for f in ("TimeOfDay.sql", "TimeOfDay.lua", "TimeOfDay.xml")]
              + [(os.path.join(tod, f), f, 1) for f in sorted(os.listdir(tod)) if f.endswith(".dds")])

    host_name = "LEKMOD for Bazaar"
    host_folder = os.path.join(STAGE, "MODS", "%s (v %d)" % (host_name, HOST_VERSION))
    host_files = stage_mod(
        host_folder,
        [(host_dll, "CvGameCore_Expansion2.dll", 1)]
        + [(os.path.join(BAZAAR_PKG, rel), rel, imp) for rel, imp in [
            ("DllPlugins.sql", 0), ("LICENSE.txt", 0), ("NOTICE.txt", 0),
            ("frontend/AdvancedSetup.lua", 1), ("frontend/AdvancedSetup.xml", 1),
            ("ingame/BazaarStopped.lua", 1), ("ingame/BazaarStopped.xml", 1)]]
        + extras,
        HOST_ID, HOST_VERSION,
        name=host_name,
        teaser="LEKMOD's game core with Bazaar's plugin loader built in, so Bazaar plugins run on LEKMOD.",
        description="LEKMOD's game core (v35.4) with the Bazaar Multi-DLL Mod Loader generated into it: Bazaar plugins "
                    "such as MCP Server for Bazaar load and run with LEKMOD's rules. Turn this on in place of Bazaar "
                    "itself; the two cannot both set the game's DLL. Needs LEKMOD's MP_MODSPACK data in the game's DLC "
                    "folder. Single player only. Also in it: the game option Second Leader For Everyone! (every "
                    "civilization gets a random second leader too) and Time of Day (shades the map through the day; off "
                    "until you pick a mode in its panel).",
        authors="EnormousApplePie and the LEKMOD team (LEKMOD), spradlin-dev (Bazaar)",
        deps=[("Dlc", BNW_DLC_ID, 0, 999, "")],
        refs=[],
        blocks=[("Mod", BAZAAR_ID, 0, 999, "Bazaar Multi-DLL Mod Loader"),
                ("Mod", OLD_MCP_ID, 0, 999, "Local MCP Server DLL")],
        actions=["    <OnGetDLLPath>", "      <SetDllPath>CvGameCore_Expansion2.dll</SetDllPath>", "    </OnGetDLLPath>",
                 "    <OnModActivated>", "      <UpdateDatabase>DllPlugins.sql</UpdateDatabase>",
                 "      <UpdateDatabase>SecondLeader.sql</UpdateDatabase>", "      <UpdateDatabase>TimeOfDay.sql</UpdateDatabase>",
                 "    </OnModActivated>"],
        entry_points=['    <EntryPoint type="InGameUIAddin" file="ingame/BazaarStopped.xml">',
                      "      <Name>Plugin stopped working</Name>",
                      "      <Description>Returns the player to the main menu after a plugin crashes in a game.</Description>",
                      "    </EntryPoint>",
                      '    <EntryPoint type="InGameUIAddin" file="SecondLeaderScripts.xml">',
                      "      <Name>Second Leader scripts</Name>",
                      "      <Description>LEKMOD's scripted abilities for merged second leaders.</Description>",
                      "    </EntryPoint>",
                      '    <EntryPoint type="InGameUIAddin" file="TimeOfDay.xml">',
                      "      <Name>Time of Day</Name>",
                      "      <Description>The Time of Day panel: off until a mode is picked.</Description>",
                      "    </EntryPoint>"])
    print("staged %s: %d files" % (host_folder, len(host_files)))

    plugin_dll = os.path.join(OUT, "plugin", "MCPServer.dll")
    if os.path.isfile(plugin_dll):
        plugin_dll = scrubbed(plugin_dll, os.path.join(STAGE, "dll", "MCPServer.dll"))
        mcp_name = "MCP Server for LEKMOD"
        mcp_folder = os.path.join(STAGE, "MODS", "%s (v %d)" % (mcp_name, MCP_VERSION))
        mod = os.path.join(MCP_REPO, "MCPServer", "mod")
        # The UI: the Message Center window (its textures go into the game's file system) and MCPServerUI, which has no
        # window of its own.
        ui = [(os.path.join(mod, f), f, 1 if f.endswith(".dds") else 0)
              for f in sorted(os.listdir(mod)) if f.endswith((".lua", ".xml", ".dds"))]
        mcp_files = stage_mod(
            mcp_folder,
            [(plugin_dll, "MCPServer.dll", 0), (os.path.join(mod, "MCPServer.sql"), "MCPServer.sql", 0)] + ui
            + [(os.path.join(MCP_REPO, "LICENSE"), "LICENSE.txt", 0), (os.path.join(MCP_REPO, "NOTICE"), "NOTICE.txt", 0)],
            MCP_ID, MCP_VERSION,
            name=mcp_name,
            teaser="Runs a local MCP server so an AI model can play Civilization V.",
            description="Runs an MCP server on this PC at http://127.0.0.1:7676/mcp. An AI model connected through an MCP "
                        "client on the same PC can play your civilization, a teammate or an opponent. Adds the "
                        "Message Center, where you write to the AI players one to one or in groups. Single player only. "
                        "This build is a plugin for LEKMOD for Bazaar.",
            authors="maddachshund (spradlin-dev); rebuilt for LEKMOD for Bazaar",
            deps=[("Dlc", BNW_DLC_ID, 0, 999, ""), ("Mod", HOST_ID, 0, 999, host_name)],
            refs=[],
            blocks=[("Mod", OLD_MCP_ID, 0, 999, "Local MCP Server DLL")],
            actions=["    <OnModActivated>", "      <UpdateDatabase>MCPServer.sql</UpdateDatabase>", "    </OnModActivated>"],
            entry_points=['    <EntryPoint type="InGameUIAddin" file="MessageCenter.xml">', "      <Name>Message Center</Name>",
                          "      <Description>Write to the AI players, one to one or in groups.</Description>", "    </EntryPoint>",
                          '    <EntryPoint type="InGameUIAddin" file="MCPServerUI.xml">', "      <Name>MCP Server UI</Name>",
                          "      <Description>Relays the game's popups and leader screens to the AI players.</Description>",
                          "    </EntryPoint>"])
        print("staged %s: %d files" % (mcp_folder, len(mcp_files)))
        write_zip(os.path.join(STAGE, "lekmod-bazaar.zip"), host_dll, [host_folder, mcp_folder])
    else:
        print("plugin DLL not built yet; MCP Server for Bazaar not staged, no release zip")

    install = os.path.join(STAGE, "install.sh")
    with open(install, "w") as f:
        f.write('''#!/usr/bin/env bash
# Installs the staged mods into the game. Run by hand. The parked LEKMOD copies under ~/repos/lekmod-mac are
# left as they are, so this can be undone by deleting what it adds.
set -euo pipefail
GAME=%r
MODS=%r
DLC_SRC=%r
STAGE=%r
HOST_DLL=%r
LEKMAP=%r
[ -d "$GAME/Assets/DLC" ] || { echo "game DLC folder not found at $GAME" >&2; exit 1; }
[ -d "$DLC_SRC" ] || { echo "parked LEKMOD data not found at $DLC_SRC" >&2; exit 1; }
[ -d "$LEKMAP" ] || { echo "Lekmap scripts not found at $LEKMAP" >&2; exit 1; }
mkdir -p "$MODS"
echo "1/4 LEKMOD data -> Assets/DLC/MP_MODSPACK (415 MB)"
rsync -a --delete --exclude 'CvGameCore_Expansion2.dll' "$DLC_SRC/" "$GAME/Assets/DLC/MP_MODSPACK/"
echo "2/4 host DLL -> Assets/DLC/MP_MODSPACK (the game core the main menu runs, before any mod is on)"
cp "$HOST_DLL" "$GAME/Assets/DLC/MP_MODSPACK/CvGameCore_Expansion2.dll"
echo "3/4 mods -> MODS"
for d in "$STAGE"/MODS/*/; do
  n=$(basename "$d"); echo "-> MODS/$n"
  rsync -a --delete "$d" "$MODS/$n/"
done
echo "4/4 Lekmap map scripts -> Assets/Maps/Lekmap"
rsync -a --delete "$LEKMAP/" "$GAME/Assets/Maps/Lekmap/"
echo "done. In the game: Mods -> enable 'LEKMOD for Bazaar' and 'MCP Server for LEKMOD'; leave 'Local MCP Server DLL' off."
echo "Map Type -> a Lekmap script: the stock scripts never place LEKMOD's luxuries."
''' % (GAME, MODS, LEKMOD_DLC, STAGE, os.path.join(host_folder, "CvGameCore_Expansion2.dll"), LEKMAP))
    os.chmod(install, 0o755)
    print("wrote", install)


if __name__ == "__main__":
    main()
