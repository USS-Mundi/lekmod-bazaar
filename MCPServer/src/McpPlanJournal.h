#pragma once

// The turn cycle's debug journal, for tuning it: off unless Logs\MCPServer-plan-journal.on exists under the game's
// My Games folder. While on, each plan's outcome goes to Logs\MCPServer-plan-journal.jsonl, one JSON object a line.
// Built without the game's precompiled header, as McpCrashLog.cpp is. Safe from any thread.

#include <string>

bool McpPlanJournalOn();
void McpPlanJournalAppend(const std::string& jsonLine);
