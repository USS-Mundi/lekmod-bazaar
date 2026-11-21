// Native tests for the Message Center's chats (McpChatRoom), through its interface only.
#include "McpChatRoom.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <vector>

static int g_failed = 0;
static void Check(bool ok, const char* what)
{
	if(!ok) { std::printf("FAIL: %s\n", what); ++g_failed; }
}

static std::string g_root;
static int g_folders = 0;

// A fresh chat folder for each test, under a path with a space and an apostrophe, as the game's own has.
static std::string NewFolder()
{
	char n[32];
	std::snprintf(n, sizeof(n), "/game-%d/chat", ++g_folders);
	return g_root + "/Sid Meier's Civilization 5/MCP" + n;
}

static McpChatSeat Seat(int slot, const char* name, const char* adjective, bool readable, bool cityState = false, bool alive = true)
{
	McpChatSeat s;
	s.slot = slot;
	s.name = name;
	s.keys.push_back(adjective);
	s.readable = readable;
	s.cityState = cityState;
	s.alive = alive;
	return s;
}

// Rome is on screen. Greece, Maurya and Turkey are played by models, Persia by the game's AI, Carthage is gone.
// Kuala Lumpur is a city-state a model plays, Dublin one the game plays.
static McpChatTable Table(bool cityStateSeats = false)
{
	McpChatTable t;
	t.turn = 5;
	t.clock = "21:14:03";
	t.cityStateSeatsOn = cityStateSeats;
	t.humanName = "Rome";
	t.humanKeys.push_back("Roman");
	t.humanKeys.push_back("CIVILIZATION_ROME");
	t.seats.push_back(Seat(1, "Greece", "Greek", true));
	t.seats.push_back(Seat(2, "Maurya", "Indian", true));
	t.seats.push_back(Seat(3, "Turkey", "Ottoman", true));
	t.seats.push_back(Seat(4, "Persia", "Persian", false));
	t.seats.push_back(Seat(5, "Carthage", "Carthaginian", true, false, false));
	t.seats.push_back(Seat(23, "Kuala Lumpur", "Kuala Lumpur", true, true));
	t.seats.push_back(Seat(24, "Dublin", "Dublin", false, true));
	return t;
}

static bool Has(const std::vector<std::string>& v, const std::string& s)
{
	for(size_t i = 0; i < v.size(); ++i) if(v[i] == s) return true;
	return false;
}

static bool Lists(const std::vector<McpChatInfo>& chats, const std::string& id)
{
	for(size_t i = 0; i < chats.size(); ++i) if(chats[i].id == id) return true;
	return false;
}

// The chat listed under id, or one with an empty id when it is not listed.
static McpChatInfo Find(const std::vector<McpChatInfo>& chats, const std::string& id)
{
	for(size_t i = 0; i < chats.size(); ++i) if(chats[i].id == id) return chats[i];
	return McpChatInfo();
}

static void TestChatIsItsMemberSet()
{
	McpChatRoom room;
	room.Open(NewFolder());
	McpChatTable t = Table();
	McpChatSent a = room.Send(t, "p1", "Maurya, human", "Hold the pass.");
	Check(a.ok, "a model writes to a civilization and the human");
	Check(a.id == "human+p1+p2", "the chat's id is its members sorted and joined by +");
	McpChatSent b = room.Send(t, "human", "greece,MAURYA", "Count me in.");
	Check(b.ok && b.id == a.id, "the same members share one chat whoever writes first, names matched in any case");
	McpChatSent c = room.Send(t, "p3", "p2, Indian, maurya", "Duplicates collapse.");
	Check(c.ok && c.id == "p2+p3", "slot tokens, adjectives and names name the same member once");
	McpChatSent d = room.Send(t, "p2", "Roman", "The human's adjective is the human.");
	Check(d.ok && d.id == "human+p2", "the civilization on screen answers to its names as the human");
	std::vector<McpChatLine> lines;
	Check(room.Lines("human", a.id, 0, lines) && lines.size() == 2, "both lines are in the one chat");
	Check(lines.size() == 2 && lines[0].from == "p1" && lines[1].from == "human" && lines[1].turn == 5 && lines[1].time == "21:14:03",
		"each line keeps who wrote it, the turn and the time");
	Check(McpChatRoom::IdOf("human", std::vector<std::string>(1, "p2")) == "human+p2", "IdOf adds the sender");
	std::vector<std::string> bad(1, "../etc");
	Check(McpChatRoom::IdOf("human", bad) == "", "IdOf refuses what is not a member token");
}

