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

// At the start of the seat's turn, once a turn: the turn view (everything it can see, what changed since it last ended a
// turn marked in place, its game plan beside each part) and its land pictures; when a plan is due, the plan's orders.
void McpAddPlanToTurnStart(CvPlayer* me, picojson::object& o);
// run_plan's first step, as a game-thread job: the orders to carry out (the plan is used up), whether unread game
// messages must be acknowledged first, and the debug journal's entry so far. Fills err when there is nothing to run.
picojson::value McpTakePlan(const picojson::object& args, std::string& err);
// What the agent plans from once its turn is over: what the next turns bring and its whole game plan.
void McpAddPlanningView(CvPlayer* me, picojson::object& o);
// end_turn without run_plan: a plan due this turn was set aside.
void McpNoteTurnEnded(CvPlayer* me);
// A new or loaded game: plans are not kept.
void McpTurnPlanReset();
// A seat that ended its turn, or sat down before its turn, owes a plan for the next one, until it sends one.
void McpOwePlan(int seat);
// For its wait: true once per owed plan, so that wait returns at once and asks for the plan. A seat whose turn has come
// owes none. Any thread.
bool McpTakePlanReminder(int seat, bool bMyTurn);
