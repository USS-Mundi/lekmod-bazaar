# Building LEKMOD for Bazaar and the MCP plugin

LEKMOD for Bazaar is LEKMOD's game core with Bazaar's plugin loader generated into it,
so that Bazaar plugins run on LEKMOD's rules. The MCP Server plugin is rebuilt
against it. Both ship as ordinary mod folders; LEKMOD's own data ships as the
DLC pack it has always been.

Everything compiles on this Mac. Visual C++ 2008 runs under a wine prefix of its
own and is driven from the shell; no Windows machine and no IDE are involved.

## Where things are

| | |
|---|---|
| Bazaar clone, with our generator patch applied | `~/repos/civ5-dll/bazaar` |
| LEKMOD clone (v35.4), with our one-line fix applied | `~/repos/civ5-dll/lekmod` |
| MCP plugin clone, with our LEKMOD patch applied | `~/repos/civ5-dll/mcp-plugin` |
| Bazaar SDK, unzipped from Bazaar's Releases page (`Bazaar-SDK-<version>.zip`) | `sdk-download/` |
| Build wine prefix | `~/repos/civ5-dll/.winebuild` |
| The compiler and Windows SDK inside it | `C:\vc9`, `C:\sdk6` |
| Generated hooked source, plugin header, logs | `build/civgen`, `build/*.log` |
| Staged mod folders and the install script | `build/stage` |

The patches are in `patches/`, with what each one is for. Apply them with
`git apply` in the clone each names before building.

## Setting up once

**The compiler.** Install Visual C++ 2008 Express with SP1 into the CrossOver
bottle that runs Civilization V (the ISO survives on the Internet Archive; the
Bazaar SDK guide links it). Then copy its `VC` folder to `C:\vc9`, its
`Common7\IDE` folder to `C:\vc9\ide`, and the Windows SDK v6.0A to `C:\sdk6`,
all inside the build prefix. The short paths matter: Visual C++ 2008 fails once a
path inside a build passes 260 characters.

**The generator's Python.** In the Bazaar clone, `python3 -m venv .venv-mac` and
`.venv-mac/bin/pip install -r tools/generator/requirements.txt`. The pinned
libclang parses the game's headers natively on macOS.

## Building, in order

Each script says what it did and leaves a log in `build/`.

**1. `generator/run_lekmod.py --generate`** — runs Bazaar's hook generator over
LEKMOD's source instead of Bazaar's own. It parses the 161 engine sources (about
twenty seconds), then writes the hooked mirror of the source under
`build/civgen/src`, the hook table, and the plugin header
`build/civgen/sdk/CivHooks.h`. It keeps its own hook-id registry in
`generator/hook-ids.txt`, seeded from Bazaar's so functions both have keep the
same numbers. It stops before generating if a parse error sits outside a
function body; `--generate` is the flag that runs the second half.

**2. `generator/build_host.sh`** — compiles the mirror, Bazaar's host sources and
the hook table with Visual C++ 2008, from LEKMOD's own Release settings plus
what Bazaar adds, and links `C:\lek\out\CvGameCore_Expansion2.dll` with its
import library. About ten minutes. The link emits one warning for every object
in Firaxis's prebuilt libraries, since their PDBs were never shipped; nothing
else should appear.

**3. `generator/build_mcp.sh`** — compiles the MCP plugin against the mirror's
headers, the generated plugin header and the host's import library, with
`plugin-compat/` force-included: the VMC option flags the plugin tests, defined
false, and the few reads LEKMOD has no public form for. Links
`C:\lek\out\plugin\MCPServer.dll`. About two minutes.

**4. `generator/stage.py`** — lays out `build/stage/MODS/LEKMOD for Bazaar (v 1)` and
`build/stage/MODS/MCP Server for LEKMOD (v 1)`, each with its DLL, its files and
a modinfo written here, and writes `build/stage/install.sh` and the release
download, `build/stage/lekmod-bazaar.zip`. Touches nothing in the game.

**5. `build/stage/install.sh`**, by hand — copies LEKMOD's parked DLC pack into
the game's `Assets/DLC/MP_MODSPACK` with the host DLL at the pack root in place of LEKMOD's own, the two staged mod
folders into the game's `MODS` folder, and LEKMOD's Lekmap map scripts (the
`Lekmap` folder of the LEKMOD clone) into the game's `Assets/Maps/Lekmap`. Only
those scripts place LEKMOD's luxuries, so a game on a stock map type has none.
The parked copies under `~/repos/lekmod-mac` stay as they are, so undoing it is
deleting what it added.

**6. `generator/publish.sh <version>`**, with everything committed — creates the
public repository on its first run, pushes, and attaches the zip to a GitHub
release with that tag. Needs the GitHub CLI, `gh`, logged in.

## Checking it

The host DLL should be within a few kilobytes of Bazaar's own release build, and
its import library should name some thirty thousand symbols. In the game: Mods,
turn on LEKMOD for Bazaar and MCP Server for LEKMOD, leave Local MCP Server DLL and
Bazaar itself off (LEKMOD for Bazaar blocks both: the game can be handed one DLL path
only). Start a game. `Documents\My Games\Sid Meier's Civilization 5\Logs\Bazaar.log`
should show the plugin loaded, and the AI Log button should be on screen.

## What is left out, and why

Bazaar's layout check (`layouts.py`) compares class layouts against the record
Bazaar's own plugins were built for. LEKMOD's classes differ from Bazaar's on
purpose, and the only plugin here is built against LEKMOD's headers, so the
check has nothing to protect; it is not run.

The VMC Bootstrap mod is not installed. It carries database files for VMC, which
Bazaar's engine includes and LEKMOD's does not.
