// Native tests for the agent launcher's thinking (McpAgentDesk), through its interface. The start script it renders
// for a Mac is run for real under /bin/sh with a stand-in claude that records what it was given.
#include "McpAgentDesk.h"
#include "McpFiles.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

static int g_failed = 0;
static void Check(bool ok, const char* what)
{
	if(!ok) { std::printf("FAIL: %s\n", what); ++g_failed; }
}

static std::string g_root;
static int g_games = 0;
static int g_sessions = 0;

static std::string NewSession()
{
	char id[64];
	std::snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012d", ++g_sessions);
	return id;
}

static bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static std::string Read(const std::string& path)
{
	std::string s;
	McpReadFile(path, s);
	return s;
}

static int Count(const std::string& s, const std::string& part)
{
	int n = 0;
	for(size_t at = s.find(part); at != std::string::npos; at = s.find(part, at + part.size())) ++n;
	return n;
}

// A game on a Mac: the plugin's folder and the host's are the same here, with a space and an apostrophe in the path.
static McpDeskKit Kit()
{
	char n[32];
	std::snprintf(n, sizeof(n), "civ5-game-%d", ++g_games);
	McpDeskKit k;
	k.gameId = n;
	k.folder = g_root + "/Sid Meier's Civilization 5/MCP/" + n + "/agents";
	k.hostFolder = k.folder;
	k.unixHost = true;
	k.mcpUrl = "http://127.0.0.1:7676/mcp";
	k.rulebook = "# Rulebook\n\nPlay to win.\n";
	k.referencePath = g_root + "/reference-" + n + ".md";
	McpWriteFile(k.referencePath, "# This game's rules\n", false);
	k.freeOpponents = 4;
	k.freeTeammates = 1;
	k.screenFree = true;
	return k;
}

static void TestTable()
{
	std::vector<McpDeskGroup> g;
	std::string err;
	Check(McpDeskParseTable("2 opus high, 1 sonnet teammate", g, err) && g.size() == 2, "a table is groups split by commas");
	Check(g.size() == 2 && g[0].count == 2 && g[0].model == "opus" && g[0].effort == "high" && g[0].role == "opponent",
		"words in any order: a count, a model, an effort; opponent by default");
	Check(g.size() == 2 && g[1].role == "teammate" && g[1].effort == "medium" && g[1].context == 300000, "medium and 300k by default");
	Check(McpDeskParseTable("low claude-opus-5-5 human prompt 1m", g, err) && g.size() == 1 && g[0].effort == "medium"
		&& g[0].model == "claude-opus-5-5" && g[0].role == "human" && g[0].prompt && g[0].context == 1000000,
		"low runs as medium; a full model id, the human's own civilization, a prompt and a context");
	Check(McpDeskParseTable("XHIGH Sonnet 400K", g, err) && g.size() == 1 && g[0].effort == "xhigh" && g[0].context == 400000,
		"case does not matter for efforts and contexts");
	Check(McpDeskParseTable("", g, err) && g.empty(), "an empty table is no groups");
	Check(!McpDeskParseTable("opus high max", g, err) && Has(err, "max"), "two efforts in one group are refused, naming the word");
	Check(!McpDeskParseTable("opus sonnet", g, err) && Has(err, "sonnet"), "two models in one group are refused");
	Check(!McpDeskParseTable("opus;rm -rf", g, err), "a model name with anything but letters, digits, - . [ ] is refused");
	Check(!McpDeskParseTable("100k opus", g, err) && Has(err, "200k"), "a context below 200k is refused");
	Check(!McpDeskParseTable("13 opus", g, err), "more than 12 agents at once is refused");
}

