// Native tests for who may take a seat another model left.
#include "McpSeatRules.h"
#include <cstdio>

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

int main()
{
	TestLeftSeat();
	if(g_failed) { std::printf("%d failed\n", g_failed); return 1; }
	std::printf("seat rules: all passed\n");
	return 0;
}
