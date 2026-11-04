#pragma once

// The turn cycle's game-free parts, tested natively (tests/run_turn_cycle_test.sh). A model plans its next turn off
// the clock (plan_turn); when the turn comes it is shown what changed in what it can see since it planned, decides,
// and run_plan carries out the orders it keeps. McpTurnPlan.cpp reads the game into these shapes.

#include "picojson.h"
#include <map>
#include <set>
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

// The game plan a seat keeps across turns: one document of its intentions under fixed headings, so the game can
// show each part beside the state it is about, the turn each heading was last written, and a log of why it changed.
// A clock the seat aims to complete by a turn: a technology (with whatever it needs first), a city's build, a city's
// size, an amount of gold, or the next policy.
struct McpTarget
{
	std::string kind;     // tech, build, size, gold, policy
	std::string subject;  // the technology or the build
	std::string city;     // the city's name, for build and size
	int amount;           // the size, or the gold
	int by;               // the turn it is meant to be done by
	McpTarget() : amount(0), by(0) {}
};

// plan_turn's targets: a list of {"tech": name, "by": turn}, {"city": name, "build": name, "by": turn},
// {"city": name, "size": n, "by": turn}, {"gold": n, "by": turn} or {"policy": "next", "by": turn}. The turn must come
// after nowTurn. False, with err naming the target and what is wrong, when any one is refused; an empty list clears.
bool McpParseTargets(const picojson::value& list, int nowTurn, std::vector<McpTarget>& out, std::string& err);
// "Iron Working by T31", "Caen Library by T36", "Bayeux size 4 by T32", "150 gold by T35", "next policy by T25".
std::string McpTargetLabel(const McpTarget& t);

struct McpGamePlan
{
	std::map<std::string, std::string> sections; // heading -> text
	std::map<std::string, int> written;          // heading -> the turn it was last written
	std::vector<std::string> log;                // "T<turn>: <why>", oldest first
	std::vector<McpTarget> targets;              // what it aims to complete by when
	int turn;                                    // the turn it last changed, -1 before the first plan
	McpGamePlan() : turn(-1) {}
};

// direction, cities, military, civilians, research, policy, relations: the order they are shown in.
const std::vector<std::string>& McpGamePlanHeadings();
// Replaces the headings given; the others stay. A first plan needs direction; a later change needs why, which goes
// into the log. Giving a heading its current text is no change. warnings: what does not stop the change (a plan past
// 700 words). False, with err, when the change is refused.
bool McpGamePlanApply(McpGamePlan& plan, const std::map<std::string, std::string>& changes, const std::string& why,
	int turn, std::string& err, std::vector<std::string>& warnings);
// The plan as text: every heading with the turn it was written ("none written" for an empty one), then the last 8
// log lines.
std::string McpGamePlanText(const McpGamePlan& plan);
// One heading as it is shown beside its state: "plan (T<turn>): <text>", or "plan: none written".
std::string McpGamePlanLine(const McpGamePlan& plan, const std::string& heading);
// A plan saved under the earlier headings (read, arc, thrust, rivals, tripwires) moved onto the current ones: read,
// arc, thrust and tripwires into direction, rivals into relations (only where those are empty), written on the plan's
// last turn. The earlier headings are removed. A plan without them is left as it is.
void McpMigratePlan(McpGamePlan& plan);
// What has no plan: each heading with no text, and "<city> in the cities plan" for each city the cities plan does not
// name.
std::vector<std::string> McpWithoutAPlan(const McpGamePlan& plan, const std::vector<std::string>& cities);

// One line of the turn view: a thing the player can see (a city, a unit, a civilization...), under its section, with
// a key that names the same thing turn after turn, and its fields in the order they are shown.
struct McpViewItem
{
	std::string section;
	std::string key;
	std::vector<std::pair<std::string, std::string> > fields;
};

struct McpViewSection
{
	std::string name;
	std::vector<std::string> lines;
};

// The turn view with what is new marked where it sits: "+ " a thing not there last time, "~ " one whose fields
// changed (the old values follow in "(was: ...)"; for a list, "<field>: removed ...; added ..." with only the
// elements that changed), "- " one gone (its last line), "  " the same. Fields in ticking
// (counters that move every turn) are shown but do not count as a change. Sections come in the order now has them,
// then any only before had. Without bHasBefore (the first view since the game loaded) nothing is marked. A field
// named name or type is shown by its value alone; the others as "<field> <value>", joined by " · ".
std::vector<McpViewSection> McpMarkView(const std::vector<McpViewItem>& before, const std::vector<McpViewItem>& now,
	const std::set<std::string>& ticking, bool bHasBefore);

// Turns until gold reaches cost at rate a turn: 0 if it already has, -1 if it never will.
int McpTurnsToAfford(int cost, int gold, int rate);
// The turn a plan is made on, which it runs after: the game turn during the seat's own turn, else the turn the seat
// last ended (-1 if none since the game loaded), since the game may already be on the turn the seat is waiting for.
int McpPlanMadeOnTurn(int gameTurn, bool bMyTurn, int lastEndedTurn);

// After a seat asks to end its turn, the game runs its units' queued moves, and those can hand the turn back (a unit
// that finished with moves left, or woke needing orders). bTurnOver once the seat's turn has passed; bMyTurn while it
// is the seat's turn and not ending.
enum McpEndTurnState { MCP_TURN_ENDING, MCP_TURN_ENDED, MCP_TURN_HANDED_BACK };
McpEndTurnState McpEndTurnOutcome(bool bTurnOver, bool bMyTurn);

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
