#include "McpReplyText.h"

std::string McpReplyText(const picojson::value& v)
{
	if(v.is<std::string>()) return v.get<std::string>();
	if(!v.is<picojson::object>() || !v.get<picojson::object>().count(MCP_DO_NOW)) return v.serialize();
	picojson::object rest = v.get<picojson::object>();
	std::string doNow = rest[MCP_DO_NOW].to_str();
	rest.erase(MCP_DO_NOW);
	return "DO NOW: " + doNow + "\n\n" + picojson::value(rest).serialize();
}
