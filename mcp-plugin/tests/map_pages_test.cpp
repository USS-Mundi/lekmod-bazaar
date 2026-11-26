// Native tests for the map pages game_settings carries: tests/run_map_pages_test.sh
#include "McpMapPages.h"
#include <cstdio>
#include <cstring>
#include <string>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

int main()
{
	// A map script with a page in docs/agent/maps gets that page, named as game_settings.map_type names it.
	const char* fusion = McpMapPage("MundiFusion");
	CHECK(fusion != NULL);
	CHECK(fusion && std::strstr(fusion, "Mundi Fusion") != NULL);
	CHECK(fusion && std::strstr(fusion, "Astronomy") != NULL);
	// The script's file name may come in any case.
	CHECK(McpMapPage("mundifusion") == fusion);
	// Any other map has none.
	CHECK(McpMapPage("LekmapPangaeaFractalv6.3") == NULL);
	CHECK(McpMapPage("") == NULL);
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("map pages: all passed\n");
	return 0;
}
