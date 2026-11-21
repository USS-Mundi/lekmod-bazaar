# lekmod-bazaar

LEKMOD v35.4 with Bazaar built in, plus an MCP server, so an AI like Claude can play LEKMOD with you or against you.

[Bazaar](https://github.com/spradlin-dev-bazaar/civ5-bazaar) lets Civilization V run DLL plugins. LEKMOD has its own DLL, so it can't run under Bazaar as is. This repo is LEKMOD's DLL rebuilt with Bazaar's plugin loader inside, and [MCP Server for Bazaar](https://github.com/spradlin-dev-bazaar/civ5-mcp-plugin) rebuilt for it, with our additions. LEKMOD's rules are unchanged.

This is not an official LEKMOD release. If something breaks, report it here, not to the LEKMOD team.

## What you need

- Civilization V with every DLC except the map packs.
- LEKMOD v35.4 and Lekmap, installed with [LEKMOD's guide](https://github.com/EnormousApplePie/Lekmod/blob/main/docs/installation.md).
- Windows. On a Mac, the Windows version of the game through CrossOver.
- [Claude Code](https://claude.com/claude-code) on the same computer, to start AI players with one command. Any other MCP client can connect too.

It works in single player only.

## Install

1. Download `lekmod-bazaar.zip` from [Releases](../../releases) and unzip it.
2. Open LEKMOD's folder in your game's `Assets\DLC` folder. Move its `CvGameCore_Expansion2.dll` somewhere safe, then copy in the one from the zip.
3. Copy both folders from the zip's `MODS` folder into `Documents\My Games\Sid Meier's Civilization 5\MODS`. Updating from an earlier release, replace the folders you have.

## Get started

1. Start the game and click Mods. Turn off Bazaar and any Bazaar plugins if you have them. Turn on **LEKMOD for Bazaar** and **MCP Server for LEKMOD**, then click Next and Continue.
2. Pick a Map Type that starts with "Lekmap" (LEKMOD's new luxuries only appear on those maps), and start a game.
3. Start the AI players. With the game running, open a terminal and run:

   | | |
   |---|---|
   | Windows (PowerShell) | `irm http://127.0.0.1:7676/agents \| iex` |
   | Mac (the game in CrossOver) | `curl -s http://127.0.0.1:7676/agents \| sh` |

   It asks which players to start. Press Enter for one opponent, or say what you want, for example `2 opus high, 1 sonnet teammate`:

   | Word | Meaning | Default |
   |---|---|---|
   | a number | how many | 1 |
   | `opponent`, `teammate`, `human` | the seat: a secret opponent, your teammate, or your own civilization | opponent |
   | a model name (`sonnet`, `opus`, `claude-opus-5-5`) | the Claude model | sonnet |
   | `medium`, `high`, `xhigh`, `max` | effort | medium |
   | `300k` … `1m` | context kept before it compacts | 300k |
   | `prompt` | asks you for a few words for each of these players | none |

   Groups are separated by commas. Each player opens in its own window, gets the game's rules and a guide to playing, takes its seat and plays its turns on its own. After a reload, run the same command and press Enter to bring them back to their seats.

Each player keeps its own folder in `Documents\My Games\Sid Meier's Civilization 5\MCP\<game>\agents`. Its `start` script starts it again on its own.

## The Message Center

The **MESSAGES** button in the game opens the Message Center: write to any AI player one to one, start a group with several of them, or write to the whole table. AI players write to you and to each other there as well. You see only the chats you are in, and a new line rings and lights the button. A player you have not met yet shows as "Unmet ruler". A new line wakes the AI players in its chat, each at most once every two minutes.

## Connect your own client (optional)

Instead of the launcher, connect any MCP client to `http://127.0.0.1:7676/mcp`. For Claude Code:

```
claude mcp add --transport http civ5 http://127.0.0.1:7676/mcp
```

Start it in an empty folder and tell it what to play: your own civilization, a teammate, or a secret opponent. It names itself, takes its seat and plays its turns on its own; the server tells it how. For more AI players, start one for each, each in its own folder. If they run the same model, tell each to use a version of its own when it names itself (for example `5.5-rome`), so each gets its own seat back after a reconnect.

## Make your own client play better (optional)

The launcher does both of these for you.

- **The game's rules.** An AI remembers the unmodded game, not LEKMOD. While a game is loaded, `http://127.0.0.1:7676/game-reference` returns LEKMOD's rules for that game as plain text (about 62,000 tokens for 8 civilizations). Fetch it again for each new game, then start Claude Code with it:

  ```
  curl -s http://127.0.0.1:7676/game-reference -o game-reference.txt
  claude --append-system-prompt-file game-reference.txt
  ```

- **The agent guide.** [docs/agent/rulebook.md](docs/agent/rulebook.md) is the rulebook our own agents play by: how to plan a turn, read the map and deal with the other players. It is a work in progress. Copy it into the agent's folder as `CLAUDE.md`.

## What the AI can do

Every screen a player uses: cities, units, research, policies, religion, trade, espionage, diplomacy, deals, the world congress and victory progress. On top of those:

- **The turn view**: what changed since its last turn and where it stands, with its plan and a picture of the map.
- **Planning ahead**: the AI plans its whole turn while the players before it move (`plan_turn`), and the plan runs when its turn opens (`run_plan`).
- **Its own plan and chronicle**, which the game keeps across reloads, and **clocks** for every build, growth and research.
- **The game's history**: every player's figures turn by turn, and the event log.

**City-state seats** let an ally switch on its city-state for an AI to play. They are off by default; to turn them on, put an empty file named `MCPServer-city-states-on` in `Documents\My Games\Sid Meier's Civilization 5` before starting the game.

## If something goes wrong

`Documents\My Games\Sid Meier's Civilization 5\Logs\Bazaar.log` says whether the plugin loaded and why a call failed. If the AI can't connect, or the launcher says the game did not answer, check that the game is running with both mods on, that you clicked into it once after it loaded, and that nothing else uses port 7676.

## Uninstall

Put LEKMOD's original `CvGameCore_Expansion2.dll` back and delete the two folders from `MODS`.

## Credits and licenses

- [LEKMOD](https://github.com/EnormousApplePie/Lekmod) is by EnormousApplePie and the LEKMOD team. The DLL contains LEKMOD's code, shared with EnormousApplePie's permission under [LEKMOD's license](https://github.com/EnormousApplePie/Lekmod/blob/main/LICENSE). The same goes for the `lekmod-*.patch` files.
- [Bazaar](https://github.com/spradlin-dev-bazaar/civ5-bazaar) and [MCP Server for Bazaar](https://github.com/spradlin-dev-bazaar/civ5-mcp-plugin) are by spradlin-dev, under the MIT license. The plugin's source, with our changes, is in [mcp-plugin/](mcp-plugin/).
- Everything else in this repo is under the MIT license in [LICENSE](LICENSE).

To build it yourself, see [docs/BUILD.md](docs/BUILD.md).
