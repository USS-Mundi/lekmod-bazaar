#pragma once

// The turn view: what a player looks at every turn, one thing per line, gathered from the same tools the model can
// call: the top bar and the empire's clocks, others' units and cities in sight, the seat's units, its cities with
// their clocks and tiles, research and policies open now, the players and city-states met, plus what the game says
// happened and what it is waiting on. The other screens stay with their tools; the view says which changed. McpTurnPlan.cpp marks what changed since the seat's last turn (McpMarkView) and sets each
// part of the seat's game plan beside the state it is about. On the game thread.

#include "McpTurnCycle.h"
#include "picojson.h"
#include <map>
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
	std::vector<std::string> blocks;     // what stops the turn from ending (McpDecisionKind)
	std::vector<std::string> fromOthers; // what other players are waiting on: requests, deals, messages
	std::vector<std::string> available;  // what can be done this turn and nothing requires: promotions, upgrades, bombards
	std::vector<std::string> state;      // the briefing's warnings: gold falling, unhappiness, starvation, damage
	std::vector<std::string> damaged;    // pillaged improvements and roads in the seat's land, until a worker mends them
	std::vector<McpWithoutOrder> withoutOrder; // units waiting for orders and cities with nothing to build
	std::vector<std::string> cities;     // the seat's city names
	std::map<std::string, std::string> screens; // each screen left to its tool, by tool name, reduced to its words
};

// The items, the fields that tick and what has no order. Reads only; safe at a turn's end and its start.
McpTurnViewData McpGatherTurnView(CvPlayer* me);
// The pillaged improvements and roads in the seat's land, one line each (McpDamagedLine).
std::vector<std::string> McpDamagedInLand(CvPlayer* me);
// Adds what happened and what the game waits on, from the briefing. Once, at the turn's start: the briefing tracks what
// it has already reported.
void McpGatherTurnNews(McpTurnViewData& d);
// One city in full, as text: its line with buildings, its clocks and queue, and its tiles.
picojson::value McpCityDetailText(CvPlayer* me, int cityId, std::string& err);