static void TestRefusals()
{
	std::string folder = NewFolder();
	McpChatRoom room;
	room.Open(folder);
	McpChatTable t = Table();
	McpChatSent r = room.Send(t, "p1", "Babylonia", "Hello?");
	Check(!r.ok && r.reason.find("Babylonia") != std::string::npos && r.reason.find("Maurya") != std::string::npos,
		"an unknown name is refused, naming who can be written to");
	r = room.Send(t, "p1", "Greece", "Me, myself.");
	Check(!r.ok && r.reason.find("yourself") != std::string::npos, "writing only to yourself is refused");
	r = room.Send(t, "p1", "all, Maurya", "Both.");
	Check(!r.ok && r.reason.find("all") != std::string::npos, "all cannot be combined with other names");
	r = room.Send(t, "p1", "Persia", "Hello Persia.");
	Check(!r.ok && r.reason.find("game's own AI") != std::string::npos && r.reason.find("diplomacy") != std::string::npos,
		"a civilization the game's AI plays is refused, pointing at diplomacy");
	r = room.Send(t, "p1", "Carthage", "Are you there?");
	Check(!r.ok && r.reason.find("gone") != std::string::npos, "a civilization no longer in the game is refused");
	r = room.Send(t, "p1", "Maurya", "   ");
	Check(!r.ok, "an empty line is refused");
	r = room.Send(t, "p1", "Maurya", std::string(MCP_CHAT_MAX_TEXT + 1, 'x'));
	Check(!r.ok && r.reason.find("2000") != std::string::npos, "a line over 2000 characters is refused");
	r = room.Send(t, "p1", "Kuala Lumpur, Maurya", "Join us.");
	Check(!r.ok && r.reason.find("not seated") != std::string::npos, "city-states take no part while their seats are off");

	McpChatTable big = Table();
	const char* names[] = { "A", "B", "C", "D", "E", "F", "G", "H" };
	std::string to;
	for(int i = 0; i < 8; ++i)
	{
		big.seats.push_back(Seat(10 + i, names[i], names[i], true));
		to += std::string(i ? "," : "") + names[i];
	}
	r = room.Send(big, "p1", to, "Nine of us.");
	Check(!r.ok && r.reason.find("8") != std::string::npos, "a chat holds at most 8, the sender included");
	Check(room.Send(big, "p1", "A,B,C,D,E,F,G", "Eight of us.").ok, "eight members, the sender included, is allowed");

	McpChatTable cs = Table(true);
	r = room.Send(cs, "p1", "Kuala Lumpur", "Just us two.");
	Check(!r.ok && r.reason.find("group") != std::string::npos, "a chat of two with a city-state is refused");
	r = room.Send(cs, "p23", "all", "Hear me.");
	Check(!r.ok, "a city-state cannot write to the whole table");
	r = room.Send(cs, "p1", "Dublin, Maurya", "Dublin too.");
	Check(!r.ok && r.reason.find("game's own AI") != std::string::npos, "a city-state no model plays is refused like a bot");
	Check(room.Send(cs, "p1", "Kuala Lumpur, Maurya", "A group of three.").ok, "a city-state a model plays joins a group of three");

	McpChatRoom again;
	again.Open(folder);
	Check(again.ChatsOf(Table(), "human").empty(), "nothing a refusal touched reached the files");
}

static void TestPrivacy()
{
	McpChatRoom room;
	room.Open(NewFolder());
	McpChatTable t = Table();
	McpChatSent secret = room.Send(t, "p1", "Maurya", "The human must not know.");
	McpChatSent open = room.Send(t, "p1", "human", "Hello, human.");
	room.Send(t, "human", "all", "Welcome, all.");
	std::vector<McpChatInfo> human = room.ChatsOf(t, "human");
	Check(!Lists(human, secret.id), "the human never lists the agents' private chat");
	Check(Lists(human, open.id) && Lists(human, "all"), "the human lists his chats and the whole table");
	std::vector<McpChatLine> lines;
	Check(!room.Lines("human", secret.id, 0, lines), "the human cannot open the agents' private chat");
	Check(!room.Lines("p3", secret.id, 0, lines), "another agent cannot open it either");
	Check(room.Lines("p2", secret.id, 0, lines) && lines.size() == 1, "a member opens it");
	int more = 0;
	std::vector<McpChatDelivery> turkey = room.Take(t, "p3", MCP_CHAT_MAX_DELIVERED, more);
	Check(turkey.size() == 1 && turkey[0].words == "Welcome, all.", "a third agent is handed only the whole table's line");
	McpChatTable cs = Table(true);
	room.Send(cs, "p1", "Kuala Lumpur, Maurya", "A group with a city-state.");
	std::vector<McpChatDelivery> kl = room.Take(cs, "p23", MCP_CHAT_MAX_DELIVERED, more);
	Check(kl.size() == 1 && kl[0].words == "A group with a city-state.", "a city-state is handed its groups only, never the whole table");
	Check(!Lists(room.ChatsOf(cs, "p23"), "all"), "a city-state does not list the whole table");
}

