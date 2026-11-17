#pragma once

struct CivHostInfo;
struct lua_State;

// For an after-hook on CvLuaGame::RegisterMembers, which leaves the Game table on top of the Lua stack.
void McpAddGameMethods(const CivHostInfo* host, lua_State* L);
#include <string>
// A seat's second leader, when the Second Leader option gave it one: Game.SecondLeader(player), as that mod's own
// screens call it. False when the option is off, the mod is not loaded, or the seat has none. Game thread only.
bool McpLuaSecondLeader(int player, std::string& civType, std::string& leaderType);
