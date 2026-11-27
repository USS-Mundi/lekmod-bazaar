#include "McpAgentDesk.h"
#include "McpFiles.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>

namespace
{
	const char* EFFORTS[] = { "low", "medium", "high", "xhigh", "max" };
	const int MIN_CONTEXT = 200000, MAX_CONTEXT = 1000000;
	const char* FIRST_PROMPT = "/loop read your instructions, then play";
	const char* RESTART_PROMPT = "/loop you were restarted: take your seat back, then play";

	std::string Str(int n)
	{
		std::ostringstream o;
		o << n;
		return o.str();
	}

	std::string Lower(const std::string& s)
	{
		std::string out(s);
		for(size_t i = 0; i < out.size(); ++i) if(out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
		return out;
	}

	std::string Trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r\n");
		if(a == std::string::npos) return "";
		size_t b = s.find_last_not_of(" \t\r\n");
		return s.substr(a, b - a + 1);
	}

	std::vector<std::string> Split(const std::string& s, char sep)
	{
		std::vector<std::string> out;
		size_t start = 0;
		for(;;)
		{
			size_t end = s.find(sep, start);
			out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if(end == std::string::npos) break;
			start = end + 1;
		}
		return out;
	}

	std::vector<std::string> Words(const std::string& s)
	{
		std::vector<std::string> out;
		std::istringstream in(s);
		std::string w;
		while(in >> w) out.push_back(w);
		return out;
	}

	bool Digits(const std::string& s)
	{
		if(s.empty() || s.size() > 7) return false;
		for(size_t i = 0; i < s.size(); ++i) if(s[i] < '0' || s[i] > '9') return false;
		return true;
	}

	// "400k" or "1m": tokens; -1 when the word is not a context.
	int Context(const std::string& lower)
	{
		if(lower.size() < 2) return -1;
		char unit = lower[lower.size() - 1];
		std::string n = lower.substr(0, lower.size() - 1);
		if((unit != 'k' && unit != 'm') || !Digits(n)) return -1;
		return atoi(n.c_str()) * (unit == 'k' ? 1000 : 1000000);
	}

	// Claude's model names: an alias (sonnet, opus, haiku, fable, opusplan), with [1m] or not, or a full claude- id.
	bool ModelName(const std::string& word)
	{
		std::string w = Lower(word);
		if(w.empty() || w.size() > 64) return false;
		const char* aliases[] = { "sonnet", "opus", "haiku", "fable", "opusplan" };
		std::string base = w.size() > 4 && w.compare(w.size() - 4, 4, "[1m]") == 0 ? w.substr(0, w.size() - 4) : w;
		bool known = w.compare(0, 7, "claude-") == 0;
		for(size_t a = 0; !known && a < sizeof(aliases) / sizeof(aliases[0]); ++a) known = base == aliases[a];
		if(!known) return false;
		for(size_t i = 0; i < w.size(); ++i)
		{
			char c = w[i];
			bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '[' || c == ']';
			if(!ok) return false;
		}
		return true;
	}

	struct Agent
	{
		std::string name; // agent-3
		int number;
		std::map<std::string, std::string> info; // agent.txt
	};

	bool ByNumber(const Agent& a, const Agent& b) { return a.number < b.number; }

	std::string Join(const std::string& folder, const std::string& name) { return folder + McpPathSep() + name; }

	std::string HostJoin(const McpDeskKit& k, const std::string& a, const std::string& b) { return a + (k.unixHost ? "/" : "\\") + b; }

	std::vector<Agent> Agents(const McpDeskKit& k)
	{
		std::vector<Agent> out;
		std::vector<std::string> dirs = McpDirsIn(k.folder, "agent-");
		for(size_t i = 0; i < dirs.size(); ++i)
		{
			std::string n = dirs[i].substr(6);
			std::string text;
			if(!Digits(n) || !McpReadFile(Join(Join(k.folder, dirs[i]), "agent.txt"), text)) continue;
			Agent a;
			a.name = dirs[i];
			a.number = atoi(n.c_str());
			std::vector<std::string> lines = Split(text, '\n');
			for(size_t l = 0; l < lines.size(); ++l)
			{
				std::string line = Trim(lines[l]);
				size_t eq = line.find('=');
				if(eq != std::string::npos) a.info[line.substr(0, eq)] = line.substr(eq + 1);
			}
			if(a.info["session"].empty() || !ModelName(a.info["model"])) continue;
			out.push_back(a);
		}
		std::sort(out.begin(), out.end(), ByNumber);
		return out;
	}

