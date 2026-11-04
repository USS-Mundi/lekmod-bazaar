// Native tests for the clocks: turns, need and slack, queues and targets (no game needed): tests/run_clocks_test.sh
#include "McpClocks.h"
#include <cstdio>
#include <string>
#include <vector>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)
#define CHECK_EQ(a, b) do { std::string _a = (a), _b = (b); if(_a != _b) { std::printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); ++s_failures; } } while(0)

namespace
{
	McpClock Clock(const char* name, long long have, long long need, long long first, long long rate, int game = -1)
	{
		McpClock c;
		c.name = name;
		c.have100 = have;
		c.need100 = need;
		c.first100 = first;
		c.rate100 = rate;
		c.gameTurns = game;
		return c;
	}

	void TestTurnsFollowTheGamesCount()
	{
		// The game: left = needed - progress - first; turns = ceil(left / rate) + 1, at least 1.
		CHECK(McpClockTurns(Clock("Settler", 1500, 4600, 800, 800)) == 4);
		CHECK(McpClockTurns(Clock("Settler", 1500, 4600, 1200, 800)) == 4);  // overflow on the first turn
		CHECK(McpClockTurns(Clock("Settler", 1500, 4600, 2300, 800)) == 2);
		CHECK(McpClockTurns(Clock("Shrine", 1400, 1500, 600, 600)) == 1);
		CHECK(McpClockTurns(Clock("growth", 1200, 2600, 0, 0)) == -1);     // never at this rate
		CHECK(McpClockTurns(Clock("growth", 2600, 2600, 100, 100)) == 1);
	}

	void TestNeedIsTheLeastExtraPerTurnForEachHorizon()
	{
		McpClock c = Clock("Settler", 1500, 4600, 800, 800);   // 31 left at 8: 4 turns
		long long n3 = McpClockNeedFor(c, 3);
		McpClock faster = Clock("Settler", 1500, 4600, 800 + n3, 800 + n3);
		CHECK(McpClockTurns(faster) <= 3);
		McpClock short1 = Clock("Settler", 1500, 4600, 800 + n3 - 1, 800 + n3 - 1);
		CHECK(McpClockTurns(short1) > 3);
		CHECK(McpClockNeedFor(c, 4) == 0);
		long long n1 = McpClockNeedFor(c, 1);
		CHECK(McpClockTurns(Clock("Settler", 1500, 4600, 800 + n1, 800 + n1)) == 1);
	}

	void TestSlackIsTheMostThatCanGoWithoutSlipping()
	{
		McpClock c = Clock("Settler", 1500, 4600, 800, 800);
		long long s = McpClockSlack(c);
		CHECK(McpClockTurns(Clock("Settler", 1500, 4600, 800 - s, 800 - s)) == 4);
		CHECK(McpClockTurns(Clock("Settler", 1500, 4600, 800 - s - 1, 800 - s - 1)) > 4);
	}

	void TestAmountsRoundTheSafeWay()
	{
		CHECK_EQ(McpAmount(300, true), "3");
		CHECK_EQ(McpAmount(234, true), "2.4");    // a need rounds up
		CHECK_EQ(McpAmount(239, false), "2.3");   // a slack rounds down
		CHECK_EQ(McpAmount(33, false), "0.3");
		CHECK_EQ(McpAmount(0, true), "0");
	}

	void TestLines()
	{
		CHECK_EQ(McpClockLine(Clock("Settler", 1500, 4600, 800, 800, 4)), "Settler 15/46 at 8/t: 4t · need +2.4 for 3t · slack 0.2");
		CHECK_EQ(McpClockLine(Clock("Shrine", 1400, 1500, 600, 600, 1)), "Shrine 14/15 at 6/t: 1t · slack 5");
		CHECK_EQ(McpClockLine(Clock("growth", 1200, 2600, 0, 0)), "growth 12/26 at 0/t: never at this rate · at +1/t: 14t");
		// The game counts differently: its count is shown and need and slack are withheld.
		CHECK_EQ(McpClockLine(Clock("Settler", 1500, 4600, 800, 800, 5)), "Settler 15/46 at 8/t: 5t by the game's count · need and slack unavailable");
	}

	void TestLandingTurnAndEdge()
	{
		McpClock c = Clock("Settler", 1500, 4600, 800, 800);    // 4 turns
		CHECK_EQ(McpClockLands(c, 45), "T49");
		CHECK_EQ(McpClockBody(c), "15/46 at 8/t: 4t · need +2.4 for 3t · slack 0.2");
		CHECK(!McpClockAtEdge(c));
		McpClock tight = Clock("Settler", 1500, 4600, 800, 800);
		tight.have100 = 2200;                                      // 24 left at 8: 3 turns, slack 0
		CHECK(McpClockAtEdge(tight));
		CHECK_EQ(McpClockLands(Clock("growth", 1200, 2600, 0, 0), 45), "never");
		// The game counting differently: the landing turn follows the game's count.
		CHECK_EQ(McpClockLands(Clock("Settler", 1500, 4600, 800, 800, 5), 45), "T50");
		CHECK(!McpClockAtEdge(Clock("Settler", 1500, 4600, 800, 800, 5)));
	}

	void TestQueueAddsEachItemsOwnTurns()
	{
		std::vector<std::pair<std::string, int> > q;
		q.push_back(std::make_pair(std::string("Settler"), 4));
		q.push_back(std::make_pair(std::string("Library"), 11));
		q.push_back(std::make_pair(std::string("Warrior"), 3));
		CHECK_EQ(McpQueueLine(q), "Settler 4t → Library 15t → Warrior 18t");
	}

	void TestTargets()
	{
		McpClock c = Clock("Iron Working", 2700, 12900, 1000, 1000);   // 102 left at 10: 11 turns
		CHECK_EQ(McpTargetLine("Iron Working by T31", c, 31, 23), "Iron Working by T31: 102 left at 10/t lands T34 · need +2.8/t");
		CHECK_EQ(McpTargetLine("Iron Working by T40", c, 40, 23), "Iron Working by T40: on track, lands T34 · slack 4/t");
		CHECK_EQ(McpTargetLine("Iron Working by T20", c, 20, 23), "Iron Working by T20: that turn has passed");
	}
}

namespace
{
	void TestADrainCountsTheTurnItGoesBelowZero()
	{
		CHECK(McpDrainTurns(1300, -400) == 4);   // 13, 9, 5, 1, then -3
		CHECK(McpDrainTurns(1200, -400) == 4);   // 12, 8, 4, 0, then -4
		CHECK(McpDrainTurns(0, -400) == 1);
		CHECK(McpDrainTurns(-100, -400) == 0);
		CHECK(McpDrainTurns(1300, 0) == -1);
		CHECK(McpDrainTurns(1300, 200) == -1);
		CHECK_EQ(McpDrainBody(1300, -400), "13 at -4/t: below zero in 4t");
		CHECK_EQ(McpDrainBody(-100, -400), "-1 at -4/t: below zero now");
		CHECK_EQ(McpDrainLands(1300, -400, 57), "T61");
	}
}

int main()
{
	TestTurnsFollowTheGamesCount();
	TestNeedIsTheLeastExtraPerTurnForEachHorizon();
	TestSlackIsTheMostThatCanGoWithoutSlipping();
	TestAmountsRoundTheSafeWay();
	TestLines();
	TestLandingTurnAndEdge();
	TestQueueAddsEachItemsOwnTurns();
	TestTargets();
	TestADrainCountsTheTurnItGoesBelowZero();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all clock tests passed\n");
	return 0;
}
