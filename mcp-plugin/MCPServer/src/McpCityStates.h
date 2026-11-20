#pragma once

// City-states an ally switches on: while one is on, a model may play it as a seat. Game thread only.

#include <string>
#include "McpCityStateRules.h"
#include "picojson.h"

// Reads the settings tables from the game's database; a missing table is reported in the log and its defaults apply.
void McpCsLoadSettings();
const McpCsSettings& McpCsGetSettings();

// Only its current ally can switch a city-state on. Until a model takes its seat, the game's AI keeps playing it.
bool McpCsSwitchOn(PlayerTypes eMinor, PlayerTypes ePatron, std::string& err);
// A seat is handed back to the game's AI at once, or when its turn ends if it is in the middle of one.
void McpCsSwitchOff(PlayerTypes eMinor, const std::string& why);
bool McpCsIsSwitchedOn(PlayerTypes eMinor);
PlayerTypes McpCsPatron(PlayerTypes eMinor);   // NO_PLAYER while it is off
std::string McpCsPlayedBy(PlayerTypes eMinor); // the model on its seat, or "" while the game's AI plays it

// The patron's influence with it, and what that buys (all 0 / NULL while it is off).
int McpCsInfluence(PlayerTypes eMinor);
const McpCsTier* McpCsCurrentTier(PlayerTypes eMinor);
int McpCsSendCapFor(PlayerTypes eMinor);
int McpCsUnitsOut(PlayerTypes eMinor);   // its military units outside its own land

// The conversation between a city-state and a civ, kept for the whole game (the last 40 lines), so a city-state that
// is switched on again, or whose alliance comes back, picks up where it left off.
void McpCsNoteLine(PlayerTypes eMinor, PlayerTypes eCiv, const std::string& line);
std::vector<std::string> McpCsConversation(PlayerTypes eMinor, PlayerTypes eCiv);

// What each civ has done for and to each city-state, from the start of the game (since it was loaded: not kept in a
// save yet), the last 30 per civ, each line "turn N: what". Recorded for every city-state, switched on or not.
void McpCsNoteDeed(PlayerTypes eMinor, PlayerTypes eCiv, const std::string& what);
std::vector<std::string> McpCsDeeds(PlayerTypes eMinor, PlayerTypes eCiv);
const char* McpMinorQuestName(int eQuest); // a MinorCivQuestTypes; McpMinorCivs.cpp

// A switched-on city-state a model has taken (or one switched off during its own turn, until that turn ends).
bool McpIsCityStateSeat(PlayerTypes e);
// Live sight: a sharing unit sees for its city-state's ally as the ally's own units do, until it stops sharing, dies,
// the city-state is switched off or the ally's standing falls below the sight step.
bool McpCsShareSight(CvUnit* u, bool bOn, std::string& err);
std::vector<int> McpCsSharingUnits(PlayerTypes eMinor);
// At the highest standing the ally does not ask: every unit of a switched-on city-state sees for them, and its
// player cannot stop one. The second is the influence that takes, -1 when no step shares sight.
bool McpCsEveryUnitSees(PlayerTypes eMinor);
int McpCsEveryUnitSeesFrom();
void McpCsSightAfterMove(const CvUnit* u);
void McpCsSightBeforeKill(const CvUnit* u);
// What a city-state's player sees in status: its ally, what the alliance buys, its own character.
picojson::value McpCsSeatView(PlayerTypes eMinor);

// The army rules, by the ally's influence. "" when a city-state's unit may go to dest; otherwise why not: a unit in its
// own land heading out while as many as its ally may have are out already.
std::string McpCsMoveRefusal(const CvUnit* u, const CvPlot* dest);
// Its units may enter its ally's land (from the step that allows it). Called from hot paths: cheap for non-minors.
bool McpCsMayEnter(PlayerTypes eMinor, TeamTypes eTeam);
bool McpCsTeamMayEnter(TeamTypes eMinorTeam, TeamTypes eLandTeam);
// City-states allied to one team are one side while both alliances last (McpCsSameSide): each may cross the other's
// land, switched on or not. McpCsPeaceWithSameSide ends a war one of them has with another of its side.
bool McpCsTeamsSameSide(TeamTypes eA, TeamTypes eB);
void McpCsPeaceWithSameSide(PlayerTypes eMinor);
// What a unit of a switched-on city-state sees stays uncovered on its ally's map (from the step that allows it).
void McpCsRevealAround(const CvUnit* u);

// Lock-free and cheap, for hooks the engine calls constantly and from more than one thread.
bool McpCsAnySwitchedOn();
bool McpCsTeamSwitchedOn(TeamTypes eTeam);
// Switched-on city-states no model has taken yet, for status.seats_waiting_for_a_player.
std::vector<PlayerTypes> McpCsWaitingForModel();

// Every game update: finishes hand-backs that waited for a turn to end, ends the turn of a city-state whose model is
// gone (and switches it off), and catches a lost alliance.
void McpCsTick();
// A new or loaded game: forgets every switched-on city-state and reloads the settings.
void McpCsResetForNewGame();

// Hook handlers.
void McpCsAfterSetAlly(PlayerTypes eMinor, PlayerTypes eNewAlly);
void McpCsAfterMinorTurn(PlayerTypes eMinor);
