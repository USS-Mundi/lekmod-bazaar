# Second Leader

Part of LEKMOD for Bazaar: a game option, **Second Leader For Everyone!** (Advanced Setup, first in the list). With it on, every major civilization
in the game also gets a random second civilization and its leader: the civilization its player picked (or the game
picked at random) plus one that no one else in the game has, neither as their own nor as their second.

## What merges

| Piece | Rule |
|---|---|
| The second leader's ability (its traits) | Merges row by row: the game core adds the two leaders' abilities together (`patches/lekmod-trait-merge.patch`), so +1 food on Sheep from one and +1 faith on Sheep from the other give both, and the same bonus twice adds up. Two things stay out, each named on the leader card and in the agents' file: a slot that holds one thing and both fill (a free building in every city or the capital or conquered cities, the settling unit's yields, the combat bonus beside an improvement, the free buildings' tech, a build's time), where the first's stays; and the second's bans (a unit it may not train, an improvement it may not build, an order its units may not take), which never pass on. Needs the patched game core: on LEKMOD's own core a merged trait can wipe the first's values. |
| The second civilization's unique units and buildings | Each merges unless the first civilization overrides the same class itself (its own unique stays). The second's bans (a class it cannot build) never pass on. |
| Free starting units | The second's merge where it starts with more of a class than the first (Nubia's free Scout, which its unique unit makes an Apedemak's Bow), with the first's unit AI where the first starts with that class too. Listed in the agents' file. |
| Unique improvements and free buildings | Never merge; they stay with their own civilization. |

An ability LEKMOD scripts in Lua (`Lua/Civilizations/Lekmod_<civ>.lua`, e.g. the UAE's or Kilwa's) checks the
player's own civilization, so the database merge cannot reach it. The `SecondLeaderScripts` context includes each
needed script for every player whose second's ability merged, with `Players` and `PreGame` showing that player's
second civilization inside that context only. LEKMOD's files are not edited. `tests/run_scripts.sh` drives it
against LEKMOD's installed scripts.

Screens list a civilization's uniques from `GameInfo.Civilization_UnitClassOverrides` / `_BuildingClassOverrides`
(tech tree, Civilopedia, culture overview, trade route tooltips), once, as each screen loads. The first time a Lua
environment calls `Game.GetActivePlayer` (the tech screens do just before they gather), the plugin runs
`src/SecondLeaderRows.lua` (embedded at build) in that caller's environment. It makes those two tables also yield each
merged second's units and buildings as rows of its player's own civilization. Running it where `Game` is registered does
not work: that state has no base library yet. `tests/run_rows.sh` drives it, through LEKMOD's own
`TechButtonInclude.lua`.

Nothing in the database changes: after-hooks answer the game's own questions (`CvLeaderHeadInfo::hasTrait`,
`CvCivilizationInfo::getCivilizationUnits` / `getCivilizationBuildings` / `isCivilization*Overridden`).

## When it is decided

Before players are set up (new game) and before a saved game's traits are read, from the setup: the option, the map
seed and who plays what. The same seed pairs the same, so a reload keeps its second leaders. City-states never get one.
A new game pairs the slots in play and records them in `My Games\Sid Meier's Civilization 5\Second Leader\merges\pairs-<map seed>.txt`
(a row `('Root', <folder>)` in `SecondLeaderSettings` moves this folder). A load pairs the
slots on that record when it still matches the setup: a killed player's slot is no longer in play, and left out it would
deal every survivor a different second. Without a matching record, a load pairs the slots in play.

## What it tells

- **Lua:** `Game.SecondLeaderOn()`, `Game.SecondLeader(player)` → `{ Civ, Leader, Traits, Units, Buildings }`, each piece
  `{ Type, Replaces, Merged, Clash }`, or nil, for any screen that shows it.
- **AI players and tools:** `merges/current.md` in the same folder, for the game loaded now: `# Merge random second
  leader: on|off`, a `Game: civ5-game-...` line once the game has its name, then every civilization's two halves in
  plain words, ready to hand to an AI player.
  Beside it, `merges/current.json` holds the same seats by type name (`civ`, `leader`, `second_civ`, `second_leader`,
  each with its `_name`), only the slots that take part, written with the option on or off.

## Build and tests

`build.sh` runs the tests (`tests/run_core.sh`: pairing, clash, overrides, text; `run_scripts.sh`; `run_rows.sh`) and
builds `SecondLeader.dll`; `generator/stage.py` packs it into LEKMOD for Bazaar. Check `Bazaar.log` for
`Plugin Second Leader 1 loaded`, and each pairing as `player N: CIV_X + CIV_Y`.
