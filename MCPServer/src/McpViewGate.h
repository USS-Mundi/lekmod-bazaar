#pragma once

#include <string>

// The turn view comes first. On a seat's own turn, every tool but the few that lead to the view is refused until
// the seat has opened its turn view that turn. Game thread only.

// "" when the tool may run; otherwise the refusal to send back.
std::string McpViewGateRefusal(int player, int turn, bool bMyTurn, const std::string& tool);
// The seat opened its turn view on this turn.
void McpViewGateViewed(int player, int turn);
// A new or loaded game: every seat must open its view again.
void McpViewGateReset();
