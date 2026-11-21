#pragma once

// Where a city-state's history file lives and how it is read and added to: My Games\Sid Meier's Civilization 5\MCP\
// <game id>\city-states\<file>. Built without the game's precompiled header, as McpMapDraw.cpp is. Names are UTF-8.

#include <string>

// The whole file, or nothing when there is none yet.
std::string McpCsFileRead(const std::string& gameId, const std::string& fileName);
// Adds a line, writing headerIfNew first when the file does not exist yet.
void McpCsFileAppend(const std::string& gameId, const std::string& fileName, const std::string& headerIfNew, const std::string& line);
// The file's path as a reader on this machine opens it: under Wine the Unix path, so an agent on the Mac can read it.
std::string McpCsFilePath(const std::string& gameId, const std::string& fileName);

// The same, for another folder of the game's: My Games\Sid Meier's Civilization 5\MCP\<game id>\<sub>\<file>.
std::string McpGameFileRead(const std::string& gameId, const char* sub, const std::string& fileName);
void McpGameFileAppend(const std::string& gameId, const char* sub, const std::string& fileName, const std::string& headerIfNew, const std::string& line);

// The folder My Games\Sid Meier's Civilization 5\MCP\<game id>\<sub>, made when missing, as a UTF-8 Windows path;
// "" when there is no game id or no Documents folder.
std::string McpGameFolder(const std::string& gameId, const char* sub);
// A path as this machine's own programs name it: under Wine (CrossOver on a Mac) the Unix path, otherwise unchanged.
std::string McpHostPath(const std::string& windowsPath);
// True when the game runs under Wine (CrossOver on a Mac).
bool McpRunsUnderWine();

// The city-state switch-on file, My Games\Sid Meier's Civilization 5\MCPServer-city-states-on: only while it exists
// does the plugin register its city-state hooks at start (city-state seats and what city-states remember). Without it
// the game runs city-states exactly as it would without the plugin. Read once, when the plugin starts.
bool McpCityStatesOnAtStart();
