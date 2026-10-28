# Patches to the upstream clones

Applied with `git apply` in the clone each one names. Both clones sit beside this
repository under `~/repos/civ5-dll/`.

**`bazaar-generator.patch`** — `civ5-bazaar`, two changes to the hook generator so
that it runs on macOS over a source tree other than its own:

- `common.py`: `BAZAAR_VS2008_INCLUDE`, when set, names the compiler's and the
  Windows SDK's include folders. Worker processes read it too, which a value set
  in one process would not reach.
- `generate.py`: the edit keys fold case on every platform, as Windows compares
  paths. Without it, a header a source includes under another spelling of its
  name loses its edits on a case-sensitive host, and its functions compile
  renamed with no matching declaration.

**`bazaar-host-setup-dlc-skin.patch`** — `civ5-bazaar`, `host/CivHostSetup.cpp`. The
Plugins screen's Continue opens the game's Advanced Setup from a copy the host
makes, and the host took the file from an active mod or else from the game's own
folders. A DLC pack's UI skin can carry its own `AdvancedSetup.lua` or `.xml`
(LEKMOD's does, and the game shows the skin's file over its own), so the host
now looks there too, the pack with the highest `Priority` first. Without it the
screen is the base game's, which lists only the civilizations of a DLC the
player owns, and LEKMOD's 89 never appear.

**`lekmod-cvplayer-if.patch`** — `Lekmod`, one line in `CvPlayer.h`. The guard
around `acquireCity` reads `#if !defined` with no macro named, and its branches
are the reverse of the ones in `CvPlayer.cpp`. With the macro named, header and
source agree, and `acquireCity` is a function the generator can hook.

**`lekmod-host-wiring.patch`** — `Lekmod`, the calls into Bazaar's host that
Bazaar's own engine copy carries by hand and its generator does not write. In
fifteen engine sources, under `#if defined(CIV_HOST_API)`, the host hears of the
database being cached, every game update, each new game, each player reset, the
save and load of `CvGame`'s supporting data, engine shutdown and the Plugins
screen's front-end call; every save path asks `CivHostSavingAllowed()` first;
and the Hand hears of popups, notifications, resolved deals, replaced units and
cities, cancelled unit orders and plugin-controlled players, as in Bazaar's
`CvGameCoreSource`. Without it the host compiles in but never runs: no
`Bazaar.log`, an empty Plugins screen, no plugin loaded.

**`lekmod-influence-no-capital.patch`** — `Lekmod`, one guard in
`CvPlayerCulture::GetInfluencePerTurn`. Its `LEK_YIELD_TOURISM` form reads the
capital's culture without checking that a capital exists, so a player with no
city yet crashes the game at the end of a turn. With the guard, such a player
has no influence per turn.

**`mcp-plugin-game-reference.patch`** — `civ5-mcp-plugin`, the game reference
at `GET /game-reference`, offered upstream as a pull request. Apply it before
`mcp-plugin-lekmod.patch`, and drop it once upstream has it.

**`mcp-plugin-lekmod.patch`** — `civ5-mcp-plugin`, the plugin's sources under
`MCP_LEKMOD`, which `generator/build_mcp.sh` defines. Every change is one of:
an argument VMC adds and LEKMOD lacks, dropped; an accessor Community Patch adds,
read through `plugin-compat/` instead; the attack preview's strength calls, on
LEKMOD's `CvCombatInfo` form of those functions; and the two diplomacy hook
points, under the names our hook-id registry gives them with LEKMOD's parameter
lists. Without the define the sources are the author's, unchanged.
