// The Message Center's chats: who may write to whom, who reads what, and the files under MCP\<game>\chat\.
// No game headers, so it is tested natively (tests/run_chat_room_test.sh). It has no lock of its own: McpChatGame locks
// around every call, since the window calls in from the UI thread. Names and paths are UTF-8.
//
// A member is "human" (the civilization on screen) or "p<slot>" (another player). A chat is its member set: its id is
// the tokens sorted and joined by "+", so the same members always share one chat. "all" is the whole table.
// "human-ai", a model playing the human's own civilization, reads and writes as the human's member but speaks as
// itself: its lines show as its own, and it is handed the human's.
#pragma once

#include <map>
#include <string>
#include <vector>

static const int MCP_CHAT_MAX_MEMBERS = 8;
static const int MCP_CHAT_STALE_TURNS = 10;
static const int MCP_CHAT_MAX_DELIVERED = 40;
static const size_t MCP_CHAT_MAX_TEXT = 2000;

// One player at the table other than the civilization on screen, as the game sees it on this call.
struct McpChatSeat
{
	int slot;
	std::string name;              // how the window and agents name it: "Rome", or a city-state's own name
	std::vector<std::string> keys; // what `to` may say besides the name: adjective, CIVILIZATION_ type
	bool cityState;
	bool readable; // a model plays it, or will take it back: someone reads its lines
	bool alive;
	McpChatSeat() : slot(-1), cityState(false), readable(false), alive(true) {}
};

struct McpChatTable
{
	int turn;
	std::string clock;                  // "HH:MM:SS", stamped on lines
	bool cityStateSeatsOn;              // city-states take part only while city-state seats are on
	std::string humanName;              // the civilization on screen, which "human" also answers to
	std::vector<std::string> humanKeys;
	std::vector<McpChatSeat> seats;     // every other living player the game knows, bots included
	McpChatTable() : turn(0), cityStateSeatsOn(false) {}
};

struct McpChatLine
{
	std::string time;
	int turn; // -1 when the line carries none
	std::string from;
	std::string text;
	McpChatLine() : turn(-1) {}
};

struct McpChatInfo
{
	std::string id;
	std::vector<std::string> members;
	std::string name;
	std::string startedBy;
	int startedTurn;
	int lines;
	int unread; // lines from others the viewer has not marked seen
	int lastTurn;
	McpChatLine last;
	bool stale;
	McpChatInfo() : startedTurn(-1), lines(0), unread(0), lastTurn(-1), stale(false) {}
};

struct McpChatSent
{
	bool ok;
	std::string reason;            // when refused: one sentence the sender can act on; nothing was written
	std::string id;
	std::string label;             // the chat as the sender sees it: "Eastern Front (you, Maurya, Turkey)"
	std::string replyTo;           // the `to` that speaks in this chat again
	std::vector<std::string> wake; // members to wake: everyone but the sender who reads lines ("human" included)
	McpChatSent() : ok(false) {}
};

struct McpChatDelivery
{
	std::string chat;    // as the reader sees it
	std::string replyTo; // the `to` that answers in the same chat
	std::string from;    // "Maurya", or "the human"
	int turn;
	std::string words;
	McpChatDelivery() : turn(-1) {}
};

// Whether an early wake may go through now: at most one per civilization every cooldownMs. Times are GetTickCount
// values, so they may wrap.
bool McpWakeLetThrough(bool everLetThrough, unsigned long lastMs, unsigned long nowMs, unsigned long cooldownMs);

class McpChatRoom
{
public:
	McpChatRoom();

	// The folder holding this game's chats (MCP\<game id>\chat). Loads every chat once; an empty folder opens nothing.
	void Open(const std::string& folder);
	const std::string& Folder() const { return m_folder; }

	McpChatSent Send(const McpChatTable& table, const std::string& from, const std::string& to, const std::string& text,
		const std::string& name = "");

	// The chats a viewer may see: the whole table and every chat with the viewer in it.
	std::vector<McpChatInfo> ChatsOf(const McpChatTable& table, const std::string& viewer) const;
	// Lines from..end of a chat the viewer is in; false for any other chat.
	bool Lines(const std::string& viewer, const std::string& id, int from, std::vector<McpChatLine>& out) const;
	// The first name a group is given stays; false when the chat is not the viewer's, or not a group.
	bool Name(const std::string& viewer, const std::string& id, const std::string& name);
	bool Seen(const std::string& viewer, const std::string& id);
	// The reader's lines not yet handed to it, oldest first, at most max; more = how many still wait. Moves its cursor.
	std::vector<McpChatDelivery> Take(const McpChatTable& table, const std::string& reader, int max, int& more);
	// Whether the reader has lines not yet handed to it.
	bool Pending(const McpChatTable& table, const std::string& reader) const;

	// The id of the chat of these members; the sender is added. "" when a token is not one.
	static std::string IdOf(const std::string& sender, const std::vector<std::string>& members);

private:
	struct Chat
	{
		std::vector<std::string> members;
		std::string name;
		std::string startedBy;
		int startedTurn;
		std::vector<McpChatLine> lines;
		std::map<std::string, int> seen;      // per member: lines marked seen
		std::map<std::string, int> delivered; // per member: lines handed over
		Chat() : startedTurn(-1) {}
	};
	std::string m_folder;
	std::map<std::string, Chat> m_chats;

	bool MayRead(const std::string& reader, const std::string& id, const McpChatTable* table) const;
	std::string Label(const McpChatTable& table, const std::string& id, const Chat& chat, const std::string& reader) const;
	std::string ReplyTo(const McpChatTable& table, const std::string& id, const Chat& chat, const std::string& reader) const;
	bool SaveCursor(const char* kind, const std::string& who, const std::string& id, int value);
};