	bool Running(const McpDeskKit& k, const std::string& name)
	{
		return std::find(k.running.begin(), k.running.end(), k.gameId + "/" + name) != k.running.end();
	}

	std::string RoleParagraph(const std::string& role, const std::string& name)
	{
		std::string seat;
		if(role == "teammate") seat = "You are the human's teammate: take your seat with take_seat with civ=teammate.";
		else if(role == "human") seat = "You play the human's own civilization: take your seat with take_seat with civ=human.";
		else seat = "You are an opponent of the human: take your seat with take_seat with civ=opponent.";
		return "# Your seat in this game\n\n" + seat + " When you name yourself with set_identity, give as version your "
			"model's version followed by -" + name + " (for example 5.5-" + name + "), the same on every connection: it "
			"is how your seat finds its way back to you. NOTES.md in your folder is your memory; read it when you start.\n";
	}

	std::string Settings()
	{
		// dontAsk refuses whatever is not allowed here, so an agent never stops to ask. The hook's words carry no quote
		// or other character a shell would read, so it runs the same under sh, cmd and PowerShell.
		// Claude Code's own tools outside the allow list are denied by name as well: dontAsk would refuse them anyway,
		// and a denied tool leaves the request, where together they outweigh the game's tools. A name this Claude Code
		// does not have is ignored.
		return "{\n"
			"  \"permissions\": {\n"
			"    \"defaultMode\":\"dontAsk\",\n"
			"    \"allow\": [\"mcp__civ5\", \"Read(./**)\", \"Edit(./NOTES.md)\"],\n"
			"    \"deny\": [\"mcp__civ5__lookup\", \"Agent\", \"Task\", \"Artifact\", \"ArtifactComments\", \"ArtifactData\", \"AskUserQuestion\", "
			"\"Bash\", \"CronCreate\", \"CronDelete\", \"CronList\", \"DesignSync\", \"EnterPlanMode\", \"ExitPlanMode\", \"EnterWorktree\", "
			"\"ExitWorktree\", \"ListAgents\", \"McpTaskCheck\", \"Monitor\", \"NotebookEdit\", \"PushNotification\", \"RemoteTrigger\", "
			"\"ReportFindings\", \"ScheduleWakeup\", \"SendMessage\", \"SendToProject\", \"SendToProjectContext\", \"Skill\", \"TaskStop\", "
			"\"WebFetch\", \"WebSearch\", \"Workflow\", \"Write\", \"advisor\", \"mcp__easel__chart\"]\n"
			"  },\n"
			"  \"hooks\": {\n"
			"    \"SessionStart\": [{\"matcher\": \"compact\", \"hooks\": [{\"type\": \"command\", \"command\": "
			"\"echo Your context was just compacted. Read NOTES.md in your folder again. If it is your turn, call "
			"wait_for_my_turn once: it returns the view of this turn again.\"}]}]\n"
			"  }\n"
			"}\n";
	}

	// The header names the game too: every game numbers its agents from agent-1.
	std::string AgentHeader(const McpDeskKit& k, const std::string& name) { return k.gameId + "/" + name; }

	std::string McpConfig(const McpDeskKit& k, const std::string& name)
	{
		return "{\"mcpServers\":{\"civ5\":{\"type\":\"http\",\"url\":\"" + k.mcpUrl + "\",\"headers\":{\"X-Lekmod-Agent\":\"" + AgentHeader(k, name) + "\"}}}}\n";
	}

	struct Env
	{
		const char* name;
		std::string value;
	};

	// What every agent runs with. The wait for a turn can hold a tool call for up to an hour, and the turn view runs
	// to tens of thousands of tokens; the game's tools load with the session; no CLAUDE.md from any folder is read.
	std::vector<Env> Environment(const std::map<std::string, std::string>& info)
	{
		std::map<std::string, std::string> i(info);
		Env e[] = {
			{ "CLAUDE_CODE_AUTO_COMPACT_WINDOW", i["context"] },
			{ "CLAUDE_CODE_EFFORT_LEVEL", i["effort"] },
			{ "CLAUDE_CODE_DISABLE_CLAUDE_MDS", "1" },
			{ "MCP_TOOL_TIMEOUT", "3900000" },
			{ "CLAUDE_CODE_MCP_TOOL_IDLE_TIMEOUT", "0" },
			{ "CLAUDE_CODE_MCP_AUTO_BACKGROUND_MS", "0" },
			{ "MAX_MCP_OUTPUT_TOKENS", "100000" },
			{ "ENABLE_TOOL_SEARCH", "false" },
		};
		return std::vector<Env>(e, e + sizeof(e) / sizeof(e[0]));
	}

