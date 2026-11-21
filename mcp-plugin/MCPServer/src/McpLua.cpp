#include "CvGameCoreDLLPCH.h"
#include "CivPluginApi.h"
#include "McpLua.h"
#include "McpBridge.h"
#include "McpPopups.h"
#include "McpSeat.h"
#include "McpToolSupport.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvMinorCivAI.h"
#include "McpCityStates.h"
#include "McpCityStateRules.h"
#include "CvGlobals.h"
#include "McpChatGame.h"
#include "McpCityStateFiles.h"

namespace
{
	//int, table, int GetLLMLog(); seq, then the { Category, Text, Who, Civ } lines for the player on screen and its teammates, oldest first, then how many connected models play an opponent.
	int lGetLLMLog(lua_State* L)
	{
		std::vector<McpLogEntry> entries;
		int seq = McpBridge::Get().GetLog(entries);
		lua_pushinteger(L, seq);
		lua_createtable(L, (int)entries.size(), 0);
		int n = 0;
		for(size_t i = 0; i < entries.size(); ++i)
		{
			if(!entries[i].teammate) continue;
			lua_createtable(L, 0, 4);
			lua_pushlstring(L, entries[i].category.data(), entries[i].category.size());
			lua_setfield(L, -2, "Category");
			lua_pushlstring(L, entries[i].text.data(), entries[i].text.size());
			lua_setfield(L, -2, "Text");
			lua_pushlstring(L, entries[i].who.data(), entries[i].who.size());
			lua_setfield(L, -2, "Who");
			lua_pushlstring(L, entries[i].civ.data(), entries[i].civ.size());
			lua_setfield(L, -2, "Civ");
			lua_rawseti(L, -2, ++n);
		}
		lua_pushinteger(L, McpBridge::Get().HiddenPlayers());
		return 3;
	}

	//int, string, string GetLLMIdentity(); how many models have named themselves, then the model and label when exactly one has.
	int lGetLLMIdentity(lua_State* L)
	{
		std::vector<McpSession> sessions = McpBridge::Get().Sessions();
		int iNamed = 0;
		McpIdentity only;
		for(size_t i = 0; i < sessions.size(); ++i)
		{
			if(sessions[i].identity.model.empty() || sessions[i].idle) continue;
			++iNamed;
			only = sessions[i].identity;
		}
		lua_pushinteger(L, iNamed);
		lua_pushstring(L, iNamed == 1 ? only.model.c_str() : "");
		lua_pushstring(L, iNamed == 1 ? McpIdentityLabel(only).c_str() : "");
		return 3;
	}

	//void LLMSetActiveMods(string mods); one mod per line, "name<\t>description".
	int lLLMSetActiveMods(lua_State* L)
	{
		const char* szMods = lua_tostring(L, 1);
		std::vector<McpModInfo> mods;
		std::string all = szMods ? szMods : "";
		size_t start = 0;
		while(start < all.size())
		{
			size_t end = all.find('\n', start);
			if(end == std::string::npos) end = all.size();
			std::string line = all.substr(start, end - start);
			size_t tab = line.find('\t');
			McpModInfo mod;
			mod.name = line.substr(0, tab);
			if(tab != std::string::npos) mod.description = McpTool::StripMarkup(line.substr(tab + 1));
			if(!mod.name.empty()) mods.push_back(mod);
			start = end + 1;
		}
		McpBridge::Get().SetActiveMods(mods);
		return 0;
	}

	//void LLMNotePopup(int type, int data1, int data2, int data3, bool option1, string text);
	int lLLMNotePopup(lua_State* L)
	{
		if(!lua_isnumber(L, 1)) return 0;
		const int iType = lua_tointeger(L, 1);
		const int iData1 = lua_tointeger(L, 2);
		const int iData2 = lua_tointeger(L, 3);
		const int iData3 = lua_tointeger(L, 4);
		const bool bOption1 = lua_toboolean(L, 5) != 0;
		const char* szText = lua_tostring(L, 6);
		McpBridge::Get().NotePopup(-1, iType, iData1, iData2, iData3, bOption1, szText ? szText : "", true);
		return 0;
	}

