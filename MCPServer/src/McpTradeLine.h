#pragma once
// The turn view's trade line for one player: what each side could put in a deal with the other, as the game's
// trade screen allows it, and the luxuries both already have, which neither can give the other. No game headers,
// so it is tested natively (tests/run_trade_line_test.sh).
#include <string>
#include <vector>

struct McpTradeItem { std::string name; int count; };

struct McpTradeView
{
	std::vector<McpTradeItem> youGiveLux, youGiveStrat;     // yours they lack, with how many you have
	std::vector<McpTradeItem> theyGiveLux, theyGiveStrat;   // theirs you lack, with how many they have
	std::vector<std::string> bothHave;                      // luxuries both hold: no trade either way
};

std::string McpTradeLine(const McpTradeView& v);
// Their gold for the same row, a field of its own: it changes every turn, the trade line seldom.
std::string McpGoldText(int gold, int perTurn);
