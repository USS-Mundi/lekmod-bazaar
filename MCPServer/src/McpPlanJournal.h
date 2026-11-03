#pragma once

// The turn cycle's debug journal, for tuning it: off unless Logs\MCPServer-plan-journal.on exists under the game's
// My Games folder. While on, each plan's outcome goes to Logs\MCPServer-plan-journal.jsonl, one JSON object a line.
// Built without the game's precompiled header, as McpCrashLog.cpp is. Safe from any thread.

#include <string>

bool McpPlanJournalOn();
void McpPlanJournalAppend(const std::string& jsonLine);

// Logs\MCPServer-agents-together.on in the same folder: a single-player game with model seats plays simultaneous
// turns, the seats taking theirs at the same time as the human (McpApplyTurnMode). Read when a game starts or loads.
bool McpAgentsTogetherOn();
