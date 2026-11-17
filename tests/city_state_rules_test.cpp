// Native tests for the city-state rules (no game needed): tests/run_city_state_rules_test.sh
#include "McpCityStateRules.h"
#include <cstdio>
#include <string>
#include <vector>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

namespace
{
	const McpCsTier* TierAt(const std::vector<McpCsTier>& tiers, int influence)
	{
		const McpCsTier* t = McpCsTierFor(tiers, influence);
		CHECK(t != NULL);
		return t;
	}

	bool Contains(const std::vector<std::string>& v, const std::string& s)
	{
		for(size_t i = 0; i < v.size(); ++i) if(v[i] == s) return true;
		return false;
	}

	std::vector<int> History(const int* values, size_t n) { return std::vector<int>(values, values + n); }

	void TestTierLookup()
	{
		std::vector<McpCsTier> tiers = McpCsDefaultTiers();
		CHECK(McpCsTierFor(tiers, 59) == NULL);
		CHECK(TierAt(tiers, 60)->minInfluence == 60);
		CHECK(TierAt(tiers, 69)->minInfluence == 60);
		CHECK(TierAt(tiers, 70)->minInfluence == 70);
		CHECK(TierAt(tiers, 400)->minInfluence == 150);
	}

	void TestSendCap()
	{
		std::vector<McpCsTier> tiers = McpCsDefaultTiers();
		CHECK(McpCsSendCap(TierAt(tiers, 60), 8) == 0);
		CHECK(McpCsSendCap(TierAt(tiers, 70), 8) == 1);
		CHECK(McpCsSendCap(TierAt(tiers, 80), 8) == 1);
		CHECK(McpCsSendCap(TierAt(tiers, 90), 8) == 2);
		CHECK(McpCsSendCap(TierAt(tiers, 150), 8) == 7);
		CHECK(McpCsSendCap(TierAt(tiers, 150), 3) == 2);
		CHECK(McpCsSendCap(TierAt(tiers, 70), 1) == 0);
		CHECK(McpCsSendCap(TierAt(tiers, 70), 0) == 0);
		CHECK(McpCsSendCap(NULL, 10) == 0);
	}

	void TestUnlocks()
	{
		std::vector<McpCsTier> tiers = McpCsDefaultTiers();
		CHECK(!TierAt(tiers, 79)->revealSight);
		CHECK(TierAt(tiers, 80)->revealSight);
		CHECK(!TierAt(tiers, 99)->enterAllyLand);
		CHECK(TierAt(tiers, 100)->enterAllyLand);
	}

	void TestDisposition()
	{
		std::vector<McpCsDisposition> d = McpCsDefaultDispositions();
		CHECK(McpCsDispositionFor(d, 59) == NULL);
		const McpCsDisposition* trusted = McpCsDispositionFor(d, 95);
		CHECK(trusted != NULL && trusted->name == "Trusted ally");
		const McpCsDisposition* sworn = McpCsDispositionFor(d, 300);
		CHECK(sworn != NULL && sworn->name == "Sworn");
	}

	void TestModifiers()
	{
		McpCsSettings s = McpCsDefaultSettings();
		CHECK(s.influenceCost == 1 && s.contestedLead == 20 && s.secureLead == 60 && s.trendTurns == 5 && s.trendAmount == 15);
		std::vector<int> none;

		std::vector<std::string> contested = McpCsModifiers(s, 80, 70, none);
		CHECK(Contains(contested, McpCsModifierText(MCP_CS_CONTESTED)));
		CHECK(!Contains(contested, McpCsModifierText(MCP_CS_SECURE)));

		std::vector<std::string> secure = McpCsModifiers(s, 130, 50, none);
		CHECK(Contains(secure, McpCsModifierText(MCP_CS_SECURE)));
		CHECK(!Contains(secure, McpCsModifierText(MCP_CS_CONTESTED)));

		const int up[] = { 60, 62, 70, 74, 80 };
		CHECK(Contains(McpCsModifiers(s, 80, 0, History(up, 5)), McpCsModifierText(MCP_CS_RISING)));
		const int down[] = { 90, 85, 80, 76, 74 };
		CHECK(Contains(McpCsModifiers(s, 74, 0, History(down, 5)), McpCsModifierText(MCP_CS_FALLING)));
		const int flat[] = { 70, 70, 70 };
		std::vector<std::string> steady = McpCsModifiers(s, 70, 40, History(flat, 3));
		CHECK(!Contains(steady, McpCsModifierText(MCP_CS_RISING)));
		CHECK(!Contains(steady, McpCsModifierText(MCP_CS_FALLING)));
	}

