// Native tests for the turn view's trade line: what each side could give the other.
#include "McpTradeLine.h"
#include <cstdio>
#include <string>

static int g_failed = 0;
static void Check(bool ok, const char* what, const std::string& got)
{
	if(!ok) { std::printf("FAIL: %s\n  got: %s\n", what, got.c_str()); ++g_failed; }
}
static McpTradeItem I(const char* name, int n) { McpTradeItem i; i.name = name; i.count = n; return i; }

int main()
{
	McpTradeView v;
	v.youGiveLux.push_back(I("Furs", 2)); v.youGiveLux.push_back(I("Ivory", 1));
	v.theyGiveLux.push_back(I("Silk", 1));
	v.bothHave.push_back("Cocoa"); v.bothHave.push_back("Pearls");
	v.youGiveStrat.push_back(I("Iron", 3));
	v.theyGiveStrat.push_back(I("Horses", 2));
	std::string s = McpTradeLine(v);
	Check(s == "you could give Furs 2, Ivory 1, Iron 3; they could give Silk 1, Horses 2; both have Cocoa, Pearls (no trade)", "the whole line", s);
	Check(McpGoldText(120, 14) == "120, +14/t" && McpGoldText(35, -3) == "35, -3/t" && McpGoldText(0, 0) == "0, +0/t", "their gold", McpGoldText(35, -3));

	McpTradeView none;
	s = McpTradeLine(none);
	Check(s == "nothing either could give", "nothing at all", s);

	McpTradeView blocked;
	blocked.bothHave.push_back("Wine");
	s = McpTradeLine(blocked);
	Check(s == "nothing either could give; both have Wine (no trade)", "only what both have", s);

	McpTradeView oneSide;
	oneSide.theyGiveLux.push_back(I("Gems", 1));
	s = McpTradeLine(oneSide);
	Check(s == "you could give nothing they lack; they could give Gems 1", "one side empty", s);

	if(g_failed) { std::printf("%d failed\n", g_failed); return 1; }
	std::printf("trade line: all passed\n");
	return 0;
}
