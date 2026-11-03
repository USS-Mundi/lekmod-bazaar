# civ5-mcp-plugin
A DLL Bazaar Plugin for Civilization 5 that allows an LLM with MCP functionality to play the game with you as a teammate, against you as an opponent, or play your turns for you.

## What you need

- Civilization V with Brave New World, on Windows.
- Bazaar, the multi-DLL mod loader, with its two mods: Bazaar Multi-DLL Mod Loader and Bazaar - VMC Bootstrap (required). Each release of this plugin says which Bazaar version it requires.
- An MCP client on the same PC, such as Claude Code.

## Installing

1. Install Bazaar's two mods.
2. Download the `.civ5mod` file from this plugin's release on GitHub, and put it in `Documents\My Games\Sid Meier's Civilization 5\MODS`.
3. In the game's Mods menu, turn on Bazaar - VMC Bootstrap (required), Bazaar Multi-DLL Mod Loader and MCP Server for Bazaar.
4. Start or load a single-player game. While it runs, the server is at `http://127.0.0.1:7676/mcp`.
5. Connect your MCP client to that address. For Claude Code: `claude mcp add --transport http civ5 http://127.0.0.1:7676/mcp`

This plugin replaces the Local MCP Server DLL mod, so turn that one off.

## Giving the model the game's rules

While a game is loaded, `http://127.0.0.1:7676/game-reference` returns the rules of that game as plain text: technologies, buildings, wonders, units, policies, beliefs, promotions, improvements, resources, terrain and specialists, with their costs and effects, and the abilities and unique units of the civilizations in the game. It also gives the rules at this game's difficulty and map size: what happiness, food and gold below zero cost, and what the computer players get. It is read from the game's own database, so mods are included. For an 8-civilization Brave New World game it is about 42,000 tokens.

With it in the system prompt, the model plans from the game's numbers rather than from memory, which gets many of them wrong. Load your game first, then start Claude Code with it (PowerShell):

```
curl.exe -s http://127.0.0.1:7676/game-reference -o game-reference.txt
claude --append-system-prompt-file game-reference.txt
```

Fetch it again for each new game, since it depends on the civilizations in it.

## Testing what a model knows

`tools/agent_quiz.py` quizzes a model on the rules of Brave New World without mods and scores its answers against the game's own data: 3,625 questions on costs, prerequisites, strengths, upgrades, yields, upkeep, abilities and global rules. It needs only Python 3. Build the answer key from your own install, make a quiz, give its sheet to the model and score the reply:

```
python tools/agent_quiz.py build "C:\Program Files (x86)\Steam\steamapps\common\Sid Meier's Civilization V\Assets"
python tools/agent_quiz.py make sample --set sampler
python tools/agent_quiz.py score sample
```

`python tools/agent_quiz.py -h` shows how to run Claude on the sheet with no tools, with or without the game reference. The key and the quizzes stay in `tools/agent-quiz/`, which git ignores.
## City-states an ally can switch on

A city-state's ally, the human or a model's seat, can ask for its active help. That switches it on, and a model may then play it as a seat. The game keeps its rules: influence, quests and who the ally is work as always, and the city-state never declares war or makes peace on its own.

- **Switching on:** a seat uses `city_state_action` with `switch_on`, or simply writes to the city-state with `tell_city_state`. The human writes to it in the log window's chat box, where the city-states they lead are listed beside their teammate seats. Until a model takes its seat, the game's AI keeps playing it.
- **Playing it:** `take_seat civ=<city-state>` while it is switched on (`status.seats_waiting_for_a_player` lists them). Its player plays every unit and its city's builds, sees its ally, the ally's standing and what each civ has done for it in `status`, and answers its ally with `chat`.
- **What influence buys:** how many of its units it sends out of its own land, whether its units' sight stays uncovered on the ally's map, and whether its units may enter the ally's land all scale with the ally's influence. Its player's `status` gives the influence at which each of those next opens (`next_opens_at_influence`). Once its sight reaches the ally's map, its player can also let chosen units see for the ally live, as the ally's own units do (`share_sight`); what they light is kept in the save and taken off on load. Each turn a model plays it, the ally pays extra influence.
- **Switching off:** it goes back to the game's AI when its ally lets go (`switch_off`, or the human writing `dismiss` to it in the log window), when its player releases the seat, when its alliance passes to another civ, or when its player is gone (no request for two minutes) as its turn comes.
- **Settings:** the steps, the text its player reads about its ally and the costs are tables in `MCPServer.sql` (`McpCityStateTiers`, `McpCityStateDispositions`, `McpCityStateSettings`). Change the numbers there; a missing table is reported in the log and built-in defaults apply.
- **Kept on disk:** every deed toward a city-state and every line of its conversations also go to a file for that game, `My Games\Sid Meier's Civilization 5\MCP\<game id>\city-states\<city-state>.md`, read back after a reload. Its player's `status` names the file (`whole_history_file`); the lists in `status` show only the latest.
- **Switching on is not kept in a save yet:** after a reload every city-state goes back to the game's AI.

## Making a release

Run `python tools/release.py check`, then `build`, then `publish --notes FILE`. Publishing needs the GitHub CLI, `gh`, logged in. The steps around them are in `.claude/skills/release/SKILL.md`.