	void TestRefusals()
	{
		CHECK(McpCsToolRefusal("declare_war", "", "").find("a city-state does not do that") == 0);
		CHECK(McpCsToolRefusal("move_unit", "", "").empty());
		CHECK(McpCsToolRefusal("unit_action", "fortify", "").empty());
		CHECK(!McpCsToolRefusal("unit_action", "disband", "").empty());
		CHECK(McpCsToolRefusal("set_production", "", "UNIT_COMPOSITE_BOWMAN").empty());
		CHECK(!McpCsToolRefusal("set_production", "", "PROCESS_WEALTH").empty());
		CHECK(!McpCsToolRefusal("set_production", "", "PROCESS_RESEARCH").empty());
		// The queue tool puts items in too, under the same rule.
		CHECK(McpCsToolRefusal("city_queue", "", "UNIT_COMPOSITE_BOWMAN").empty());
		CHECK(!McpCsToolRefusal("city_queue", "", "PROCESS_WEALTH").empty());
		CHECK(!McpCsToolRefusal("economic_overview", "", "").empty());
	}
	void TestNextSteps()
	{
		std::vector<McpCsTier> tiers = McpCsDefaultTiers();
		McpCsNextSteps low = McpCsNextStepsFor(tiers, 65, 8);
		CHECK(low.moreUnitsOut == 70 && low.sight == 80 && low.enterAllyLand == 100);
		McpCsNextSteps mid = McpCsNextStepsFor(tiers, 104, 8);
		CHECK(mid.moreUnitsOut == 110 && mid.sight == -1 && mid.enterAllyLand == -1);
		McpCsNextSteps few = McpCsNextStepsFor(tiers, 104, 2);
		CHECK(few.moreUnitsOut == -1);
		McpCsNextSteps top = McpCsNextStepsFor(tiers, 150, 8);
		CHECK(top.moreUnitsOut == -1 && top.sight == -1 && top.enterAllyLand == -1);
		McpCsNextSteps none = McpCsNextStepsFor(tiers, 40, 8);
		CHECK(none.moreUnitsOut == 70 && none.sight == 80 && none.enterAllyLand == 100);
	}

