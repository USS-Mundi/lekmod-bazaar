#include "CvGameCoreDLLPCH.h"
#include "McpChatGame.h"
#include "McpBridge.h"
#include "McpCityStateFiles.h"
#include "McpSeat.h"
#include "McpToolSupport.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvGlobals.h"

using namespace McpTool;

namespace
{
	const DWORD TABLE_REFRESH_MS = 500;

	// Guards the room, the game it belongs to and the table the window uses. Made when the DLL loads, before any thread
	// can use it.
	struct Section
	{
		CRITICAL_SECTION cs;
		Section() { InitializeCriticalSection(&cs); }
		~Section() { DeleteCriticalSection(&cs); }
	} s_section;

	struct Lock
	{
		Lock() { EnterCriticalSection(&s_section.cs); }
		~Lock() { LeaveCriticalSection(&s_section.cs); }
	};

	McpChatRoom s_room;
	std::string s_game;
	McpChatTable s_table; // as of the last tick, for the window
	DWORD s_tableMs = 0;

	std::string Clock()
	{
		SYSTEMTIME t;
		GetLocalTime(&t);
		char s[16];
		_snprintf_s(s, sizeof(s), _TRUNCATE, "%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
		return s;
	}

	// Game thread: the table as the game sees it now.
	McpChatTable TableNow()
	{
		McpChatTable t;
		CvGame& g = GC.getGame();
		t.turn = g.getGameTurn();
		t.clock = Clock();
		t.cityStateSeatsOn = McpCityStatesOnAtStart();
		PlayerTypes eHuman = g.getActivePlayer();
		if(eHuman != NO_PLAYER)
		{
			CvPlayer& h = GET_PLAYER(eHuman);
			t.humanName = h.getCivilizationShortDescription();
			t.humanKeys.push_back(h.getCivilizationAdjective());
			const CvCivilizationInfo* pInfo = GC.getCivilizationInfo(h.getCivilizationType());
			if(pInfo) t.humanKeys.push_back(pInfo->GetType());
		}
		for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
		{
			PlayerTypes e = (PlayerTypes)i;
			if(e == eHuman) continue;
			CvPlayer& p = GET_PLAYER(e);
			if(!p.isEverAlive() || p.isBarbarian()) continue;
			McpChatSeat s;
			s.slot = i;
			s.cityState = p.isMinorCiv();
			s.name = CivName(e);
			if(!s.cityState)
			{
				s.keys.push_back(p.getCivilizationAdjective());
				const CvCivilizationInfo* pInfo = GC.getCivilizationInfo(p.getCivilizationType());
				if(pInfo) s.keys.push_back(pInfo->GetType());
				const CvLeaderHeadInfo* pLeader = GC.getLeaderHeadInfo(p.getLeaderType());
				if(pLeader) s.keys.push_back(pLeader->GetDescription());
			}
			s.alive = p.isAlive();
			s.readable = McpIsSeat(e);
			t.seats.push_back(s);
		}
		return t;
	}

	std::string Folder(const std::string& game)
	{
		return game.empty() ? std::string() : McpGameFolder(game, "chat");
	}

	// Game thread. Under the lock: the room is this game's.
	void SameGame()
	{
		std::string game = McpGameId();
		if(game == s_game) return;
		s_game = game;
		s_room.Open(Folder(game));
	}

	std::string Display(const McpChatTable& t, const std::string& token)
	{
		if(token == "human") return "the human";
		for(size_t i = 0; i < t.seats.size(); ++i)
			if("p" + McpFormat("%d", t.seats[i].slot) == token) return t.seats[i].name;
		return token;
	}

	// Outside the lock: notes the message for each member a model plays, waking it unless one was let through for
	// it in the last two minutes. The human on screen is woken only when a model plays that civilization.
	void Wake(const McpChatTable& t, const std::vector<std::string>& wake, PlayerTypes eHuman, std::vector<std::string>& now,
		std::vector<std::string>& later)
	{
		for(size_t i = 0; i < wake.size(); ++i)
		{
			int player = -1;
			if(wake[i] == "human")
			{
				if(eHuman == NO_PLAYER || !McpBridge::Get().ScreenPlayed()) continue;
				player = (int)eHuman;
			}
			else player = atoi(wake[i].c_str() + 1);
			(McpBridge::Get().NoteMessage(player) ? now : later).push_back(Display(t, wake[i]));
		}
	}

	JArr Names(const std::vector<std::string>& v)
	{
		JArr a;
		for(size_t i = 0; i < v.size(); ++i) a.push_back(JVal(v[i]));
		return a;
	}
}

void McpChatTick()
{
	DWORD now = GetTickCount();
	std::string game = McpGameId();
	bool refresh;
	{
		Lock lock;
		if(game != s_game)
		{
			s_game = game;
			s_room.Open(Folder(game));
			s_tableMs = 0;
		}
		refresh = s_tableMs == 0 || now - s_tableMs >= TABLE_REFRESH_MS;
	}
	if(!refresh) return;
	McpChatTable t = TableNow(); // outside the lock: it reads the game, which takes locks of its own
	Lock lock;
	s_table = t;
	s_tableMs = now == 0 ? 1 : now;
}

std::string McpChatToken(int player)
{
	if(player < 0) return "";
	if((PlayerTypes)player == GC.getGame().getActivePlayer()) return "human";
	return McpFormat("p%d", player);
}

picojson::value McpChatSay(int player, const std::string& to, const std::string& words, const std::string& name, std::string& err)
{
	McpChatTable t = TableNow();
	McpChatSent sent;
	{
		Lock lock;
		SameGame();
		sent = s_room.Send(t, McpChatToken(player), to, words, name);
	}
	if(!sent.ok)
	{
		err = sent.reason;
		return JVal();
	}
	std::vector<std::string> now, later;
	Wake(t, sent.wake, GC.getGame().getActivePlayer(), now, later);
	JObj o;
	o["chat"] = JVal(sent.label);
	o["to"] = JVal(sent.replyTo);
	o["woken"] = JVal(Names(now));
	if(!later.empty()) o["wakes_later"] = JVal(Names(later));
	return JVal(o);
}

void McpChatAddMessages(int player, picojson::object& status)
{
	McpChatTable t = TableNow();
	std::vector<McpChatDelivery> got;
	int more = 0;
	{
		Lock lock;
		SameGame();
		got = s_room.Take(t, McpChatToken(player), MCP_CHAT_MAX_DELIVERED, more);
	}
	if(got.empty()) return;
	JArr messages;
	for(size_t i = 0; i < got.size(); ++i)
	{
		JObj m;
		m["chat"] = JVal(got[i].chat);
		m["reply_to"] = JVal(got[i].replyTo);
		m["from"] = JVal(got[i].from);
		m["turn"] = J(got[i].turn);
		m["words"] = JVal(got[i].words);
		messages.push_back(JVal(m));
	}
	status["messages"] = JVal(messages);
	if(more > 0) status["more_messages"] = J(more);
}

std::vector<McpChatInfo> McpChatHumanChats()
{
	Lock lock;
	return s_room.ChatsOf(s_table, "human");
}

bool McpChatHumanLines(const std::string& id, int from, std::vector<McpChatLine>& out)
{
	Lock lock;
	return s_room.Lines("human", id, from, out);
}

bool McpChatHumanSend(const std::vector<std::string>& to, const std::string& text, std::string& id, std::string& err)
{
	McpChatTable t;
	McpChatSent sent;
	std::string joined;
	for(size_t i = 0; i < to.size(); ++i) joined += (i ? "," : "") + to[i];
	{
		Lock lock;
		t = s_table;
		t.clock = Clock();
		if(s_game.empty())
		{
			err = "no game is loaded";
			return false;
		}
		sent = s_room.Send(t, "human", joined, text);
		if(sent.ok) s_room.Seen("human", sent.id);
	}
	if(!sent.ok)
	{
		err = sent.reason;
		return false;
	}
	id = sent.id;
	std::vector<std::string> now, later;
	Wake(t, sent.wake, NO_PLAYER, now, later); // the human is the sender
	return true;
}

bool McpChatHumanName(const std::string& id, const std::string& name)
{
	Lock lock;
	return s_room.Name("human", id, name);
}

bool McpChatHumanSeen(const std::string& id)
{
	Lock lock;
	return s_room.Seen("human", id);
}

std::vector<std::pair<std::string, std::string> > McpChatSeats()
{
	std::vector<std::pair<std::string, std::string> > out;
	Lock lock;
	for(size_t i = 0; i < s_table.seats.size(); ++i)
	{
		const McpChatSeat& s = s_table.seats[i];
		if(!s.readable || !s.alive || (s.cityState && !s_table.cityStateSeatsOn)) continue;
		out.push_back(std::make_pair(McpFormat("p%d", s.slot), s.name));
	}
	return out;
}

bool McpChatCityStateSeatsOn()
{
	return McpCityStatesOnAtStart();
}
