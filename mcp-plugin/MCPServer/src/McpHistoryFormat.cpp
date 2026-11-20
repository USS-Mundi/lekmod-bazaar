// Pure parts of the history record; see McpHistoryFormat.h.
#include "McpHistoryFormat.h"
#include <cctype>
#include <sstream>

std::string McpReplaySetKey(const std::string& setName)
{
	static const std::string PREFIX = "REPLAYDATASET_";
	std::string s = setName.compare(0, PREFIX.size(), PREFIX) == 0 ? setName.substr(PREFIX.size()) : setName;
	for(size_t i = 0; i < s.size(); ++i) s[i] = (char)std::tolower((unsigned char)s[i]);
	return s;
}

bool McpHistoryWindow(int iFrom, int iThrough, int iNow, int& iLo, int& iHi, std::string& err)
{
	int lo = iFrom < 0 ? 0 : iFrom;
	int hi = (iThrough < 0 || iThrough > iNow) ? iNow : iThrough;
	if(lo > iNow || lo > hi)
	{
		std::ostringstream s;
		s << "from_turn " << lo << " is after " << (lo > iNow ? "the current turn " : "through_turn ") << (lo > iNow ? iNow : hi);
		err = s.str();
		return false;
	}
	iLo = lo;
	iHi = hi;
	return true;
}

picojson::value McpHistorySeries(const std::map<unsigned int, int>& byTurn, int iLo, int iHi)
{
	picojson::array a;
	for(int t = iLo; t <= iHi; ++t)
	{
		std::map<unsigned int, int>::const_iterator it = t < 0 ? byTurn.end() : byTurn.find((unsigned int)t);
		a.push_back(it == byTurn.end() ? picojson::value() : picojson::value((double)it->second));
	}
	return picojson::value(a);
}

bool McpHistoryShows(bool bOnScreen, bool bSelf)
{
	return bOnScreen || bSelf;
}

std::string McpHistoryFileName(const std::string& gameId)
{
	std::string id;
	for(size_t i = 0; i < gameId.size(); ++i)
	{
		char c = gameId[i];
		if(std::isalnum((unsigned char)c) || c == '-' || c == '_') id += c;
	}
	return "MCPServer-history-" + (id.empty() ? std::string("unknown") : id) + ".json";
}