static void TestNewAgents()
{
	McpDeskKit k = Kit();
	Check(Has(McpDeskQuestion(k), "[Enter: 1 sonnet]"), "with no agents, Enter starts one Sonnet");
	std::string reply = McpDeskRun(k, "2 opus high, 1 sonnet teammate prompt", &NewSession);
	Check(Count(reply, "run\t") == 3, "three agents, three windows");
	Check(Count(reply, "pause\t3\n") == 2, "a pause between windows, so seats are claimed one at a time");
	Check(Count(reply, "ask\t") == 1 && reply.find("ask\t") < reply.find("run\t"), "the prompt is asked before any agent starts");
	Check(Has(reply, "run\tOpponent 1\t" + k.hostFolder + "/agent-1/start.command\n"), "an opponent's window is titled without its civilization");
	Check(Has(reply, "run\tTeammate 1\t" + k.hostFolder + "/agent-3/start.command\n"), "a teammate's too");
	Check(Has(reply, "ask\t" + k.hostFolder + "/agent-3\tTeammate 1\n"), "ask names the folder the prompt goes in");

	std::string a1 = k.folder + "/agent-1";
	std::string info = Read(a1 + "/agent.txt");
	Check(Has(info, "role=opponent\n") && Has(info, "model=opus\n") && Has(info, "effort=high\n") && Has(info, "context=300000\n")
		&& Has(info, "session=00000000-0000-4000-8000-"), "agent.txt holds what it was started with");
	std::string sys = Read(a1 + "/system-prompt.md");
	Check(Has(sys, "Play to win.") && Has(sys, "# This game's rules"), "the system prompt holds the rulebook and this game's rules");
	Check(Has(sys, "take_seat with civ=opponent") && Has(sys, "-agent-1"), "and its role and its own set_identity version");
	std::string settings = Read(a1 + "/.claude/settings.json");
	Check(Has(settings, "\"defaultMode\":\"dontAsk\"") && Has(settings, "\"mcp__civ5\"") && Has(settings, "\"mcp__civ5__lookup\""),
		"settings never ask: the game's tools allowed, lookup denied, anything else refused");
	Check(Has(settings, "NOTES.md"), "an agent may keep its notes");
	std::string mcp = Read(a1 + "/mcp.json");
	Check(Has(mcp, "\"url\":\"http://127.0.0.1:7676/mcp\"") && Has(mcp, "\"X-Lekmod-Agent\":\"agent-1\""),
		"the game's server, with a header that tells the plugin which agent this is");
	Check(McpFileExists(a1 + "/NOTES.md"), "an empty NOTES.md waits for it");
	Check(McpFileExists(a1 + "/start.command") && !McpFileExists(a1 + "/start.cmd"), "a Mac gets a .command");

	k.freeOpponents = 2;
	k.freeTeammates = 0;
	std::string none = McpDeskRun(k, "3 opus", &NewSession);
	Check(!Has(none, "run\t") && Has(none, "2 opponents"), "asking for more seats than are free starts nothing and says what is free");
	Check(!McpFileExists(k.folder + "/agent-4/agent.txt"), "and writes nothing");
	Check(!Has(McpDeskRun(k, "1 teammate", &NewSession), "run\t"), "no free teammate, no teammate");
	k.screenFree = false;
	Check(!Has(McpDeskRun(k, "1 human", &NewSession), "run\t"), "the human's civilization only while no model plays it");
	Check(Has(McpDeskRun(k, "opus sonnet", &NewSession), "say\t"), "a bad table is answered in a say line");
}

static void TestResume()
{
	McpDeskKit k = Kit();
	McpDeskRun(k, "3 sonnet", &NewSession);
	std::string before = Read(k.folder + "/agent-2/agent.txt");
	k.running.push_back("agent-3");
	k.rulebook = "# Rulebook\n\nA newer rulebook.\n";
	Check(Has(McpDeskQuestion(k), "[Enter: bring back your 2 agents]"), "Enter brings back the idle agents");
	std::string reply = McpDeskRun(k, "", &NewSession);
	Check(Count(reply, "run\t") == 2 && Has(reply, "agent-1/start.command") && Has(reply, "agent-2/start.command"),
		"an empty table brings back the idle agents");
	Check(!Has(reply, "agent-3/start.command") && Has(reply, "running"), "and leaves a running one alone, saying so");
	Check(Read(k.folder + "/agent-2/agent.txt") == before, "a resumed agent keeps its model and session");
	Check(Has(Read(k.folder + "/agent-2/system-prompt.md"), "A newer rulebook."), "and gets the rulebook as it is now");
	std::string more = McpDeskRun(k, "1 opus", &NewSession);
	Check(Has(more, "agent-4/start.command") && !Has(more, "agent-1/start.command"), "a table adds agents, numbered after the last");
}

static void TestStartScriptRuns()
{
	McpDeskKit k = Kit();
	McpDeskRun(k, "opus high 400k prompt", &NewSession);
	std::string dir = k.folder + "/agent-1";
	McpWriteFile(dir + "/prompt.md", "Be bold.\n", false);
	std::string bin = g_root + "/bin";
	McpMakeDirs(bin);
	std::string log = g_root + "/claude-calls.txt";
	McpWriteFile(bin + "/claude",
		"#!/bin/sh\n{ printf 'ARGS'; for a in \"$@\"; do printf ' [%s]' \"$a\"; done; printf '\\n';"
		" printf 'ENV %s %s %s %s\\n' \"$CLAUDE_CODE_AUTO_COMPACT_WINDOW\" \"$CLAUDE_CODE_EFFORT_LEVEL\" \"$CLAUDE_CODE_DISABLE_CLAUDE_MDS\" \"$MAX_MCP_OUTPUT_TOKENS\";"
		" printf 'PWD %s\\n' \"$PWD\"; cat .system-prompt.full.md; } >> '" + log + "'\n", false);
	std::string chmod = "chmod +x '" + bin + "/claude' && chmod +x \"" + dir + "/start.command\"";
	Check(std::system(chmod.c_str()) == 0, "the stand-in claude and the script are made runnable");
	std::string run = "PATH='" + bin + "':/usr/bin:/bin \"" + dir + "/start.command\" > /dev/null 2>&1";
	Check(std::system(run.c_str()) == 0, "the start script runs");
	std::string first = Read(log);
	Check(Has(first, "[--model] [opus]") && Has(first, "[--session-id] [00000000-0000-4000-8000-"), "the first start names its session");
	Check(Has(first, "[--setting-sources] [project,local]") && Has(first, "[--strict-mcp-config] [--mcp-config] [mcp.json]"),
		"no user settings and only the game's server");
	Check(Has(first, "[--append-system-prompt-file] [.system-prompt.full.md]"), "the system prompt goes in from the file");
	Check(Has(first, "[/loop read your instructions, then play]"), "and it starts playing at once");
	Check(Has(first, "ENV 400000 high 1 100000"), "the compaction window, the effort, no CLAUDE.md, long tool results");
	Check(Has(first, "PWD " + dir), "it runs in its own folder, whatever the path holds");
	Check(Has(first, "Play to win.") && Has(first, "Be bold."), "the player's prompt joins the system prompt");
	Check(!McpFileExists(dir + "/prompt.md"), "the prompt is for that start only");
	std::remove(log.c_str());
	Check(std::system(run.c_str()) == 0, "it runs again");
	std::string second = Read(log);
	Check(Has(second, "[--resume] [00000000-0000-4000-8000-") && !Has(second, "--session-id"), "a restart resumes the same session");
	Check(Has(second, "[/loop you were restarted: take your seat back, then play]"), "and is told it was restarted");
	Check(!Has(second, "Be bold."), "without the earlier prompt");
}

