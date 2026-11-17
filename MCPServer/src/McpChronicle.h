#pragma once
// A seat's chronicle: the story of its reign as it happens. The game files what was done (deeds) under the players
// involved; the seat keeps its relations entries, its threads and vows, and closes a chapter when its reign turns.
// This module holds the state, says what is due, and refuses a plan_turn that leaves the story unwritten. It never
// judges what is written. No game headers, so it is tested natively (tests/run_chronicle_test.sh).
//
// Every change is one event line (JSON). The lines are the file (chronicle/<civ>.jsonl): a load replays them.
#include "picojson.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct McpDeed { int turn; std::string with, text; };

struct McpRelation
{
	std::string text;
	int written, until;
	size_t deedsSeen;      // how many deeds with this player the ledger held when the entry was written
	int unserved;          // rounds running in which no order and no say served it
	McpRelation() : written(-1), until(-1), deedsSeen(0), unserved(0) {}
};

struct McpThread
{
	int id, opened, ended;                   // ended: the turn its ending was written, -1 while open
	std::string with, words, ending;
	std::string vow, subject, vowState;      // vow: "" or war | peace | pact | deal | take | hold; vowState: "" | kept | broken | withdrawn
	int by, vowTurn;                         // the turn the vow holds until or is due by; the turn it was resolved
	int chapters, unserved;                  // chapter closes it was carried through; rounds running unserved
	McpThread() : id(0), opened(-1), ended(-1), by(-1), vowTurn(-1), chapters(0), unserved(0) {}
};

struct McpChapter
{
	int opened, closed;
	std::string reason, title, became, direction;
	std::vector<std::string> ruled;          // per fault line, in order: first | second | both | neither
	std::vector<std::pair<std::string, std::string> > said;
	McpChapter() : opened(-1), closed(-1) {}
};

struct McpChronicle
{
	bool started;
	int opened;                              // the turn the open chapter began
	int era, eliminations, citiesLost;       // as they stood when the open chapter began
	std::vector<std::string> faults;         // the first page's fault lines, whole
	std::vector<std::string> unwritten;      // the first page's unwritten lines, whole
	std::vector<std::string> settled;        // what the seat wrote when a chapter settled an unwritten line
	std::vector<McpDeed> deeds;
	std::map<std::string, McpRelation> relations;
	std::vector<McpThread> threads;
	std::vector<McpChapter> chapters;
	std::set<std::string> gone;              // players that left the game: their entries are owed nothing more
	std::vector<std::string> log;            // event lines not yet written to the file
	std::vector<std::string> journal;        // plan journal rows not yet written (vows settled)
	McpChronicle() : started(false), opened(0), era(-1), eliminations(0), citiesLost(0) {}
};

// The file's text back into a state. Lines it cannot read are skipped.
void McpChronicleLoad(McpChronicle& c, const std::string& fileText);
// Begins the chronicle at this turn: the first chapter opens here. Does nothing once started.
void McpChronicleStart(McpChronicle& c, int turn, int era, int eliminations, int citiesLost,
	const std::vector<std::string>& faults, const std::vector<std::string>& unwritten);
// Files something the game says happened under a player, in the seat's words for it ("declared war on Madagascar").
void McpChronicleDeed(McpChronicle& c, int turn, const std::string& with, const std::string& text);
// A player left the game: its entry no longer goes stale and no longer expires.
void McpChronicleForget(McpChronicle& c, const std::string& civ);

// Why a chapter is due, or "" when none is: the seat's era changed, a civilization left the game, one of the seat's
// cities was taken, or 40 turns passed since the chapter opened.
std::string McpChronicleDue(const McpChronicle& c, int era, int eliminations, int citiesLost, int turn);

struct McpChronicleNow
{
	int turn, era, eliminations, citiesLost;
	std::string direction;                   // the plan's standing direction
	std::vector<std::string> players;        // the players an entry or a thread may name; empty: any name is taken
	McpChronicleNow() : turn(0), era(-1), eliminations(0), citiesLost(0) {}
};

struct McpChronicleResult
{
	std::vector<std::string> refusals;       // every reason this plan_turn is refused; when any, nothing was applied
	bool chapterClosed;
	std::string direction;                   // the closed chapter's direction: it replaces the plan's
	std::vector<std::pair<std::string, std::string> > say;   // the chapter's lines to send: to, words
	std::vector<std::string> journal;        // rows for the plan journal (JSON objects without game and civ)
	McpChronicleResult() : chapterClosed(false) {}
};

// plan_turn's arguments against the chronicle: plan.relations {civ: {text, until, why}}, threads {open: [{with, words,
// vow: {kind, subject, by}}], end: [{id, ending}]}, and chapter {title, became, threads: [{id, state, words}],
// nature: [{fault, ruled} | {unwritten, settled}], direction, said: [{to, words}]}. Refused, all reasons at once:
// a chapter due and missing; a chapter direction identical to the standing one; a relations entry stale or expired
// and not re-sent with a why; a thread ended with no ending; and arguments that cannot be read. True when applied.
bool McpChronicleApply(McpChronicle& c, const picojson::object& args, const McpChronicleNow& now, McpChronicleResult& out);

// What the game says of an open vow now: met (the thing vowed stands: at war, pact signed, deal made, city held by
// the seat) and failed (a peace the seat itself broke, a city to hold that was lost).
struct McpVowFact { bool met, failed; McpVowFact() : met(false), failed(false) {} };
// Settles open vows at the start of a turn. war, pact, deal and take are kept the moment they are met and broken once
// their turn passes unmet; peace and hold are broken the moment they fail and kept once their turn is reached. Each
// result is filed as the seat's own deed under the thread's player.
void McpVowsResolve(McpChronicle& c, const std::map<int, McpVowFact>& factsByThread, int turn);
// A thread ended while its vow is still open withdraws the vow, and that is filed as a deed too.

// After a plan ran: the players this round's orders and says served. Returns the entries and open threads nothing
// served, each with how many rounds running. Reports only.
std::vector<std::string> McpChronicleMirror(McpChronicle& c, const std::vector<std::string>& served, int turn);

// The CHRONICLE block for the turn view. due is McpChronicleDue's answer.
std::string McpChronicleBlock(const McpChronicle& c, const std::string& civ, const std::string& firstPage, int turn, const std::string& due);
// The whole ledger, every chapter and every thread, for the chronicle tool.
picojson::value McpChronicleFull(const McpChronicle& c);
// The latest "who I have become", or "".
std::string McpChronicleBecame(const McpChronicle& c);
// The entries as the plan's relations text, one after another: "Goths (holds until T40): A wolf at my door."
std::string McpChronicleRelationsText(const McpChronicle& c);
