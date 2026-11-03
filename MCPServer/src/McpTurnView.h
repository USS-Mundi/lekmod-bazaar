#pragma once

// The turn view: everything the seat can see, one thing per line, gathered from the same tools the model can call
// (cities and their citizens, units, research, policies, the overview screens), plus what the game says happened
// and what it is waiting on. McpTurnPlan.cpp marks what changed since the seat's last turn (McpMarkView) and sets each
// part of the seat's game plan beside the state it is about. On the game thread.

#include "McpTurnCycle.h"
#include <set>
#include <string>
#include <vector>

class CvPlayer;

// A unit waiting for orders, or a city with nothing to build: its id, so planned orders that name it can take it off
// the list, and how it is shown.
struct McpWithoutOrder
{
	bool city;
	int id;
	std::string label;
	McpWithoutOrder() : city(false), id(-1) {}
};

struct McpTurnViewData
{
	std::vector<McpViewItem> items;
	std::set<std::string> ticking;       // fields that move every turn: shown, never counted as a change
	std::vector<std::string> news;       // what happened since the last turn: the game's events and messages
	std::vector<std::string> decisions;  // what the game is waiting on: pending choices, requests, what blocks the turn
	std::vector<McpWithoutOrder> withoutOrder; // units waiting for orders and cities with nothing to build
	std::vector<std::string> cities;     // the seat's city names
};

// The items, the fields that tick and what has no order. Reads only; safe at a turn's end and its start.
McpTurnViewData McpGatherTurnView(CvPlayer* me);
// Adds what happened and what the game waits on, from the briefing. Once, at the turn's start: the briefing tracks what
// it has already reported.
void McpGatherTurnNews(McpTurnViewData& d);
