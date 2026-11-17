#pragma once

// What a switched-on city-state allows its ally, by the ally's influence: game-free, so it is tested natively
// (tests/run_city_state_rules_test.sh). The values come from the mod's SQL; these defaults equal the rows it ships.

#include <map>
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

// The influence at which each thing its ally does not have yet opens, -1 when nothing higher opens it.
struct McpCsNextSteps
{
	int moreUnitsOut;
	int sight;
	int enterAllyLand;
};
McpCsNextSteps McpCsNextStepsFor(const std::vector<McpCsTier>& tiers, int influence, int militaryUnits);

// The ally's message to its city-state is the one word that switches it off: "dismiss" (or "/dismiss"), any case.
bool McpCsIsDismissal(const std::string& text);

// The human may write to a city-state while it lives and the human is its ally: the rule of the log window's chat and
// the Message Center's list. ally and human are player numbers, -1 for none.
bool McpCsHumanMaySpeak(bool bAlive, bool bMinor, int ally, int human);

// Gold a city-state is given goes into its treasury, and tribute comes out of it: the game moves neither.
// The gold that left a giver's treasury over a gift (0 when the city-state refused it).
int McpCsGoldMoved(int giverBefore, int giverAfter);
// What the city-state pays of a tribute: what it has, never below zero. The taker's sum stays the game's.
int McpCsTributePaid(int treasury, int tribute);
// The deeds as its history records them, with the treasury before and after.
std::string McpCsGiftDeed(int gold, int treasuryBefore);
std::string McpCsTributeDeed(int tribute, int paid, int treasuryBefore);

// Two city-states are one side while each has an ally and the two allies are on one team: they pass through each
// other's land and do not fight. allyTeam is -1 for a city-state with no ally.
bool McpCsSameSide(bool aMinor, bool bMinor, int allyTeamA, int allyTeamB);

// A line of a city-state's conversation as the plugin records it, "turn N, <speaker>: text": its turn, whether the
// city-state said it, and the text. False for anything else.
bool McpCsReadTalkLine(const std::string& line, const std::string& cityState, int& turn, bool& byCityState, std::string& text);

// Live sight a city-state shares with its ally: the plots each sharing unit has lit for the ally's team. It lives in
// the city-state player's script data, which a save writes together with the visibility counts, so a load can take
// exactly those plots off again.
struct McpCsSightLedger
{
	int team;                                    // -1 while nothing is lit
	std::map<int, std::vector<int> > unitPlots;  // unit id -> plot indices lit for its sight
	McpCsSightLedger() : team(-1) {}
};
// "" when nothing is lit, so the script data is left empty.
std::string McpCsSightLedgerWrite(const McpCsSightLedger& ledger);
// false when the text is not a ledger this plugin wrote; "" reads as an empty ledger.
bool McpCsSightLedgerRead(const std::string& text, McpCsSightLedger& ledger);

const char* McpCsModifierText(McpCsModifier modifier);
// allyHistory: the ally's influence once a turn, oldest first.
std::vector<std::string> McpCsModifiers(const McpCsSettings& settings, int allyInfluence, int runnerUpInfluence, const std::vector<int>& allyHistory);

// Why a city-state's seat may not make this call, or "" when it may: what the game's rules for city-states decide, and
// what would strip the city the game takes back (disbanding units, turning the city to a process, the treasury's
// figures). action and item are the call's own arguments, "" when it has none.
std::string McpCsToolRefusal(const std::string& tool, const std::string& action, const std::string& item);