static void TestWindows()
{
	McpDeskKit k = Kit();
	k.unixHost = false;
	k.hostFolder = "C:\\Users\\Ana\\Documents\\My Games\\Sid Meier's Civilization 5\\MCP\\" + k.gameId + "\\agents";
	std::string reply = McpDeskRun(k, "1 opus", &NewSession);
	Check(Has(reply, "run\tOpponent 1\t" + k.hostFolder + "\\agent-1\\start.cmd\n"), "Windows paths in the reply");
	std::string cmd = Read(k.folder + "/agent-1/start.cmd");
	Check(Has(cmd, "cd /d \"%~dp0\"") && Has(cmd, "--session-id") && Has(cmd, "--resume") && Has(cmd, "\r\n"),
		"a cmd script that runs from its own folder, with Windows line ends");
	Check(Has(cmd, "set \"CLAUDE_CODE_AUTO_COMPACT_WINDOW=300000\""), "and the same settings");
	std::string script = McpDeskScript(k, "http://127.0.0.1:7676/agents");
	Check(Has(script, "Invoke-RestMethod") && Has(script, "Start-Process"), "Windows gets a PowerShell launcher");
	k.unixHost = true;
	script = McpDeskScript(k, "http://127.0.0.1:7676/agents");
	Check(Has(script, "#!/bin/sh") && Has(script, "open -a Terminal") && Has(script, "/dev/tty"), "a Mac gets an sh launcher");
}

// The sh launcher against stand-ins for curl and open: it reads the reply line by line and opens each window.
static void TestLauncherRuns()
{
	McpDeskKit k = Kit();
	std::string bin = g_root + "/bin2";
	McpMakeDirs(bin);
	std::string opened = g_root + "/opened.txt";
	std::string reply = "say\tStarting 2 agents.\nrun\tOpponent 1\t" + g_root + "/a b's/start.command\npause\t0\nrun\tOpponent 2\t" + g_root + "/c/start.command\n";
	McpWriteFile(g_root + "/reply.txt", reply, false);
	McpWriteFile(bin + "/curl", "#!/bin/sh\ncat '" + g_root + "/reply.txt'\n", false);
	McpWriteFile(bin + "/open", "#!/bin/sh\nprintf '%s|%s|%s\\n' \"$1\" \"$2\" \"$3\" >> '" + opened + "'\n", false);
	McpWriteFile(bin + "/claude", "#!/bin/sh\n", false);
	McpWriteFile(g_root + "/launcher.sh", McpDeskScript(k, "http://127.0.0.1:7676/agents"), false);
	std::string cmd = "chmod +x '" + bin + "/curl' '" + bin + "/open' '" + bin + "/claude' && PATH='" + bin +
		"':/usr/bin:/bin sh '" + g_root + "/launcher.sh' 1 opus < /dev/null > '" + g_root + "/launcher-out.txt' 2>&1";
	Check(std::system(cmd.c_str()) == 0, "the launcher runs");
	std::string got = Read(opened);
	Check(Has(got, "-a|Terminal|" + g_root + "/a b's/start.command\n") && Has(got, "-a|Terminal|" + g_root + "/c/start.command\n"),
		"each run line opens a Terminal window on its script, apostrophes and spaces intact");
	Check(Has(Read(g_root + "/launcher-out.txt"), "Starting 2 agents."), "say lines are printed");
}

int main()
{
	char tmpl[] = "/tmp/agent_desk_test.XXXXXX";
	char* dir = mkdtemp(tmpl);
	if(!dir) { std::printf("no temp folder\n"); return 1; }
	g_root = dir;
	TestTable();
	TestNewAgents();
	TestResume();
	TestStartScriptRuns();
	TestWindows();
	TestLauncherRuns();
	std::string rm = "rm -rf '" + g_root + "'";
	if(std::system(rm.c_str()) != 0) std::printf("could not remove %s\n", g_root.c_str());
	if(g_failed) { std::printf("%d failed\n", g_failed); return 1; }
	std::printf("agent desk: all passed\n");
	return 0;
}
