#include "McpViewGate.h"
#include <map>

namespace
{
	std::map<int, int> s_viewedTurn;

	bool RunsBeforeView(const std::string& tool)
	{
		static const char* const open[] = { "turn_view", "wait_for_my_turn", "status", "set_identity", "take_seat", "release_seat", "testing_mode" };
		for(size_t i = 0; i < sizeof(open) / sizeof(open[0]); ++i)
			if(tool == open[i]) return true;
		return false;
	}
}

std::string McpViewGateRefusal(int player, int turn, bool bMyTurn, const std::string& tool)
{
	if(!bMyTurn || RunsBeforeView(tool)) return "";
	std::map<int, int>::const_iterator it = s_viewedTurn.find(player);
	if(it != s_viewedTurn.end() && it->second == turn) return "";
	return "DO NOW: call turn_view. Nothing else runs on your turn until you have opened it.";
}

void McpViewGateViewed(int player, int turn) { s_viewedTurn[player] = turn; }

void McpViewGateReset() { s_viewedTurn.clear(); }
