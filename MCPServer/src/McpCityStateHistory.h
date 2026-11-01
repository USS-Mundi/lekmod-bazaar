#pragma once

// A city-state's history in one game, kept in a file the plugin adds to as things happen: game-free, so it is
// tested natively (tests/run_city_state_history_test.sh). McpCityStates.cpp does the file reading and writing.

#include <string>
#include <vector>

// One deed toward the city-state ("deed") or one line of its conversation ("said").
struct McpCsHistoryEntry
{
	std::string kind;
	int civ;              // the civ's player number, as the game numbers it
	std::string civName;
	std::string text;
};

// The city-state's file name, with any character a file name cannot hold replaced.
std::string McpCsHistoryFileName(const std::string& cityState);
// What a new file starts with.
std::string McpCsHistoryHeader(const std::string& cityState, const std::string& gameId);
// One entry as one line of the file, newline included.
std::string McpCsHistoryLine(const McpCsHistoryEntry& e);
// Every entry in a file's content, oldest first.
std::vector<McpCsHistoryEntry> McpCsHistoryParse(const std::string& content);