	//bool LLMTakeLeaderMessage(int leader, int diploUIState, string text); true when a model plays the civilization on
	// screen: the leader's words become its game message, and the caller closes the leader screen.
	int lLLMTakeLeaderMessage(lua_State* L)
	{
		const bool bTaken = McpBridge::Get().ScreenPlayed();
		if(bTaken)
		{
			const char* szText = lua_tostring(L, 3);
			// Not bOnScreen, whose popups are dropped once nobody plays the screen: this screen is closed already.
			McpBridge::Get().NotePopup(GC.getGame().getActivePlayer(), MCP_POPUP_LEADER_MESSAGE, lua_tointeger(L, 1), lua_tointeger(L, 2), 0, false, szText ? szText : "", false);
		}
		lua_pushboolean(L, bTaken);
		return 1;
	}

	//string LLMDialogsToClose();
	int lLLMDialogsToClose(lua_State* L)
	{
		std::string names = McpBridge::Get().TakeDialogsToClose();
		lua_pushlstring(L, names.data(), names.size());
		return 1;
	}

	//table GetLLMTeammates(); the AI seats on the on-screen player's team, as { Player, Name } entries.
	// Name is the model's, or the civilization's while no model has named itself on that seat.
	int lGetLLMTeammates(lua_State* L)
	{
		std::vector<PlayerTypes> seats = McpTeammateSeats();
		// City-states the human leads join the list: writing to one asks for its active help.
		PlayerTypes eHuman = GC.getGame().getActivePlayer();
		size_t nSeats = seats.size();
		for(int m = MAX_MAJOR_CIVS; eHuman != NO_PLAYER && m < MAX_CIV_PLAYERS; ++m)
		{
			CvPlayer& minor = GET_PLAYER((PlayerTypes)m);
			if(McpCsHumanMaySpeak(minor.isAlive(), minor.isMinorCiv(), (int)minor.GetMinorCivAI()->GetAlly(), (int)eHuman)) seats.push_back((PlayerTypes)m);
		}
		lua_createtable(L, (int)seats.size(), 0);
		for(size_t i = 0; i < seats.size(); ++i)
		{
			std::string name = i < nSeats ? McpSeatLabel(seats[i]) : std::string(GET_PLAYER(seats[i]).getCivilizationShortDescription()) + " (city-state)";
			if(name.empty()) name = GET_PLAYER(seats[i]).getCivilizationShortDescription();
			lua_createtable(L, 0, 2);
			lua_pushinteger(L, (int)seats[i]);
			lua_setfield(L, -2, "Player");
			lua_pushlstring(L, name.data(), name.size());
			lua_setfield(L, -2, "Name");
			lua_rawseti(L, -2, (int)i + 1);
		}
		return 1;
	}

	//table GetLLMCityStates(); every living city-state the on-screen player has met, as { Player, Name, Speakable }
	// entries. Speakable: the human may write to it now (its ally), the same rule as the log window's chat.
	int lGetLLMCityStates(lua_State* L)
	{
		PlayerTypes eHuman = GC.getGame().getActivePlayer();
		lua_createtable(L, 0, 0);
		if(eHuman == NO_PLAYER || !McpCityStatesOnAtStart()) return 1;
		CvTeam& team = GET_TEAM(GET_PLAYER(eHuman).getTeam());
		int n = 0;
		for(int m = MAX_MAJOR_CIVS; m < MAX_CIV_PLAYERS; ++m)
		{
			CvPlayer& minor = GET_PLAYER((PlayerTypes)m);
			if(!minor.isAlive() || !minor.isMinorCiv() || !team.isHasMet(minor.getTeam())) continue;
			std::string name = McpTool::CivName((PlayerTypes)m);
			lua_createtable(L, 0, 3);
			lua_pushinteger(L, m);
			lua_setfield(L, -2, "Player");
			lua_pushlstring(L, name.data(), name.size());
			lua_setfield(L, -2, "Name");
			lua_pushboolean(L, McpCsHumanMaySpeak(true, true, (int)minor.GetMinorCivAI()->GetAlly(), (int)eHuman));
			lua_setfield(L, -2, "Speakable");
			lua_rawseti(L, -2, ++n);
		}
		return 1;
	}

