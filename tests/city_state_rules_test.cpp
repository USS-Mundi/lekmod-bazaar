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
	TestSightLedger();
	if(s_failures) { std::printf("%d check(s) failed\n", s_failures); return 1; }
	std::printf("all city-state rule checks pass\n");
	return 0;
}
