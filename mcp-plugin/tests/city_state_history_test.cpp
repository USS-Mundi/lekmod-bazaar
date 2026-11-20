// Native tests for a city-state's history file (no game needed): tests/run_city_state_history_test.sh
#include "McpCityStateHistory.h"
#include <cstdio>
#include <string>
#include <vector>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

namespace
{
	McpCsHistoryEntry Entry(const char* kind, int civ, const char* civName, const std::string& text)
	{
		McpCsHistoryEntry e;
		e.kind = kind;
		e.civ = civ;
		e.civName = civName;
		e.text = text;
		return e;
	}

	void TestFileName()
	{
		CHECK(McpCsHistoryFileName("Geneva") == "Geneva.md");
		CHECK(McpCsHistoryFileName("Cape Town") == "Cape Town.md");
		CHECK(McpCsHistoryFileName("Ur-Nammu's \"City\": a/b\\c*?<>|") == "Ur-Nammu's -City-- a-b-c-----.md");
		CHECK(McpCsHistoryFileName("") == "city-state.md");
	}

	void TestLineRoundTrip()
	{
		std::string line = McpCsHistoryLine(Entry("deed", 3, "Rome", "turn 12: cleared a barbarian camp"));
		CHECK(line == "deed | 3 Rome | turn 12: cleared a barbarian camp\n");
		std::vector<McpCsHistoryEntry> back = McpCsHistoryParse(line);
		CHECK(back.size() == 1);
		if(back.size() == 1)
		{
			CHECK(back[0].kind == "deed");
			CHECK(back[0].civ == 3);
			CHECK(back[0].civName == "Rome");
			CHECK(back[0].text == "turn 12: cleared a barbarian camp");
		}
	}

	void TestTextKeepsItsSeparatorsAndLosesItsNewlines()
	{
		std::string line = McpCsHistoryLine(Entry("said", 0, "Scotland", "turn 4, Scotland: hold the pass | then\nwait\r\nfor me"));
		CHECK(line == "said | 0 Scotland | turn 4, Scotland: hold the pass | then wait for me\n");
		std::vector<McpCsHistoryEntry> back = McpCsHistoryParse(line);
		CHECK(back.size() == 1 && back[0].text == "turn 4, Scotland: hold the pass | then wait for me");
	}

	void TestWholeFileOldestFirst()
	{
		std::string file = McpCsHistoryHeader("Geneva", "civ5-game-abc")
			+ McpCsHistoryLine(Entry("deed", 1, "Rome", "turn 3: finished a quest"))
			+ McpCsHistoryLine(Entry("said", 2, "Georgia", "turn 5, Georgia: thank you"))
			+ "\n"
			+ "a line nobody wrote | here\n"
			+ McpCsHistoryLine(Entry("deed", 2, "Georgia", "turn 6: gave 100 gold"));
		std::vector<McpCsHistoryEntry> back = McpCsHistoryParse(file);
		CHECK(back.size() == 3);
		if(back.size() == 3)
		{
			CHECK(back[0].civName == "Rome" && back[0].kind == "deed");
			CHECK(back[1].civ == 2 && back[1].kind == "said");
			CHECK(back[2].text == "turn 6: gave 100 gold");
		}
	}

	void TestHeaderNamesTheCityStateAndGameAndHoldsNoEntries()
	{
		std::string header = McpCsHistoryHeader("Geneva", "civ5-game-abc");
		CHECK(header.find("Geneva") != std::string::npos);
		CHECK(header.find("civ5-game-abc") != std::string::npos);
		CHECK(McpCsHistoryParse(header).empty());
	}

	void TestWindowsLineEndingsRead()
	{
		std::vector<McpCsHistoryEntry> back = McpCsHistoryParse("deed | 1 Rome | turn 3: x\r\nsaid | 2 Georgia | turn 4, Georgia: y\r\n");
		CHECK(back.size() == 2);
		if(back.size() == 2) CHECK(back[0].text == "turn 3: x" && back[1].text == "turn 4, Georgia: y");
	}
}

int main()
{
	TestFileName();
	TestLineRoundTrip();
	TestTextKeepsItsSeparatorsAndLosesItsNewlines();
	TestWholeFileOldestFirst();
	TestHeaderNamesTheCityStateAndGameAndHoldsNoEntries();
	TestWindowsLineEndingsRead();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all city-state history tests passed\n");
	return 0;
}
