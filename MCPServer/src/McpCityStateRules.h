#pragma once

// What a switched-on city-state allows its ally, by the ally's influence: game-free, so it is tested natively
// (tests/run_city_state_rules_test.sh). The values come from the mod's SQL; these defaults equal the rows it ships.

#include <string>
#include <vector>

// One step of influence: from minInfluence up to the next step.
struct McpCsTier
{
	int minInfluence;
	int sendPercent;   // share of its military units it will send out of its own land
	int sendMin;       // ...but at least this many
	bool revealSight;  // what its units see stays uncovered on the ally's map
	bool enterAllyLand;
};

// The text its player reads about its ally, by influence.
struct McpCsDisposition
{
	int minInfluence;
	std::string name;
	std::string text;
};

struct McpCsSettings
{
	int influenceCost;  // extra influence the ally loses each turn the city-state is switched on
	int contestedLead;  // the ally leads the runner-up by fewer than this: contested
	int secureLead;     // ...by at least this: secure
	int trendTurns;     // rising or falling is judged over this many turns
	int trendAmount;    // ...by at least this much influence
};

enum McpCsModifier { MCP_CS_CONTESTED, MCP_CS_SECURE, MCP_CS_RISING, MCP_CS_FALLING };

std::vector<McpCsTier> McpCsDefaultTiers();
std::vector<McpCsDisposition> McpCsDefaultDispositions();
McpCsSettings McpCsDefaultSettings();

// The highest step at or below influence; NULL below the lowest.
const McpCsTier* McpCsTierFor(const std::vector<McpCsTier>& tiers, int influence);
const McpCsDisposition* McpCsDispositionFor(const std::vector<McpCsDisposition>& dispositions, int influence);

// How many of its military units it sends out at this step: never its last one, never below 0.
int McpCsSendCap(const McpCsTier* tier, int militaryUnits);

const char* McpCsModifierText(McpCsModifier modifier);
// allyHistory: the ally's influence once a turn, oldest first.
std::vector<std::string> McpCsModifiers(const McpCsSettings& settings, int allyInfluence, int runnerUpInfluence, const std::vector<int>& allyHistory);
