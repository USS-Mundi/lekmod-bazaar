#include "McpSeatRules.h"

bool McpMayTakeLeftSeat(bool bOwnLabel, bool bOnScreenTeam)
{
	return bOwnLabel || bOnScreenTeam;
}
