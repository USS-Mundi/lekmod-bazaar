// Native tests for who may take a seat another model left.
#include "McpSeatRules.h"
#include <cstdio>
#include <string>

static int g_failed = 0;
static void Check(bool ok, const char* what)
{
	if(!ok) { std::printf("FAIL: %s\n", what); ++g_failed; }
}

static void TestLeftSeat()
{
	Check(McpMayTakeLeftSeat(true, false), "the model that played an opponent seat takes it back");
	Check(!McpMayTakeLeftSeat(false, false), "another model never takes an opponent seat someone left");
	Check(McpMayTakeLeftSeat(false, true), "a seat on the human's team may go to any model the human assigns");
	Check(McpMayTakeLeftSeat(true, true), "the model that played a teammate seat takes it back");
}

static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

static void TestTakeSeatNext()
{
	std::string first = McpTakeSeatNext(false, false, false);
	Check(first.find("greet the human with say") == 0, "a model sitting down for the first time greets the human first");
	Check(Has(first, "call wait_for_my_turn") && Has(first, "your turn comes after the human's each round"), "then waits for its turn");
	Check(!Has(McpTakeSeatNext(false, false, true), "greet"), "coming back to its seat, it does not greet again");
	std::string now = McpTakeSeatNext(true, false, false);
	Check(now.find("greet the human with say") == 0 && Has(now, "it is your turn"), "sitting down on its turn, it greets and then plays");
	std::string cs = McpTakeSeatNext(false, true, false);
	Check(!Has(cs, "greet") && Has(cs, "send your next turn with plan_turn now") && Has(cs, "a city-state's turn comes after every major civilization's"),
		"a city-state seat plans first and greets no one");
}

int main()
{
	TestLeftSeat();
	TestTakeSeatNext();
	if(g_failed) { std::printf("%d failed\n", g_failed); return 1; }
	std::printf("seat rules: all passed\n");
	return 0;
}
