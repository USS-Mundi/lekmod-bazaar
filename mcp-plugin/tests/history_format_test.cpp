// Native tests for the history record's pure helpers: turn window, per-turn series, names, who sees what.
#include "McpHistoryFormat.h"
#include <cstdio>
#include <string>

static int g_failed = 0;
static void Check(bool ok, const char* what, const std::string& got)
{
	if(!ok) { std::printf("FAIL: %s\n  got: %s\n", what, got.c_str()); ++g_failed; }
}

static void TestAReplaySetIsNamedWithoutItsPrefixInLowerCase()
{
	Check(McpReplaySetKey("REPLAYDATASET_TOTALGOLD") == "totalgold", "prefix dropped, lower case", McpReplaySetKey("REPLAYDATASET_TOTALGOLD"));
	Check(McpReplaySetKey("REPLAYDATASET_SCORE") == "score", "score", McpReplaySetKey("REPLAYDATASET_SCORE"));
	Check(McpReplaySetKey("CUSTOM_SET") == "custom_set", "a name without the prefix is only lowered", McpReplaySetKey("CUSTOM_SET"));
}

static void TestTheWindowDefaultsToTheWholeGameAndRefusesABackwardRange()
{
	int lo = -9, hi = -9;
	std::string err;
	Check(McpHistoryWindow(-1, -1, 120, lo, hi, err) && lo == 0 && hi == 120, "no range: turn 0 to now", err);
	Check(McpHistoryWindow(50, -1, 120, lo, hi, err) && lo == 50 && hi == 120, "from only: to now", err);
	Check(McpHistoryWindow(10, 500, 120, lo, hi, err) && lo == 10 && hi == 120, "through past now: stops at now", err);
	err.clear();
	Check(!McpHistoryWindow(90, 80, 120, lo, hi, err) && err.find("from_turn") != std::string::npos, "from after through: refused", err);
	err.clear();
	Check(!McpHistoryWindow(130, -1, 120, lo, hi, err) && err.find("130") != std::string::npos, "from after now: refused", err);
}

static void TestASeriesHasOneValuePerTurnAndNullWhereTheGameRecordedNone()
{
	std::map<unsigned int, int> byTurn;
	byTurn[2] = 5; byTurn[3] = 7; byTurn[5] = 9;
	std::string s = McpHistorySeries(byTurn, 2, 6).serialize();
	Check(s == "[5,7,null,9,null]", "values in turn order, gaps null", s);
	Check(McpHistorySeries(byTurn, 0, 1).serialize() == "[null,null]", "turns before the record began", McpHistorySeries(byTurn, 0, 1).serialize());
}

static void TestTheOnScreenSessionSeesEveryoneAndASeatOnlyItself()
{
	Check(McpHistoryShows(true, false) && McpHistoryShows(true, true), "on screen: every player", "");
	Check(McpHistoryShows(false, true), "a seat: its own civilization", "");
	Check(!McpHistoryShows(false, false), "a seat: no other civilization", "");
}

static void TestTheFileIsNamedForTheGameWithOnlySafeCharacters()
{
	Check(McpHistoryFileName("civ5-game-287d7d27-d1bf") == "MCPServer-history-civ5-game-287d7d27-d1bf.json", "game id kept", McpHistoryFileName("civ5-game-287d7d27-d1bf"));
	Check(McpHistoryFileName("a b/..\\c") == "MCPServer-history-abc.json", "anything else dropped", McpHistoryFileName("a b/..\\c"));
	Check(McpHistoryFileName("") == "MCPServer-history-unknown.json", "no id", McpHistoryFileName(""));
}

int main()
{
	TestAReplaySetIsNamedWithoutItsPrefixInLowerCase();
	TestTheWindowDefaultsToTheWholeGameAndRefusesABackwardRange();
	TestASeriesHasOneValuePerTurnAndNullWhereTheGameRecordedNone();
	TestTheOnScreenSessionSeesEveryoneAndASeatOnlyItself();
	TestTheFileIsNamedForTheGameWithOnlySafeCharacters();
	if(g_failed) { std::printf("%d failed\n", g_failed); return 1; }
	std::printf("history format: all passed\n");
	return 0;
}
