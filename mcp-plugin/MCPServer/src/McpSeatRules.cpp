#include "McpSeatRules.h"

bool McpMayTakeLeftSeat(bool bOwnLabel, bool bOnScreenTeam)
{
	return bOwnLabel || bOnScreenTeam;
}

std::string McpTakeSeatNext(bool bTurnNow, bool bCityState, bool bBack)
{
	std::string next = (!bCityState && !bBack)
		? "greet the human with say first, short and in character: they read it in their Message Center. Then " : "";
	next += bTurnNow ? "it is your turn: call wait_for_my_turn for the briefing and play it; "
		: bCityState ? "you have no plan yet: send your next turn with plan_turn now, then call wait_for_my_turn; "
		: "call wait_for_my_turn: it tells you when to plan your turn; ";
	return next + (bCityState ? "a city-state's turn comes after every major civilization's" : "your turn comes after the human's each round");
}
