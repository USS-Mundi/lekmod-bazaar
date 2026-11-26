#pragma once
#include <string>

// The page about a map script that game_settings carries as "map": what the land and sea are like, from
// docs/agent/maps/<script>.md. mapType is game_settings' map_type (the script's file name without .lua), in any case.
// NULL for a script with no page.
const char* McpMapPage(const std::string& mapType);