static void TestWakes()
{
	McpChatRoom room;
	room.Open(NewFolder());
	McpChatTable t = Table();
	McpChatSent s = room.Send(t, "p1", "Maurya, Turkey", "Close the strait.");
	Check(s.wake.size() == 2 && Has(s.wake, "p2") && Has(s.wake, "p3"), "a line wakes every member but the sender");
	s = room.Send(t, "human", "all", "Everyone.");
	Check(s.wake.size() == 3 && Has(s.wake, "p1") && Has(s.wake, "p2") && Has(s.wake, "p3"),
		"the whole table wakes every model-played civilization, never a bot, the dead or the sender");
	s = room.Send(t, "p1", "human", "Only you.");
	Check(s.wake.size() == 1 && s.wake[0] == "human", "a line to the human names the human (the adapter decides whether a model plays it)");

	Check(McpWakeLetThrough(false, 0, 1000, 120000), "the first wake goes through");
	Check(!McpWakeLetThrough(true, 1000, 60000, 120000), "a second wake within two minutes is held back");
	Check(McpWakeLetThrough(true, 1000, 121000, 120000), "a wake two minutes later goes through");
	Check(!McpWakeLetThrough(true, 0xFFFFFF00UL, 1000, 120000), "the clock wrapping round does not let one through early");
}

static void TestDelivery()
{
	std::string folder = NewFolder();
	McpChatTable t = Table();
	{
		McpChatRoom room;
		room.Open(folder);
		McpChatSent s = room.Send(t, "p1", "Maurya, human", "Turkey moves on Sinope.", "Eastern Front");
		Check(s.ok && s.label == "Eastern Front (you, the human, Maurya)", "the sender's label names the group and its members");
		room.Send(t, "p2", "Greece, human", "I hold the river.");
		int more = -1;
		Check(room.Pending(t, "p2"), "Maurya has a line waiting");
		std::vector<McpChatDelivery> got = room.Take(t, "p2", MCP_CHAT_MAX_DELIVERED, more);
		Check(got.size() == 1 && more == 0, "a reader is handed the others' lines, never its own");
		Check(got.size() == 1 && got[0].from == "Greece" && got[0].turn == 5 && got[0].words == "Turkey moves on Sinope.",
			"a line arrives with who wrote it, the turn and the words");
		Check(got.size() == 1 && got[0].chat == "Eastern Front (you, the human, Greece)", "it names the chat as the reader sees it");
		Check(got.size() == 1 && got[0].replyTo == "human, Greece", "reply_to is the to that answers in the same chat");
		Check(room.Take(t, "p2", MCP_CHAT_MAX_DELIVERED, more).empty() && !room.Pending(t, "p2"), "each line is handed over once");
		McpChatSent back = room.Send(t, "p2", got[0].replyTo, "Agreed.");
		Check(back.ok && back.id == s.id, "answering with reply_to lands in the same chat");
	}
	McpChatRoom reopened;
	reopened.Open(folder);
	int more = 0;
	Check(reopened.Take(t, "p2", MCP_CHAT_MAX_DELIVERED, more).empty(), "a reload hands nothing over again");
	std::vector<McpChatDelivery> greece = reopened.Take(t, "p1", MCP_CHAT_MAX_DELIVERED, more);
	Check(greece.size() == 2, "a seat whose agent was away gets everything written to it since");
	reopened.Send(t, "human", "Maurya, Greece", "And after the reload?");
	std::vector<McpChatDelivery> got = reopened.Take(t, "p2", MCP_CHAT_MAX_DELIVERED, more);
	Check(got.size() == 1 && got[0].from == "the human", "a line written after a reload is handed over");

	McpChatRoom room;
	room.Open(NewFolder());
	for(int i = 0; i < 45; ++i) room.Send(t, "p1", "Maurya", "line");
	std::vector<McpChatDelivery> first = room.Take(t, "p2", MCP_CHAT_MAX_DELIVERED, more);
	Check(first.size() == 40 && more == 5, "at most 40 are handed over at once, and the rest are counted");
	std::vector<McpChatDelivery> rest = room.Take(t, "p2", MCP_CHAT_MAX_DELIVERED, more);
	Check(rest.size() == 5 && more == 0, "the rest come next time");
}

