// The Message Center in the game: the chat room of the game loaded, the table as the game sees it, the agents' say
// and messages, and the window's side of it (MessageCenter.lua through Game.MC*). The rules are McpChatRoom's.
//
// The room is shared by the game thread (tools, the tick) and the UI thread (the window's Lua calls), so every use of
// it takes the room's lock. The UI thread never reads game state: it uses the table the tick last built.
#pragma once

#include <string>
#include <utility>
#include <vector>
#include "picojson.h"
#include "McpChatRoom.h"

// Game thread, every frame: opens this game's chats after a start or load, and refreshes the table the window uses.
void McpChatTick();

// Game thread. The member a civilization is in chat: "human" for the one on screen, else "p<slot>".
std::string McpChatToken(int player);

// Game thread. A player speaks: delivered to the chat's members and waking them. The reply as say returns it, or the
// refusal in err.
picojson::value McpChatSay(int player, const std::string& to, const std::string& words, const std::string& name, std::string& err);

// Game thread. The lines waiting for a player go into its status as messages (and more_messages when capped).
void McpChatAddMessages(int player, picojson::object& status);

// Any thread: the window's side, always as the human.
std::vector<McpChatInfo> McpChatHumanChats();
bool McpChatHumanLines(const std::string& id, int from, std::vector<McpChatLine>& out);
// The id of the chat it went to, or false with the refusal in err.
bool McpChatHumanSend(const std::vector<std::string>& to, const std::string& text, std::string& id, std::string& err);
bool McpChatHumanName(const std::string& id, const std::string& name);
bool McpChatHumanSeen(const std::string& id);
// Every civilization and city-state a model plays or will take back: its token and its name.
std::vector<std::pair<std::string, std::string> > McpChatSeats();
// City-states take part in chats, and show in the window, only while their seats are on.
bool McpChatCityStateSeatsOn();