	std::string ClaudeArgs(const std::map<std::string, std::string>& info)
	{
		std::map<std::string, std::string> i(info);
		return "--model " + i["model"] + " --setting-sources project,local --strict-mcp-config --mcp-config mcp.json "
			"--append-system-prompt-file .system-prompt.full.md";
	}

	std::string ShScript(const McpDeskKit& k, const Agent& a)
	{
		std::map<std::string, std::string> i(a.info);
		std::string s = "#!/bin/sh\n"
			"# " + a.name + " of " + k.gameId + ": " + i["model"] + ", " + i["effort"] + " effort. Run it, or double-click it, to start this agent again.\n"
			"cd \"$(dirname \"$0\")\" || exit 1\n"
			"printf '\\033]0;%s\\007' '" + i["title"] + "'\n"
			"if command -v lekmod-agents-hook >/dev/null 2>&1; then lekmod-agents-hook prepare \"$PWD\"; fi\n"
			"cat system-prompt.md > .system-prompt.full.md\n"
			"if [ -f prompt.md ]; then printf '\\n\\n# From the person who started you\\n\\n' >> .system-prompt.full.md; "
			"cat prompt.md >> .system-prompt.full.md; fi\n"
			"if [ -f extra-prompt.md ]; then printf '\\n\\n' >> .system-prompt.full.md; cat extra-prompt.md >> .system-prompt.full.md; fi\n";
		std::vector<Env> env = Environment(a.info);
		s += "export";
		for(size_t e = 0; e < env.size(); ++e) s += std::string(" ") + env[e].name + "=" + env[e].value;
		// Its own session when there is one; the first start (or one that never got going) finds none and makes it.
		s += "\nunset MAX_THINKING_TOKENS\n"
			"claude " + ClaudeArgs(a.info) + " --resume " + i["session"] + " '" + RESTART_PROMPT + "' ||\n"
			"  claude " + ClaudeArgs(a.info) + " --session-id " + i["session"] + " '" + FIRST_PROMPT + "'\n"
			"if command -v lekmod-agents-hook >/dev/null 2>&1; then lekmod-agents-hook exited \"$PWD\"; fi\n";
		return s;
	}

	std::string CmdScript(const McpDeskKit& k, const Agent& a)
	{
		std::map<std::string, std::string> i(a.info);
		std::string s = "@echo off\n"
			"rem " + a.name + " of " + k.gameId + ": " + i["model"] + ", " + i["effort"] + " effort. Double-click it to start this agent again.\n"
			"chcp 65001 >nul\n"
			"cd /d \"%~dp0\"\n"
			"title " + i["title"] + "\n"
			"where lekmod-agents-hook >nul 2>nul && call lekmod-agents-hook prepare \"%CD%\"\n"
			"copy /y /b system-prompt.md .system-prompt.full.md >nul\n"
			"if exist prompt.md (\n"
			"  >>.system-prompt.full.md echo.\n"
			"  >>.system-prompt.full.md echo # From the person who started you\n"
			"  >>.system-prompt.full.md echo.\n"
			"  type prompt.md >>.system-prompt.full.md\n"
			")\n"
			"if exist extra-prompt.md (\n"
			"  >>.system-prompt.full.md echo.\n"
			"  type extra-prompt.md >>.system-prompt.full.md\n"
			")\n";
		std::vector<Env> env = Environment(a.info);
		for(size_t e = 0; e < env.size(); ++e) s += std::string("set \"") + env[e].name + "=" + env[e].value + "\"\n";
		s += "set MAX_THINKING_TOKENS=\n"
			"call claude " + ClaudeArgs(a.info) + " --resume " + i["session"] + " \"" + RESTART_PROMPT + "\"\n"
			"if errorlevel 1 call claude " + ClaudeArgs(a.info) + " --session-id " + i["session"] + " \"" + FIRST_PROMPT + "\"\n"
			"where lekmod-agents-hook >nul 2>nul && call lekmod-agents-hook exited \"%CD%\"\n";
		std::string crlf;
		for(size_t c = 0; c < s.size(); ++c) crlf += s[c] == '\n' ? std::string("\r\n") : std::string(1, s[c]);
		return crlf;
	}