	//table LLMCityStateTalk(int player); the on-screen player's conversation with a city-state, oldest first, as
	// { Turn, Mine, Text } entries. Kept for the whole game, whoever is its ally now.
	int lLLMCityStateTalk(lua_State* L)
	{
		const int iMinor = lua_tointeger(L, 1);
		PlayerTypes eHuman = GC.getGame().getActivePlayer();
		lua_createtable(L, 0, 0);
		if(eHuman == NO_PLAYER || iMinor < MAX_MAJOR_CIVS || iMinor >= MAX_CIV_PLAYERS) return 1;
		std::string name = McpTool::CivName((PlayerTypes)iMinor);
		std::vector<std::string> talk = McpCsConversation((PlayerTypes)iMinor, eHuman);
		int n = 0;
		for(size_t i = 0; i < talk.size(); ++i)
		{
			int turn = 0;
			bool byCs = false;
			std::string text;
			if(!McpCsReadTalkLine(talk[i], name, turn, byCs, text)) continue;
			lua_createtable(L, 0, 3);
			lua_pushinteger(L, turn);
			lua_setfield(L, -2, "Turn");
			lua_pushboolean(L, !byCs);
			lua_setfield(L, -2, "Mine");
			lua_pushlstring(L, text.data(), text.size());
			lua_setfield(L, -2, "Text");
			lua_rawseti(L, -2, ++n);
		}
		return 1;
	}

	//void LLMChat(int player, string text); the human's message to a teammate seat.
	int lLLMChat(lua_State* L)
	{
		const int iPlayer = lua_tointeger(L, 1);
		const char* szText = lua_tostring(L, 2);
		if(szText && *szText) McpBridge::Get().SendChat(iPlayer, szText);
		return 0;
	}

	//int, int LLMChatUnread(); replies written since the log window was last open, then replies ever.
	int lLLMChatUnread(lua_State* L)
	{
		lua_pushinteger(L, McpBridge::Get().UnreadReplies());
		lua_pushinteger(L, McpBridge::Get().TotalReplies());
		return 2;
	}

	//void LLMChatSeen();
	int lLLMChatSeen(lua_State* L)
	{
		McpBridge::Get().MarkRepliesSeen();
		return 0;
	}

	void AddMethod(const CivHostInfo* host, lua_State* L, const char* name, CivLuaFunction fn)
	{
		host->PushLuaFunction(host->pluginIndex, L, fn);
		lua_setfield(L, -2, name);
	}

	// The Message Center window (MessageCenter.lua). Every call is the human's; McpChatGame keeps the rules.
	void SetString(lua_State* L, const char* key, const std::string& v)
	{
		lua_pushlstring(L, v.data(), v.size());
		lua_setfield(L, -2, key);
	}

	void SetInt(lua_State* L, const char* key, int v)
	{
		lua_pushinteger(L, v);
		lua_setfield(L, -2, key);
	}

	void PushLine(lua_State* L, const McpChatLine& l)
	{
		lua_createtable(L, 0, 4);
		SetString(L, "Time", l.time);
		SetInt(L, "Turn", l.turn);
		SetString(L, "From", l.from);
		SetString(L, "Text", l.text);
	}

	std::vector<std::string> Strings(lua_State* L, int index)
	{
		std::vector<std::string> out;
		if(!lua_istable(L, index)) return out;
		for(int i = 1; ; ++i)
		{
			lua_rawgeti(L, index, i);
			if(lua_isnil(L, -1)) { lua_pop(L, 1); break; }
			const char* s = lua_tostring(L, -1);
			if(s) out.push_back(s);
			lua_pop(L, 1);
		}
		return out;
	}

