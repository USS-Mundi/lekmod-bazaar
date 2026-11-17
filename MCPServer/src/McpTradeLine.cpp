#include "McpTradeLine.h"
#include <sstream>

namespace
{
	std::string Items(const std::vector<McpTradeItem>& a, const std::vector<McpTradeItem>& b)
	{
		std::string out;
		const std::vector<McpTradeItem>* lists[2] = { &a, &b };
		for(int l = 0; l < 2; ++l)
			for(size_t i = 0; i < lists[l]->size(); ++i)
			{
				std::ostringstream n;
				n << ' ' << (*lists[l])[i].count;
				out += (out.empty() ? "" : ", ") + (*lists[l])[i].name + n.str();
			}
		return out;
	}
}

std::string McpTradeLine(const McpTradeView& v)
{
	std::string you = Items(v.youGiveLux, v.youGiveStrat), they = Items(v.theyGiveLux, v.theyGiveStrat);
	std::string out;
	if(you.empty() && they.empty()) out = "nothing either could give";
	else out = "you could give " + (you.empty() ? std::string("nothing they lack") : you) + "; they could give " +
	           (they.empty() ? std::string("nothing you lack") : they);
	if(!v.bothHave.empty())
	{
		std::string both;
		for(size_t i = 0; i < v.bothHave.size(); ++i) both += (both.empty() ? "" : ", ") + v.bothHave[i];
		out += "; both have " + both + " (no trade)";
	}
	return out;
}

std::string McpGoldText(int gold, int perTurn)
{
	std::ostringstream g;
	g << gold << ", " << (perTurn >= 0 ? "+" : "") << perTurn << "/t";
	return g.str();
}