	std::string ScriptName(const McpDeskKit& k) { return k.unixHost ? "start.command" : "start.cmd"; }

	// Writes what an agent starts from: its settings, the game's server, its system prompt as the rulebook and the
	// reference are now, and its start script. agent.txt and NOTES.md are written only when missing.
	bool Prepare(const McpDeskKit& k, const Agent& a)
	{
		std::string dir = Join(k.folder, a.name);
		McpMakeDirs(Join(dir, ".claude"));
		bool ok = true;
		if(!McpFileExists(Join(dir, "agent.txt")))
		{
			std::string text;
			const char* keys[] = { "title", "role", "model", "effort", "context", "session" };
			std::map<std::string, std::string> i(a.info);
			for(size_t n = 0; n < sizeof(keys) / sizeof(keys[0]); ++n) text += std::string(keys[n]) + "=" + i[keys[n]] + "\n";
			ok = McpWriteFile(Join(dir, "agent.txt"), text, false) && ok;
		}
		if(!McpFileExists(Join(dir, "NOTES.md"))) ok = McpWriteFile(Join(dir, "NOTES.md"), "", false) && ok;
		ok = McpWriteFile(Join(Join(dir, ".claude"), "settings.json"), Settings(), false) && ok;
		ok = McpWriteFile(Join(dir, "mcp.json"), McpConfig(k, a.name), false) && ok;
		std::string reference;
		if(!k.referencePath.empty()) McpReadFile(k.referencePath, reference);
		std::map<std::string, std::string> i(a.info);
		std::string prompt = k.rulebook + "\n\n" + RoleParagraph(i["role"], a.name) + (reference.empty() ? "" : "\n\n" + reference);
		ok = McpWriteFile(Join(dir, "system-prompt.md"), prompt, false) && ok;
		ok = McpWriteFile(Join(dir, ScriptName(k)), k.unixHost ? ShScript(k, a) : CmdScript(k, a), false) && ok;
		return ok;
	}

	std::string Say(const std::string& text) { return "say\t" + text + "\n"; }

	std::string Plural(int n, const char* one, const char* many) { return Str(n) + " " + (n == 1 ? one : many); }

	std::string Free(const McpDeskKit& k)
	{
		std::string s = Plural(k.freeOpponents, "opponent", "opponents") + ", " + Plural(k.freeTeammates, "teammate", "teammates");
		if(k.screenFree) s += " and your own civilization";
		return s;
	}

	std::string Title(const std::string& role, int n)
	{
		if(role == "human") return "Your civilization";
		return (role == "teammate" ? "Teammate " : "Opponent ") + Str(n);
	}
}

bool McpDeskParseTable(const std::string& table, std::vector<McpDeskGroup>& out, std::string& err)
{
	out.clear();
	std::vector<std::string> groups = Split(table, ',');
	int total = 0;
	for(size_t g = 0; g < groups.size(); ++g)
	{
		std::vector<std::string> words = Words(groups[g]);
		if(words.empty()) continue;
		McpDeskGroup group;
		bool count = false, model = false, effort = false, role = false, context = false;
		for(size_t w = 0; w < words.size(); ++w)
		{
			std::string word = words[w], lower = Lower(word);
			std::string seen;
			if(Digits(lower))
			{
				if(count) seen = "count";
				count = true;
				group.count = atoi(lower.c_str());
			}
			else if(std::find(EFFORTS, EFFORTS + 5, lower) != EFFORTS + 5)
			{
				if(effort) seen = "effort";
				effort = true;
				group.effort = lower == "low" ? "medium" : lower;
			}
			else if(lower == "opponent" || lower == "opponents" || lower == "teammate" || lower == "teammates" || lower == "human")
			{
				if(role) seen = "role";
				role = true;
				group.role = lower == "human" ? "human" : (lower[0] == 'o' ? "opponent" : "teammate");
			}
			else if(lower == "prompt" || lower == "prompts") group.prompt = true;
			else if(Context(lower) >= 0)
			{
				int tokens = Context(lower);
				if(tokens < MIN_CONTEXT || tokens > MAX_CONTEXT)
				{
					err = "'" + word + "': the context kept before compacting goes from 200k to 1m.";
					return false;
				}
				if(context) seen = "context";
				context = true;
				group.context = tokens;
			}
			else if(ModelName(word))
			{
				if(model) seen = "model";
				model = true;
				group.model = lower.compare(0, 7, "claude-") == 0 ? word : lower;
			}
			else
			{
				err = "'" + word + "' is not a count, a model (sonnet, opus, haiku, fable, or a full claude- name), an effort "
					"(medium, high, xhigh, max), a seat (opponent, teammate, human), a context (300k to 1m) or prompt.";
				return false;
			}
			if(!seen.empty())
			{
				err = "'" + Trim(groups[g]) + "' names a " + seen + " twice ('" + word + "'). Separate groups with commas.";
				return false;
			}
		}
		if(group.count < 1) { err = "'" + Trim(groups[g]) + "' asks for no agents."; return false; }
		total += group.count;
		out.push_back(group);
	}
	if(total > MCP_DESK_MAX_AGENTS)
	{
		err = "That is " + Str(total) + " agents; start at most " + Str(MCP_DESK_MAX_AGENTS) + " at once.";
		return false;
	}
	return true;
}