	//table MCChats(); the chats the human is in, and the whole table: { Id, Members = {tokens}, Name, StartedBy,
	// StartedTurn, Lines, Unread, LastTurn, Stale, Last = { Time, Turn, From, Text } }.
	int lMCChats(lua_State* L)
	{
		std::vector<McpChatInfo> chats = McpChatHumanChats();
		lua_createtable(L, (int)chats.size(), 0);
		for(size_t i = 0; i < chats.size(); ++i)
		{
			const McpChatInfo& c = chats[i];
			lua_createtable(L, 0, 10);
			SetString(L, "Id", c.id);
			lua_createtable(L, (int)c.members.size(), 0);
			for(size_t k = 0; k < c.members.size(); ++k)
			{
				lua_pushlstring(L, c.members[k].data(), c.members[k].size());
				lua_rawseti(L, -2, (int)k + 1);
			}
			lua_setfield(L, -2, "Members");
			SetString(L, "Name", c.name);
			SetString(L, "StartedBy", c.startedBy);
			SetInt(L, "StartedTurn", c.startedTurn);
			SetInt(L, "Lines", c.lines);
			SetInt(L, "Unread", c.unread);
			SetInt(L, "LastTurn", c.lastTurn);
			lua_pushboolean(L, c.stale);
			lua_setfield(L, -2, "Stale");
			PushLine(L, c.last);
			lua_setfield(L, -2, "Last");
			lua_rawseti(L, -2, (int)i + 1);
		}
		return 1;
	}

	//table MCLines(string id, int from); lines from.. of a chat the human is in, { Time, Turn, From, Text }; nil for
	// any other chat.
	int lMCLines(lua_State* L)
	{
		const char* id = lua_tostring(L, 1);
		std::vector<McpChatLine> lines;
		if(!id || !McpChatHumanLines(id, (int)lua_tointeger(L, 2), lines)) { lua_pushnil(L); return 1; }
		lua_createtable(L, (int)lines.size(), 0);
		for(size_t i = 0; i < lines.size(); ++i)
		{
			PushLine(L, lines[i]);
			lua_rawseti(L, -2, (int)i + 1);
		}
		return 1;
	}

	//string MCSend(table members, string text); the human's line to the chat of these members (tokens, or { "all" }),
	// made on its first line. Returns its id, or nil and why not.
	int lMCSend(lua_State* L)
	{
		const char* text = lua_tostring(L, 2);
		std::string id, err;
		if(!McpChatHumanSend(Strings(L, 1), text ? text : "", id, err))
		{
			lua_pushnil(L);
			lua_pushlstring(L, err.data(), err.size());
			return 2;
		}
		lua_pushlstring(L, id.data(), id.size());
		return 1;
	}

	//bool MCName(string id, string name); names a group that has no name yet.
	int lMCName(lua_State* L)
	{
		const char* id = lua_tostring(L, 1);
		const char* name = lua_tostring(L, 2);
		lua_pushboolean(L, id && name && McpChatHumanName(id, name));
		return 1;
	}

	//bool MCSeen(string id); marks the chat's lines seen.
	int lMCSeen(lua_State* L)
	{
		const char* id = lua_tostring(L, 1);
		lua_pushboolean(L, id && McpChatHumanSeen(id));
		return 1;
	}

	//table MCSeats(); every civilization and city-state a model plays or will take back: { Token, Civ }.
	int lMCSeats(lua_State* L)
	{
		std::vector<std::pair<std::string, std::string> > seats = McpChatSeats();
		lua_createtable(L, (int)seats.size(), 0);
		for(size_t i = 0; i < seats.size(); ++i)
		{
			lua_createtable(L, 0, 2);
			SetString(L, "Token", seats[i].first);
			SetString(L, "Civ", seats[i].second);
			lua_rawseti(L, -2, (int)i + 1);
		}
		return 1;
	}