static void TestDeliveryOrderAcrossChats()
{
	McpChatRoom room;
	room.Open(NewFolder());
	McpChatTable t = Table();
	t.turn = 7;
	room.Send(t, "p1", "Maurya", "second");
	t.turn = 6;
	room.Send(t, "p3", "Maurya", "first");
	t.turn = 7;
	room.Send(t, "p1", "Maurya", "third");
	int more = 0;
	std::vector<McpChatDelivery> got = room.Take(t, "p2", 2, more);
	Check(got.size() == 2 && got[0].words == "first" && got[1].words == "second" && more == 1,
		"lines from several chats come oldest first");
	got = room.Take(t, "p2", 2, more);
	Check(got.size() == 1 && got[0].words == "third", "and the one left comes next");
}

static void TestUnreadNamesAndStale()
{
	std::string folder = NewFolder();
	McpChatTable t = Table();
	McpChatRoom room;
	room.Open(folder);
	McpChatSent s = room.Send(t, "p1", "human, Maurya", "One.");
	room.Send(t, "p2", "human, Greece", "Two.");
	room.Send(t, "human", "Greece, Maurya", "Mine.");
	McpChatInfo c = Find(room.ChatsOf(t, "human"), s.id);
	Check(c.id == s.id && c.unread == 2 && c.lines == 3, "the human's unread counts the others' lines");
	Check(c.id == s.id && c.startedBy == "p1" && c.startedTurn == 5, "a chat records who started it, and when");
	Check(c.id == s.id && c.last.text == "Mine." && c.lastTurn == 5, "and its last line");
	Check(room.Seen("human", s.id), "the human marks a chat seen");
	c = Find(room.ChatsOf(t, "human"), s.id);
	Check(c.id == s.id && c.unread == 0, "nothing is unread after that");
	Check(!room.Seen("human", "p1+p2"), "the human cannot mark a chat he is not in");

	Check(!room.Name("human", "human+p1", "Pair"), "a chat of two is never named");
	Check(room.Name("human", s.id, "Eastern Front"), "a group is named");
	Check(!room.Name("p2", s.id, "Western Front"), "a second name is ignored");
	McpChatRoom reopened;
	reopened.Open(folder);
	c = Find(reopened.ChatsOf(t, "human"), s.id);
	Check(c.id == s.id && c.name == "Eastern Front" && c.unread == 0, "the name and what was seen survive a reload");

	t.turn = 5 + MCP_CHAT_STALE_TURNS - 1;
	c = Find(reopened.ChatsOf(t, "human"), s.id);
	Check(c.id == s.id && !c.stale, "a chat quiet for fewer than 10 turns is live");
	t.turn = 5 + MCP_CHAT_STALE_TURNS;
	c = Find(reopened.ChatsOf(t, "human"), s.id);
	Check(c.id == s.id && c.stale, "a chat quiet for 10 turns is stale");
	reopened.Send(t, "p1", "human, Maurya", "Awake again.");
	c = Find(reopened.ChatsOf(t, "human"), s.id);
	Check(c.id == s.id && !c.stale, "writing to it makes it live again");
}

static void TestLinesAreOneLine()
{
	McpChatRoom room;
	std::string folder = NewFolder();
	room.Open(folder);
	McpChatTable t = Table();
	McpChatSent s = room.Send(t, "p1", "Maurya", "first\nsecond\r\nthird");
	McpChatRoom reopened;
	reopened.Open(folder);
	std::vector<McpChatLine> lines;
	Check(reopened.Lines("p2", s.id, 0, lines) && lines.size() == 1 && lines[0].text == "first second third",
		"a line with breaks in it stays one line");
	Check(reopened.Lines("p2", s.id, 1, lines) && lines.empty(), "Lines starts where it is asked to");
}

int main()
{
	char tmpl[] = "/tmp/chat_room_test.XXXXXX";
	char* dir = mkdtemp(tmpl);
	if(!dir) { std::printf("no temp folder\n"); return 1; }
	g_root = dir;
	TestChatIsItsMemberSet();
	TestRefusals();
	TestPrivacy();
	TestWakes();
	TestDelivery();
	TestDeliveryOrderAcrossChats();
	TestUnreadNamesAndStale();
	TestLinesAreOneLine();
	std::string rm = "rm -rf '" + g_root + "'";
	if(std::system(rm.c_str()) != 0) std::printf("could not remove %s\n", g_root.c_str());
	if(g_failed) { std::printf("%d failed\n", g_failed); return 1; }
	std::printf("chat room: all passed\n");
	return 0;
}
