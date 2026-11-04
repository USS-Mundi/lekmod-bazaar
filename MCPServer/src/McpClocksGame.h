#pragma once

// The clocks read from the game (McpClocks.h holds the arithmetic): each city's build, growth and next border tile
// and its build queue; the empire's research and research queue, next policy, pantheon and golden age, and where
// science, culture and faith come from city by city; and the seat's targets. On the game thread.

#include "McpClocks.h"
#include "McpTurnCycle.h"
#include <string>
#include <utility>
#include <vector>

class CvPlayer;
class CvCity;

struct McpCityClocks
{
	std::vector<McpClock> clocks;                       // build, growth, next border tile, those that apply
	std::vector<std::string> notes;                     // growth paused or starving, nothing being built, the price to buy the build
	std::vector<std::pair<std::string, int> > queue;    // each queued item and its own turns
};

// A store a negative rate empties (McpDrainBody), and what happens once it is below zero.
struct McpDrain
{
	std::string name;
	long long have100, rate100;
	std::string then;
};

struct McpEmpireClocks
{
	std::vector<McpClock> clocks;                       // research, next policy, pantheon, golden age
	std::vector<McpDrain> drains;                       // the treasury, while gold is falling
	std::vector<std::string> notes;                     // gold while not falling, a golden age under way, a pantheon founded
	std::vector<std::pair<std::string, int> > researchQueue;
	std::vector<std::pair<std::string, std::string> > sources; // science, culture, faith: "Caen 6 · Bayeux 3 · other 2"
};

McpCityClocks McpGatherCityClocks(CvCity* c);
McpEmpireClocks McpGatherEmpireClocks(CvPlayer* me);
// A target's line ("Iron Working by T31: 102 left at 10/t lands T34 · need +2.8/t"), or why it cannot be counted.
std::string McpTargetStatus(CvPlayer* me, const McpTarget& t);
