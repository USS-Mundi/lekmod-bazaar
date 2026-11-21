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

void McpAddMessageNotice(picojson::object& reply)
{
	reply["woken_by_message"] = picojson::value(true);
	picojson::object::const_iterator it = reply.find(MCP_DO_NOW);
	std::string then = it == reply.end() ? std::string() : it->second.to_str();
	reply[MCP_DO_NOW] = picojson::value(then.empty() ? std::string("a player wrote to you: the lines are in messages. Answer with say if you choose, then call wait_for_my_turn.")
		: "a player wrote to you: the lines are in messages. Answer with say if you choose. Then " + then);
}
