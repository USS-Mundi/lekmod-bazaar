#pragma once

// The turn cycle's game-free parts, tested natively (tests/run_turn_cycle_test.sh). A model plans its next turn off
// the clock (plan_turn); when the turn comes it is shown what changed in what it can see since it planned, decides,
// and run_plan carries out the orders it keeps. McpTurnPlan.cpp reads the game into these shapes.

#include <map>
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
	bool cityState;    // its owner is a city-state
	int nearDist;
	std::string nearWhat;
	McpSeenUnit() : id(-1), owner(-1), x(-1), y(-1), hp(0), enemy(false), cityState(false), nearDist(-1) {}
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
// by distance (an enemy anywhere in sight, another civilization's within 5 tiles; a city-state's only when at war),
// then gold when it emptied or moved by 50 or more and happiness when it crossed zero or moved by 3 or more. Past
// limit lines, the rest are counted in one last line.
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

// The game plan a seat keeps across turns: fixed headings so the game can show and track it, the seat's own words
// under each, and a log of why it changed.
struct McpGamePlan
{
	std::map<std::string, std::string> sections; // heading -> text
	std::vector<std::string> log;                // "T<turn>: <why>", oldest first
	int turn;                                    // the turn it last changed, -1 before the first plan
	McpGamePlan() : turn(-1) {}
};

// read, arc, thrust, rivals, tripwires: the order they are shown in.
const std::vector<std::string>& McpGamePlanHeadings();
// Replaces the headings given; the others stay. A first plan needs arc and thrust; a later change needs why, which
// goes into the log. Giving a heading its current text is no change. warnings: what does not stop the change (a plan
// past 450 words). False, with err, when the change is refused.
bool McpGamePlanApply(McpGamePlan& plan, const std::map<std::string, std::string>& changes, const std::string& why,
	int turn, std::string& err, std::vector<std::string>& warnings);
// The plan as text: every heading and the last 8 log lines, or only arc, thrust and tripwires.
std::string McpGamePlanText(const McpGamePlan& plan, bool bFull);
// Turns until gold reaches cost at rate a turn: 0 if it already has, -1 if it never will.
int McpTurnsToAfford(int cost, int gold, int rate);
// The turn a plan is made on, which it runs after: the game turn during the seat's own turn, else the turn the seat
// last ended (-1 if none since the game loaded), since the game may already be on the turn the seat is waiting for.
int McpPlanMadeOnTurn(int gameTurn, bool bMyTurn, int lastEndedTurn);

// One picture of a seat's land: the candidate center it is drawn around, the cities within reach of it, and the
// distance to the farthest of them.
struct McpPicture
{
	int center;
	std::vector<int> cities;
	int farthest;
	McpPicture() : center(-1), farthest(0) {}
};
// Pictures that together show every city, at most maxPictures. dist[c][k] is the distance from candidate center c to
// city k; a city is in a picture when it is within reach of the center. Each picture takes the center that shows the
// most cities not yet shown, then the one whose farthest city is nearest, then the first.
std::vector<McpPicture> McpPlanPictures(const std::vector<std::vector<int> >& dist, int reach, int maxPictures);
