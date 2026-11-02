#pragma once

// The turn cycle's game-free parts, tested natively (tests/run_turn_cycle_test.sh). A model plans its next turn off
// the clock (plan_turn); when the turn comes it is shown what changed in what it can see since it planned, decides,
// and run_plan carries out the orders it keeps. McpTurnPlan.cpp reads the game into these shapes.

#include <string>
#include <vector>

// A unit as one player sees it. nearDist and nearWhat: the closest of that player's units and cities to it, -1 if none.
struct McpSeenUnit
{
	int id;
	int owner;
	std::string who;   // "you", or the owner's name
	std::string type;
	int x, y;
	int hp;
	bool enemy;        // its owner is at war with the player, or barbarian
	int nearDist;
	std::string nearWhat;
	McpSeenUnit() : id(-1), owner(-1), x(-1), y(-1), hp(0), enemy(false), nearDist(-1) {}
};

struct McpSeenCity
{
	int id;
	std::string name;
	int hp, maxHp;
	McpSeenCity() : id(-1), hp(0), maxHp(0) {}
};

// What one player can see: its own units and cities, others' units in sight, its wars and two numbers.
struct McpView
{
	int gold;
	int happiness;
	std::vector<McpSeenUnit> mine;
	std::vector<McpSeenUnit> theirs;
	std::vector<McpSeenCity> cities;
	std::vector<std::string> atWarWith;
	McpView() : gold(0), happiness(0) {}
};

// What changed between the view a plan was made in and now, most urgent first: wars, losses, damage, others' units
// by distance (an enemy anywhere in sight, anyone else within 5 tiles), then the numbers. Past limit lines, the rest
// are counted in one last line.
std::vector<std::string> McpViewChanges(const McpView& planned, const McpView& now, size_t limit);

// One order of a plan: a tool and its arguments as JSON text.
struct McpPlannedOrder
{
	std::string tool;
	std::string args;
};

// The orders run_plan carries out: the plan without the dropped ones (numbered from 1), with the added ones before a
// final end_turn. A number outside the plan sets err and runs nothing.
std::vector<McpPlannedOrder> McpOrdersToRun(const std::vector<McpPlannedOrder>& plan, const std::vector<int>& drop,
	const std::vector<McpPlannedOrder>& add, std::string& err);

// The kinds a changed plan's reason can be, for the debug journal.
bool McpIsWhyKind(const std::string& kind);
std::string McpWhyKinds();
