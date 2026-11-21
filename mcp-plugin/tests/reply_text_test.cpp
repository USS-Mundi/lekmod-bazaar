// Native tests for how a tool's reply becomes the text the model reads: tests/run_reply_text_test.sh
#include "McpReplyText.h"
#include "picojson.h"
#include <cstdio>
#include <string>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

int main()
{
	// An instruction leads, on its own line, and leaves the rest of the reply.
	picojson::object o;
	o["turn"] = picojson::value(30.0);
	o["alive"] = picojson::value(true);
	o[MCP_DO_NOW] = picojson::value(std::string("call turn_view, before any other tool."));
	std::string text = McpReplyText(picojson::value(o));
	CHECK(text.find("DO NOW: call turn_view, before any other tool.\n\n{") == 0);
	CHECK(text.find(MCP_DO_NOW) == std::string::npos);
	CHECK(text.find("\"turn\":30") != std::string::npos);

	// Without one, the reply is its JSON as before.
	picojson::object plain;
	plain["ok"] = picojson::value(true);
	CHECK(McpReplyText(picojson::value(plain)) == "{\"ok\":true}");

	// A plain-text reply stays as it is.
	CHECK(McpReplyText(picojson::value(std::string("it is not your turn right now"))) == "it is not your turn right now");

	// A message for the seat leads whatever the reply already asked for, and stands alone when it asked for nothing.
	picojson::object woke;
	woke["turn"] = picojson::value(30.0);
	McpAddMessageNotice(woke);
	CHECK(woke[MCP_DO_NOW].to_str() == "a player wrote to you: the lines are in messages. Answer with say if you choose, then call wait_for_my_turn.");
	CHECK(woke["woken_by_message"].get<bool>());
	picojson::object round;
	round[MCP_DO_NOW] = picojson::value(std::string("call turn_view, then send this turn's orders with plan_turn."));
	McpAddMessageNotice(round);
	CHECK(round[MCP_DO_NOW].to_str() == "a player wrote to you: the lines are in messages. Answer with say if you choose. Then call turn_view, then send this turn's orders with plan_turn.");

	if(s_failures) { std::printf("%d failure(s)\n", s_failures); return 1; }
	std::printf("reply text: all passed\n");
	return 0;
}