std::string McpDeskQuestion(const McpDeskKit& k)
{
	std::vector<Agent> agents = Agents(k);
	int idle = 0;
	for(size_t i = 0; i < agents.size(); ++i) if(!Running(k, agents[i].name)) ++idle;
	std::string enter = idle > 0 ? (idle == 1 ? std::string("bring back your agent") : "bring back your " + Str(idle) + " agents") : std::string("1 sonnet");
	return "Agents for this game, e.g. \"2 opus high, 1 sonnet teammate\" [Enter: " + enter + "]: ";
}

std::string McpDeskRun(const McpDeskKit& k, const std::string& table, std::string (*newSessionId)())
{
	if(k.gameId.empty() || k.folder.empty())
		return Say("No game is loaded yet: start or load one with MCP Server for LEKMOD on, click into it once, and run this again.");
	std::vector<McpDeskGroup> groups;
	std::string err;
	if(!McpDeskParseTable(table, groups, err)) return Say(err);
	if(k.referencePath.empty())
		return Say("This game's rules are not ready yet: the plugin writes them a few seconds after the game loads. Run this "
			"again in a moment. If it keeps saying so, Logs\\Bazaar.log says why.");
	std::vector<Agent> agents = Agents(k);
	std::string reply;
	std::vector<Agent> start;

	if(groups.empty())
	{
		for(size_t i = 0; i < agents.size(); ++i)
		{
			if(Running(k, agents[i].name))
				reply += Say(agents[i].info["title"] + " (" + agents[i].name + ") seems to be running: it was at the game in the last "
					"ten minutes. If its window is closed, run its start script.");
			else start.push_back(agents[i]);
		}
		if(start.empty() && !agents.empty()) return reply;
		if(agents.empty()) groups.push_back(McpDeskGroup());
		else reply += Say("Bringing back " + Plural((int)start.size(), "agent", "agents") + ".");
	}

	if(!groups.empty())
	{
		int opponents = 0, teammates = 0, humans = 0;
		for(size_t g = 0; g < groups.size(); ++g)
		{
			if(groups[g].role == "opponent") opponents += groups[g].count;
			else if(groups[g].role == "teammate") teammates += groups[g].count;
			else humans += groups[g].count;
		}
		if(opponents > k.freeOpponents || teammates > k.freeTeammates || humans > (k.screenFree ? 1 : 0))
			return Say("That does not fit this game. Free now: " + Free(k) + ".");
		int next = agents.empty() ? 1 : agents.back().number + 1;
		std::map<std::string, int> titles;
		for(size_t i = 0; i < agents.size(); ++i) ++titles[agents[i].info["role"]];
		for(size_t g = 0; g < groups.size(); ++g)
		{
			for(int n = 0; n < groups[g].count; ++n)
			{
				Agent a;
				a.number = next++;
				a.name = "agent-" + Str(a.number);
				a.info["title"] = Title(groups[g].role, ++titles[groups[g].role]);
				a.info["role"] = groups[g].role;
				a.info["model"] = groups[g].model;
				a.info["effort"] = groups[g].effort;
				a.info["context"] = Str(groups[g].context);
				a.info["session"] = newSessionId();
				if(groups[g].prompt) a.info["ask"] = "1";
				start.push_back(a);
			}
		}
		reply += Say("Starting " + Plural((int)start.size(), "agent", "agents") + " for this game.");
		reply += Say("If a window asks whether you trust its folder, answer Yes: it is the agent's own folder.");
	}

	std::string runs;
	for(size_t i = 0; i < start.size(); ++i)
	{
		const Agent& a = start[i];
		if(!Prepare(k, a)) return reply + Say("Could not write " + Join(k.folder, a.name) + ".");
		std::string folder = HostJoin(k, k.hostFolder, a.name);
		std::map<std::string, std::string> info(a.info);
		if(info["ask"] == "1") reply += "ask\t" + folder + "\t" + info["title"] + "\n";
		if(i > 0) runs += "pause\t3\n";
		runs += "run\t" + info["title"] + "\t" + HostJoin(k, folder, ScriptName(k)) + "\n";
	}
	return reply + runs;
}