	//string MCChatId(table members); the id of the chat of the human and these members.
	int lMCChatId(lua_State* L)
	{
		std::string id = McpChatRoom::IdOf("human", Strings(L, 1));
		lua_pushlstring(L, id.data(), id.size());
		return 1;
	}
}

namespace
{
	// The Lua state the Game table was last registered in, and that table, kept so the plugin can call a method
	// another plugin added to it.
	lua_State* s_gameState = NULL;
	int s_gameRef = LUA_NOREF;
}

namespace
{
	// The call itself, on plain buffers: a Lua state that has gone away under the plugin must cost a second leader,
	// not the game, so a fault here is caught (structured exceptions, which need a function with nothing to unwind).
	bool CallSecondLeader(lua_State* L, int gameRef, int player, char* civ, char* leader, size_t cap)
	{
		bool ok = false;
		__try
		{
			int top = lua_gettop(L);
			lua_rawgeti(L, LUA_REGISTRYINDEX, gameRef);
			if(lua_istable(L, -1))
			{
				lua_getfield(L, -1, "SecondLeader");
				if(lua_isfunction(L, -1))
				{
					lua_pushinteger(L, player);
					if(lua_pcall(L, 1, 1, 0) == 0 && lua_istable(L, -1))
					{
						lua_getfield(L, -1, "Civ");
						const char* c = lua_tostring(L, -1);
						lua_getfield(L, -2, "Leader");
						const char* l = lua_tostring(L, -1);
						if(c && l && *c && *l && strlen(c) < cap && strlen(l) < cap)
						{
							strcpy(civ, c);
							strcpy(leader, l);
							ok = true;
						}
					}
				}
			}
			lua_settop(L, top);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			ok = false;
		}
		return ok;
	}
}

bool McpLuaSecondLeader(int player, std::string& civType, std::string& leaderType)
{
	if(!s_gameState || s_gameRef == LUA_NOREF) return false;
	char civ[128], leader[128];
	if(!CallSecondLeader(s_gameState, s_gameRef, player, civ, leader, sizeof(civ)))
		return false;
	civType = civ;
	leaderType = leader;
	return true;
}

void McpAddGameMethods(const CivHostInfo* host, lua_State* L)
{
	s_gameState = L;
	lua_pushvalue(L, -1);
	s_gameRef = luaL_ref(L, LUA_REGISTRYINDEX);
	AddMethod(host, L, "GetLLMLog", &lGetLLMLog);
	AddMethod(host, L, "GetLLMIdentity", &lGetLLMIdentity);
	AddMethod(host, L, "LLMNotePopup", &lLLMNotePopup);
	AddMethod(host, L, "LLMTakeLeaderMessage", &lLLMTakeLeaderMessage);
	AddMethod(host, L, "LLMSetActiveMods", &lLLMSetActiveMods);
	AddMethod(host, L, "LLMDialogsToClose", &lLLMDialogsToClose);
	AddMethod(host, L, "GetLLMTeammates", &lGetLLMTeammates);
	AddMethod(host, L, "GetLLMCityStates", &lGetLLMCityStates);
	AddMethod(host, L, "LLMCityStateTalk", &lLLMCityStateTalk);
	AddMethod(host, L, "LLMChat", &lLLMChat);
	AddMethod(host, L, "LLMChatUnread", &lLLMChatUnread);
	AddMethod(host, L, "LLMChatSeen", &lLLMChatSeen);
	AddMethod(host, L, "MCChats", &lMCChats);
	AddMethod(host, L, "MCLines", &lMCLines);
	AddMethod(host, L, "MCSend", &lMCSend);
	AddMethod(host, L, "MCName", &lMCName);
	AddMethod(host, L, "MCSeen", &lMCSeen);
	AddMethod(host, L, "MCSeats", &lMCSeats);
	AddMethod(host, L, "MCChatId", &lMCChatId);
}
