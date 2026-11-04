#pragma once

// Clocks: what a city or the empire is counting toward (a build, growth, the next border tile, a technology, a
// policy, a pantheon, a golden age), with the three numbers a player balances tiles against. Game-free, tested
// natively (tests/run_clocks_test.sh); McpClocksGame.cpp reads the game's counters into these.
//
// All amounts are in hundredths. For an amount still needed L, a first turn adding F and each later turn R:
//   turns    = 1 + ceil((L - F) / R), at least 1; never when R <= 0 and F < L (the game's own count)
//   need(k)  = the least extra per turn (on every turn) that finishes within k turns: ceil((L - F - (k-1)R) / k)
//   slack    = the most less per turn that still finishes in the same turns: floor((F + (t-1)R - L) / t)

#include <string>
#include <utility>
#include <vector>

struct McpClock
{
	std::string name;
	long long have100;   // progress so far, shown as have/need
	long long need100;   // the amount it completes at
	long long first100;  // what this turn adds (overflow included)
	long long rate100;   // what each later turn adds
	int gameTurns;       // the game's own count of turns, -1 when the game keeps none for this clock
	McpClock() : have100(0), need100(0), first100(0), rate100(0), gameTurns(-1) {}
};

// Turns to complete at the current rate; -1 when it never completes at this rate.
int McpClockTurns(const McpClock& c);
// The least extra per turn that completes it within k turns (k at least 1); 0 when it already does.
long long McpClockNeedFor(const McpClock& c, int k);
// The most less per turn that still completes it in the same number of turns; 0 when it never completes.
long long McpClockSlack(const McpClock& c);
// The same, keeping it within k turns rather than its current count.
long long McpClockSlackFor(const McpClock& c, int k);
// An amount in hundredths as text with at most one decimal: rounded up (bUp, for a need) or down (for a slack).
std::string McpAmount(long long x100, bool bUp);
// The clock without its name: "<have>/<need> at <rate>/t: <turns>t · need +<n> for <turns-1>t · slack <s>".
std::string McpClockBody(const McpClock& c);
// The turn it completes on at the current rate ("T49"), by the game's count when the game keeps one; "never".
std::string McpClockLands(const McpClock& c, int nowTurn);
// At its edge: the counts agree and its slack is 0, so any less per turn makes it a turn later.
bool McpClockAtEdge(const McpClock& c);
// The briefing's line: "<name> <have>/<need> at <rate>/t: <turns>t · need +<n> for <turns-1>t · slack <s>". When the
// game keeps its own count and it differs, the game's count is shown and need and slack are withheld.
std::string McpClockLine(const McpClock& c);
// A queue: each item's own turns, added up ("Settler 4t → Library 15t").
std::string McpQueueLine(const std::vector<std::pair<std::string, int> >& items);
// A target: the clock to complete by turn byTurn, it being turn nowTurn.
std::string McpTargetLine(const std::string& label, const McpClock& c, int byTurn, int nowTurn);
