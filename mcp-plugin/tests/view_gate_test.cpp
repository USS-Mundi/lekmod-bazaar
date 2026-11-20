// Native tests for the turn view gate: tests/run_view_gate_test.sh
#include "McpViewGate.h"
#include <cstdio>
#include <string>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

int main()
{
	McpViewGateReset();

	// Before the view, on its own turn, a seat can do nothing but open it (and wait, read its status, name itself).
	const char* refused[] = { "move_unit", "end_turn", "run_plan", "briefing", "units", "map_image", "plan_turn", "set_production", "chat" };
	for(size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
		CHECK(McpViewGateRefusal(3, 5, true, refused[i]).find("DO NOW: call turn_view") == 0);
	const char* open[] = { "turn_view", "wait_for_my_turn", "status", "set_identity", "take_seat", "release_seat", "testing_mode" };
	for(size_t i = 0; i < sizeof(open) / sizeof(open[0]); ++i)
		CHECK(McpViewGateRefusal(3, 5, true, open[i]).empty());

	// Off its turn, nothing is held back.
	CHECK(McpViewGateRefusal(3, 5, false, "plan_turn").empty());

	// Once viewed, the whole turn is open, for that seat only.
	McpViewGateViewed(3, 5);
	CHECK(McpViewGateRefusal(3, 5, true, "move_unit").empty());
	CHECK(McpViewGateRefusal(3, 5, true, "end_turn").empty());
	CHECK(!McpViewGateRefusal(2, 5, true, "move_unit").empty());

	// The next turn needs its own view.
	CHECK(!McpViewGateRefusal(3, 6, true, "move_unit").empty());

	// A new or loaded game forgets every view.
	McpViewGateReset();
	CHECK(!McpViewGateRefusal(3, 5, true, "move_unit").empty());

	if(s_failures) { std::printf("%d failure(s)\n", s_failures); return 1; }
	std::printf("view gate: all passed\n");
	return 0;
}
