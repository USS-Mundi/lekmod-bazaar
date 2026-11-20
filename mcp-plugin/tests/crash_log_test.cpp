// Native tests for the crash log's game-free parts (no game needed): tests/run_crash_log_test.sh
#include "McpCrashLog.h"
#include <cstdio>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

int main()
{
	CHECK(McpHeapCheckEveryMs(0) == 500);
	CHECK(McpHeapCheckEveryMs(10) == 500);
	CHECK(McpHeapCheckEveryMs(50) == 1000);
	CHECK(McpHeapCheckEveryMs(200) == 4000);
	CHECK(McpHeapCheckEveryMs(201) == 0);
	if(s_failures) { std::printf("%d check(s) failed\n", s_failures); return 1; }
	std::printf("all crash log checks pass\n");
	return 0;
}
