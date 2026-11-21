#pragma once
// The leader seed and the chronicle against the running game: deals each major seat's first page once per game from
// its leaders' rolled sheets, keeps each seat's chronicle with the game's files (seeds/ and chronicle/ beside plans/),
// files what the game says happened, and stands between plan_turn and the chronicle's refusals. The rules themselves
// are in McpSeedCompose, McpChronicle and McpWorldDeeds, which need no game.
//
// Off while Logs\MCPServer-chronicle.off exists under the game's My Games folder: no page, no block, no refusal.
#include "picojson.h"
#include <string>
#include <utility>
#include <vector>

class CvPlayer;

// Whether this seat has a first page and a chronicle: a major civilization's seat, with the switch on.
bool McpChronicleOn(CvPlayer* me);
// The seat's first page, dealt on first ask and kept for the game; "" for a seat that has none.
std::string McpWhoYouAre(CvPlayer* me);
// Files what happened since the seat was last brought up to date and settles its vows. Called before the seat reads
// or writes its chronicle.
void McpChronicleSync(CvPlayer* me);

struct McpChroniclePlan
{
	std::vector<std::string> refusals;                           // plan_turn is refused for all of these at once
	bool relationsGiven, chapterClosed;
	std::string relationsText, direction, why;                   // for the plan: its relations heading, a closed chapter's direction, and why when the seat gave none
	std::vector<std::pair<std::string, std::string> > say;       // a closed chapter's lines: to, words
	McpChroniclePlan() : relationsGiven(false), chapterClosed(false) {}
};
// plan_turn's arguments against the seat's chronicle, on a copy: nothing is kept until McpChronicleCommit. direction
// is the plan's standing one. False when refused.
bool McpChroniclePlanTurn(CvPlayer* me, const picojson::object& args, const std::string& direction, McpChroniclePlan& out);
// Keeps what the last accepted McpChroniclePlanTurn for this seat changed, and queues its lines to say.
void McpChronicleCommit(CvPlayer* me);
// The CHRONICLE block for the turn view, "" for a seat without one.
std::string McpChronicleViewBlock(CvPlayer* me);
// After a plan's orders ran: which entries and threads no order and no say served. serves: each order's serves text;
// said: who each say order went to.
std::vector<std::string> McpChronicleMirrorFor(CvPlayer* me, const std::vector<std::string>& serves, const std::vector<std::string>& said);
// Says one line for the seat in the Message Center, as the say tool does; a refused line goes to its log file.
void McpSayQueue(CvPlayer* me, const std::string& to, const std::string& words);
// A war notification the game raised: who declared on which team. The chronicle's deeds name the declarer from it.
void McpChronicleNoteWar(int declaringPlayer, int onTeam);
// The whole chronicle for the chronicle tool: the ledger, the entries, every thread and every chapter.
picojson::value McpChronicleToolView(CvPlayer* me);
void McpChronicleReset();
