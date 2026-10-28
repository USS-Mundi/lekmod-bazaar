# lekmod-bazaar

LEKMOD v35.4 with Bazaar built in, plus the MCP server plugin, so you can play LEKMOD with AI in the loop: an AI like Claude can play with you or against you.

[Bazaar](https://github.com/spradlin-dev-bazaar/civ5-bazaar) lets Civilization V run several DLL mods at once. LEKMOD has its own DLL, so it can't run under Bazaar as is. This repo is LEKMOD's DLL rebuilt with Bazaar's plugin loader inside. LEKMOD's rules are unchanged.

This is not an official LEKMOD release. If something breaks, report it here, not to the LEKMOD team.

## What's in the download

- **LEKMOD for Bazaar**: LEKMOD's DLL with Bazaar built in. Turn it on instead of Bazaar.
- **MCP Server for LEKMOD**: [MCP Server for Bazaar](https://github.com/spradlin-dev-bazaar/civ5-mcp-plugin), rebuilt for this DLL. It has its own name, so it can sit next to the Workshop version.

Plugins from the Steam Workshop are built for Bazaar's own DLL, so they need rebuilding before they work here.

## What you need

- Civilization V with every DLC except the map packs.
- LEKMOD v35.4 and Lekmap, installed with [LEKMOD's guide](https://github.com/EnormousApplePie/Lekmod/blob/main/docs/installation.md).
- Windows. On a Mac, use the Windows version of the game through CrossOver.
- An MCP client on the same computer, such as Claude Code.

It works in single player only.

## Install

1. Download `lekmod-bazaar.zip` from [Releases](../../releases) and unzip it.
2. Open LEKMOD's folder in your game's `Assets\DLC` folder. Move its `CvGameCore_Expansion2.dll` somewhere safe, then copy in the one from the zip.
3. Copy both folders from the zip's `MODS` folder into `Documents\My Games\Sid Meier's Civilization 5\MODS`.

## Play

1. Start the game and click Mods. Turn off Bazaar and any Bazaar plugins if you have them. Turn on LEKMOD for Bazaar and MCP Server for LEKMOD.
2. Click Next, then Continue.
3. Pick a Map Type that starts with "Lekmap". LEKMOD's new luxuries only appear on those maps.
4. Start the game, then connect your MCP client to `http://127.0.0.1:7676/mcp`. For Claude Code:

   ```
   claude mcp add --transport http civ5 http://127.0.0.1:7676/mcp
   ```

If the AI can't connect, check `Documents\My Games\Sid Meier's Civilization 5\Logs\Bazaar.log`. It says whether the plugin loaded.

## Giving the AI the game's rules

While a game is loaded, `http://127.0.0.1:7676/game-reference` returns LEKMOD's rules for that game as plain text: technologies, buildings, wonders, units, policies, beliefs, promotions, improvements, resources and terrain, with their costs and effects, and the civilizations in the game. An AI's memory knows the unmodded game, not LEKMOD, so it needs this to plan. For 8 civilizations it is about 62,000 tokens. Load your game first, then start Claude Code with it:

```
curl -s http://127.0.0.1:7676/game-reference -o game-reference.txt
claude --append-system-prompt-file game-reference.txt
```

Fetch it again for each new game.

## Uninstall

Put LEKMOD's original `CvGameCore_Expansion2.dll` back and delete the two folders from `MODS`.

## Credits and licenses

- [LEKMOD](https://github.com/EnormousApplePie/Lekmod) is by EnormousApplePie and the LEKMOD team. The DLL contains LEKMOD's code, shared with EnormousApplePie's permission under [LEKMOD's license](https://github.com/EnormousApplePie/Lekmod/blob/main/LICENSE). The same goes for the `lekmod-*.patch` files.
- [Bazaar](https://github.com/spradlin-dev-bazaar/civ5-bazaar) and [MCP Server for Bazaar](https://github.com/spradlin-dev-bazaar/civ5-mcp-plugin) are by spradlin-dev, under the MIT license.
- Everything else in this repo is under the MIT license in [LICENSE](LICENSE).

To build it yourself, see [docs/BUILD.md](docs/BUILD.md).