	void TestDismissal()
	{
		CHECK(McpCsIsDismissal("dismiss"));
		CHECK(McpCsIsDismissal("  Dismiss \n"));
		CHECK(McpCsIsDismissal("/dismiss"));
		CHECK(!McpCsIsDismissal("please dismiss your guard"));
		CHECK(!McpCsIsDismissal("dismissed"));
		CHECK(!McpCsIsDismissal(""));
	}
	void TestHumanMaySpeak()
	{
		CHECK(McpCsHumanMaySpeak(true, true, 0, 0));
		CHECK(!McpCsHumanMaySpeak(true, true, 3, 0));
		CHECK(!McpCsHumanMaySpeak(true, true, -1, 0));
		CHECK(!McpCsHumanMaySpeak(false, true, 0, 0));
		CHECK(!McpCsHumanMaySpeak(true, false, 0, 0));
		CHECK(!McpCsHumanMaySpeak(true, true, -1, -1));
	}
	void TestTalkLine()
	{
		int turn = 0;
		bool byCs = false;
		std::string text;
		CHECK(McpCsReadTalkLine("turn 81, Goths: Send your archers north: now.", "Valletta", turn, byCs, text));
		CHECK(turn == 81 && !byCs && text == "Send your archers north: now.");
		CHECK(McpCsReadTalkLine("turn 82, Panama City: They march at dawn.", "Panama City", turn, byCs, text));
		CHECK(turn == 82 && byCs && text == "They march at dawn.");
		CHECK(!McpCsReadTalkLine("deed turn 3: gold", "Valletta", turn, byCs, text));
		CHECK(!McpCsReadTalkLine("turn x, Goths: hi", "Valletta", turn, byCs, text));
		CHECK(!McpCsReadTalkLine("turn 5, Goths hi", "Valletta", turn, byCs, text));
	}
	void TestSightLedger()
	{
		McpCsSightLedger empty;
		CHECK(McpCsSightLedgerWrite(empty).empty());
		McpCsSightLedger read;
		CHECK(McpCsSightLedgerRead("", read) && read.team == -1 && read.unitPlots.empty());

		McpCsSightLedger lit;
		lit.team = 3;
		lit.unitPlots[8192].push_back(101);
		lit.unitPlots[8192].push_back(102);
		lit.unitPlots[16385].push_back(7);
		McpCsSightLedger back;
		CHECK(McpCsSightLedgerRead(McpCsSightLedgerWrite(lit), back));
		CHECK(back.team == 3 && back.unitPlots.size() == 2);
		CHECK(back.unitPlots[8192].size() == 2 && back.unitPlots[8192][1] == 102);
		CHECK(back.unitPlots[16385].size() == 1 && back.unitPlots[16385][0] == 7);

		McpCsSightLedger none;
		none.team = 3;
		CHECK(McpCsSightLedgerWrite(none).empty());

		McpCsSightLedger foreign;
		CHECK(!McpCsSightLedgerRead("someone else's data", foreign));
		CHECK(!McpCsSightLedgerRead("mcp-sight 1|x|5:1", foreign));
		CHECK(!McpCsSightLedgerRead("mcp-sight 1|3|5:1,b", foreign));
	}
}

static void TestGold()
{
	// a gift is what actually left the giver: a gift the city-state refused moves nothing
	CHECK(McpCsGoldMoved(1500, 500) == 1000);
	CHECK(McpCsGoldMoved(1500, 1500) == 0);
	CHECK(McpCsGoldMoved(500, 900) == 0);
	CHECK(McpCsGiftDeed(1000, 41) == "gave them 1000 gold (treasury 41 to 1041)");
	// tribute comes out of the treasury, down to zero at most; the taker's sum is the game's
	CHECK(McpCsTributePaid(300, 240) == 240);
	CHECK(McpCsTributePaid(100, 240) == 100);
	CHECK(McpCsTributePaid(-5, 240) == 0);
	CHECK(McpCsTributeDeed(240, 240, 300) == "took 240 gold from them by threat (treasury 300 to 60)");
	CHECK(McpCsTributeDeed(240, 100, 100) == "took 240 gold from them by threat (treasury 100 to 0)");
}

static void TestSameSide()
{
	// two city-states are one side while each has an ally and the allies are on one team
	CHECK(McpCsSameSide(true, true, 2, 2));
	CHECK(!McpCsSameSide(true, true, 2, 3));
	CHECK(!McpCsSameSide(true, true, -1, -1));
	CHECK(!McpCsSameSide(true, true, 2, -1));
	CHECK(!McpCsSameSide(true, false, 2, 2));
	CHECK(!McpCsSameSide(false, true, 2, 2));
}

int main()
{
	TestTierLookup();
	TestSendCap();
	TestUnlocks();
	TestDisposition();
	TestModifiers();
	TestRefusals();
	TestNextSteps();
	TestDismissal();
	TestHumanMaySpeak();
	TestTalkLine();
	TestSightLedger();
	TestGold();
	TestSameSide();
	if(s_failures) { std::printf("%d check(s) failed\n", s_failures); return 1; }
	std::printf("all city-state rule checks pass\n");
	return 0;
}
