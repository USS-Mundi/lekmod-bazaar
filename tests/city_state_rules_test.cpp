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
}

int main()
{
	TestTierLookup();
	TestSendCap();
	TestUnlocks();
	TestDisposition();
	TestModifiers();
	if(s_failures) { std::printf("%d check(s) failed\n", s_failures); return 1; }
	std::printf("all city-state rule checks pass\n");
	return 0;
}
