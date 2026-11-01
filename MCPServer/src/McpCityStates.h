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

// A switched-on city-state a model has taken (or one switched off during its own turn, until that turn ends).
bool McpIsCityStateSeat(PlayerTypes e);
// "" when a city-state's player may use the tool; otherwise why not.
std::string McpCsToolRefusal(const std::string& tool);
// What a city-state's player sees in status: its ally, what the alliance buys, its own character.
picojson::value McpCsSeatView(PlayerTypes eMinor);

// Every game update: finishes hand-backs that waited for a turn to end, ends the turn of a city-state whose model is
// gone (and switches it off), and catches a lost alliance.
void McpCsTick();
// A new or loaded game: forgets every switched-on city-state and reloads the settings.
void McpCsResetForNewGame();

// Hook handlers.
void McpCsAfterSetAlly(PlayerTypes eMinor, PlayerTypes eNewAlly);
void McpCsAfterMinorTurn(PlayerTypes eMinor);