std::string McpDeskScript(const McpDeskKit& k, const std::string& agentsUrl)
{
	std::string question = McpDeskQuestion(k);
	if(k.unixHost)
	{
		return "#!/bin/sh\n"
			"# MCP Server for LEKMOD: starts AI players for the game that is loaded, each in its own Terminal window.\n"
			"U='" + agentsUrl + "'\n"
			"command -v claude >/dev/null 2>&1 || { echo 'Install Claude Code first: https://claude.com/claude-code'; exit 1; }\n"
			"T=\"$*\"\n"
			"if [ -z \"$T\" ]; then printf '%s' '" + question + "'; IFS= read -r T < /dev/tty; fi\n"
			"R=$(curl -s --data-urlencode \"table=$T\" \"$U\") || { echo \"The game did not answer at $U. Is it running, with MCP Server for LEKMOD on?\"; exit 1; }\n"
			"TAB=$(printf '\\t')\n"
			"printf '%s\\n' \"$R\" | while IFS=\"$TAB\" read -r verb a b; do\n"
			"  case \"$verb\" in\n"
			"    say) printf '%s\\n' \"$a\" ;;\n"
			"    ask) printf 'Your prompt for %s (Enter for none): ' \"$b\"; IFS= read -r P < /dev/tty; [ -n \"$P\" ] && printf '%s\\n' \"$P\" > \"$a/prompt.md\" ;;\n"
			"    pause) sleep \"$a\" ;;\n"
			"    run) chmod +x \"$b\" 2>/dev/null\n"
			"      if command -v lekmod-agents-hook >/dev/null 2>&1 && lekmod-agents-hook open \"$a\" \"$b\"; then :; else open -a Terminal \"$b\"; fi ;;\n"
			"  esac\n"
			"done\n";
	}
	return "# MCP Server for LEKMOD: starts AI players for the game that is loaded, each in its own window.\n"
		"$u = '" + agentsUrl + "'\n"
		"if (-not (Get-Command claude -ErrorAction SilentlyContinue)) { Write-Host 'Install Claude Code first: https://claude.com/claude-code'; return }\n"
		"$t = Read-Host '" + question.substr(0, question.size() - 2) + "'\n"
		"try { $r = Invoke-RestMethod -Method Post -Uri $u -Body @{ table = $t } } catch { Write-Host \"The game did not answer at $u. Is it running, with MCP Server for LEKMOD on?\"; return }\n"
		"$hook = Get-Command lekmod-agents-hook -ErrorAction SilentlyContinue\n"
		"foreach ($line in ($r -split \"`n\")) {\n"
		"  $f = $line.TrimEnd(\"`r\") -split \"`t\"\n"
		"  switch ($f[0]) {\n"
		"    'say' { Write-Host $f[1] }\n"
		"    'ask' { $p = Read-Host \"Your prompt for $($f[2]) (Enter for none)\"; if ($p) { [IO.File]::WriteAllText((Join-Path $f[1] 'prompt.md'), $p, (New-Object Text.UTF8Encoding $false)) } }\n"
		"    'pause' { Start-Sleep -Seconds ([int]$f[1]) }\n"
		"    'run' {\n"
		"      $opened = $false\n"
		"      if ($hook) { & lekmod-agents-hook open $f[1] $f[2]; $opened = ($LASTEXITCODE -eq 0) }\n"
		"      if (-not $opened) { Start-Process -FilePath $f[2] -WorkingDirectory (Split-Path -Parent $f[2]) }\n"
		"    }\n"
		"  }\n"
		"}\n";
}
