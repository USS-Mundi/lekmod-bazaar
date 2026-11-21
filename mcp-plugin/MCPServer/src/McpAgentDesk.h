// The agent launcher's thinking: what a player asks for ("2 opus high, 1 sonnet teammate"), each agent's folder, its
// settings and system prompt, and the script that starts it in its own terminal window. The plugin answers the
// launcher script with it (GET and POST /agents); the script only reads a line and opens windows.
// No game headers, so it is tested natively (tests/run_agent_desk_test.sh). Paths are UTF-8.
#pragma once

#include <string>
#include <vector>

// What the game knows at the moment of a launch, published by the game thread (the launch runs on the HTTP thread,
// often while the game is paused behind a terminal).
struct McpDeskKit
{
	std::string gameId;                  // "" before a game is loaded
	std::string folder;                  // MCP\<game id>\agents, as the plugin writes it
	std::string hostFolder;              // the same folder as the player's own programs name it
	bool unixHost;                       // the game runs under Wine on a Mac: sh scripts and Unix paths
	std::string mcpUrl;                  // http://127.0.0.1:7676/mcp
	std::string rulebook;                // the agents' rulebook (docs/agent/rulebook.md, built in)
	std::string referencePath;           // this game's rules as the plugin wrote them; "" when not written yet
	int freeOpponents;                   // civilizations the game's AI plays that a new agent may take
	int freeTeammates;
	bool screenFree;                     // no model plays the human's own civilization
	std::vector<std::string> running;    // agents a live session plays now ("agent-3")
	McpDeskKit() : unixHost(false), freeOpponents(0), freeTeammates(0), screenFree(true) {}
};

// One group of a table: "2 opus high teammate prompt 400k".
struct McpDeskGroup
{
	int count;
	std::string model;  // default sonnet
	std::string effort; // default medium; low runs as medium
	std::string role;   // opponent (default), teammate or human
	int context;        // tokens kept before compacting, default 300000
	bool prompt;        // ask the player for a prompt for each of these
	McpDeskGroup() : count(1), model("sonnet"), effort("medium"), role("opponent"), context(300000), prompt(false) {}
};

static const int MCP_DESK_MAX_AGENTS = 12;

// Parses a table; false with a sentence naming the bad group and word.
bool McpDeskParseTable(const std::string& table, std::vector<McpDeskGroup>& out, std::string& err);

// The question the launcher asks, with what Enter does: bring back this game's idle agents, or start one.
std::string McpDeskQuestion(const McpDeskKit& kit);

// The launcher script for this host: sh under Wine, PowerShell on Windows. It asks the question, posts the answer to
// POST /agents and carries out the reply.
std::string McpDeskScript(const McpDeskKit& kit, const std::string& agentsUrl);

// Carries out a table (empty: bring back the idle agents, or start one when there are none): writes the folders and
// returns the reply the script runs, one instruction per line, tab-separated:
//   say <text>             print it
//   ask <folder> <title>   ask the player for this agent's prompt and write <folder>/prompt.md
//   run <title> <script>   open a terminal window running the script
//   pause <seconds>        wait before the next line (agents claiming seats at once can land on the same one)
// A table that does not fit (more opponents than are free, say) writes nothing and answers with say lines only.
// newSessionId names a new agent's Claude Code session (a UUID).
std::string McpDeskRun(const McpDeskKit& kit, const std::string& table, std::string (*newSessionId)());
