#include "McpCityStateHistory.h"
#include <cstdlib>
#include <sstream>

namespace
{
	const char* const SEP = " | ";

	std::string OneLine(const std::string& text)
	{
		std::string out;
		for(size_t i = 0; i < text.size(); ++i)
		{
			if(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
			out += (text[i] == '\r' || text[i] == '\n') ? ' ' : text[i];
		}
		return out;
	}

	bool ParseLine(std::string line, McpCsHistoryEntry& e)
	{
		if(!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		size_t a = line.find(SEP);
		if(a == std::string::npos) return false;
		size_t b = line.find(SEP, a + 3);
		if(b == std::string::npos) return false;
		e.kind = line.substr(0, a);
		if(e.kind != "deed" && e.kind != "said") return false;
		std::string civ = line.substr(a + 3, b - a - 3);
		size_t space = civ.find(' ');
		if(space == std::string::npos || space == 0) return false;
		std::string number = civ.substr(0, space);
		if(number.find_first_not_of("0123456789") != std::string::npos) return false;
		e.civ = std::atoi(number.c_str());
		e.civName = civ.substr(space + 1);
		e.text = line.substr(b + 3);
		return true;
	}
}

std::string McpCsHistoryFileName(const std::string& cityState)
{
	std::string name = cityState.empty() ? "city-state" : cityState;
	for(size_t i = 0; i < name.size(); ++i)
		if(std::string("\\/:*?\"<>|").find(name[i]) != std::string::npos || (unsigned char)name[i] < 32) name[i] = '-';
	return name + ".md";
}

std::string McpCsHistoryHeader(const std::string& cityState, const std::string& gameId)
{
	return "# " + cityState + ", game " + gameId + "\n\n"
		"Every deed toward this city-state and every line of its conversation in this game, oldest first. "
		"Each line: deed or said, then the civ, then what.\n\n";
}

std::string McpCsHistoryLine(const McpCsHistoryEntry& e)
{
	std::ostringstream number;
	number << e.civ;
	return e.kind + SEP + number.str() + " " + OneLine(e.civName) + SEP + OneLine(e.text) + "\n";
}

std::vector<McpCsHistoryEntry> McpCsHistoryParse(const std::string& content)
{
	std::vector<McpCsHistoryEntry> out;
	size_t start = 0;
	while(start < content.size())
	{
		size_t end = content.find('\n', start);
		if(end == std::string::npos) end = content.size();
		McpCsHistoryEntry e;
		if(ParseLine(content.substr(start, end - start), e)) out.push_back(e);
		start = end + 1;
	}
	return out;
}
