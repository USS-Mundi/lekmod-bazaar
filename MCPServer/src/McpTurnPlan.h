#pragma once

// The turn cycle in the game, on the game thread: a seat's plan for its next turn, what it could see when it made it,
// and what has changed by the time the turn comes. McpTurnCycle.h holds the game-free parts; run_plan's loop over the
// orders is the bridge's (McpBridge.cpp), so each order runs exactly as a call from the model would.

#include "picojson.h"
#include <string>

class CvPlayer;

#define MCP_TOOL_PLAN_TURN "plan_turn"
#define MCP_TOOL_RUN_PLAN "run_plan"
#define MCP_JOB_PLAN_TAKE "plan-take"

// At the start of the seat's turn: the briefing, and when a plan is due, the plan and what changed since it was made.
void McpAddPlanToTurnStart(CvPlayer* me, picojson::object& o);
// run_plan's first step, as a game-thread job: the orders to carry out (the plan is used up), whether unread game
// messages must be acknowledged first, and the debug journal's entry so far. Fills err when there is nothing to run.
picojson::value McpTakePlan(const picojson::object& args, std::string& err);
// end_turn without run_plan: a plan due this turn was set aside.
void McpNoteTurnEnded(CvPlayer* me);
// A new or loaded game: plans are not kept.
void McpTurnPlanReset();
