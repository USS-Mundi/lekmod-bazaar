#include "McpMapPages.h"
#include <cctype>
#include <cstddef>

#include "McpMapPagesData.inc"

static bool SameName(const std::string& a, const char* b)
{
	size_t i = 0;
	for(; i < a.size() && b[i]; ++i)
		if(std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
	return i == a.size() && !b[i];
}

const char* McpMapPage(const std::string& mapType)
{
	if(mapType.empty()) return NULL;
	for(size_t i = 0; kMapPages[i][0]; ++i)
		if(SameName(mapType, kMapPages[i][0])) return kMapPages[i][1];
	return NULL;
}
