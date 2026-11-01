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
