#pragma once

// The game's per-turn record, read back for reports: the pure parts, with no game headers, so they test natively.

#include <map>
#include <string>
#include "picojson.h"

// "REPLAYDATASET_TOTALGOLD" -> "totalgold"; a name without that prefix is only lowered.
std::string McpReplaySetKey(const std::string& setName);
// The turns a history covers. A negative from or through means the first turn or now; through past now stops at now.
// False, with err, when from is after through or after now.
bool McpHistoryWindow(int iFrom, int iThrough, int iNow, int& iLo, int& iHi, std::string& err);
// One value per turn from iLo to iHi; a turn the game recorded nothing for is null.
picojson::value McpHistorySeries(const std::map<unsigned int, int>& byTurn, int iLo, int iHi);
// Whether a caller sees a player's history: the on-screen session sees every player, a seat only its own.
bool McpHistoryShows(bool bOnScreen, bool bSelf);
// The Logs file the whole record goes to: MCPServer-history-<game>.json, the id kept to letters, digits, - and _.
std::string McpHistoryFileName(const std::string& gameId);
