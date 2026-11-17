#pragma once
// What a seat can know of the world at one moment, and the deeds that lie between two such moments: wars begun and
// ended, cities taken, razed and founded close by, players gone, friendships, denouncements, pacts and the seat's own
// deals. Every deed is something the game says happened; nothing here is a strength, a ratio or an opening.
// No game headers, so it is tested natively (tests/run_world_deeds_test.sh). McpChronicleGame.cpp fills the world.
#include "picojson.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct McpWorldCity
{
	std::string name, owner, firstOwner;   // civilization names; firstOwner founded it
	bool firstCapital;                     // a major civilization's first capital
	int founded;                           // the turn it was founded
	std::string nearCity;                  // the seat's city nearest to it, "" for the seat's own cities
	int nearTiles;
	McpWorldCity() : firstCapital(false), founded(-1), nearTiles(-1) {}
};

struct McpWorld
{
	int turn;
	std::string me;
	std::set<std::string> players;                               // majors the seat has met that are in the game, the seat too
	std::map<std::pair<std::string, std::string>, std::string> wars;   // (a, b) with a < b -> who declared, "" when not known
	std::set<std::pair<std::string, std::string> > friends, pacts;     // (a, b) with a < b
	std::set<std::pair<std::string, std::string> > denounced;          // (who, whom)
	std::map<int, McpWorldCity> cities;                          // by city id: the cities of players the seat has met, and its own
	std::map<std::string, std::set<std::string> > deals;         // the seat's deals in force, by player, each as its terms
	McpWorld() : turn(-1) {}
};

struct McpWorldDeed { std::string with, text; };

// The deeds between two moments, each filed under the player it is about, in the seat's words. gone gets the players
// that left the game. A world with turn -1 is no moment at all: nothing lies between it and anything.
std::vector<McpWorldDeed> McpWorldDeeds(const McpWorld& before, const McpWorld& after, std::vector<std::string>& gone);
// The pair in the order the sets keep it.
std::pair<std::string, std::string> McpWorldPair(const std::string& a, const std::string& b);
picojson::value McpWorldToJson(const McpWorld& w);
bool McpWorldFromJson(const picojson::value& v, McpWorld& w);
