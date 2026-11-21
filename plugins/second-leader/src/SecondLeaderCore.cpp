#include "SecondLeaderCore.h"
#include <stdio.h>
#include <sstream>
#include <stdlib.h>
#include <string.h>

std::vector<int> SlShuffle(const std::vector<int>& items, unsigned int seed)
{
	// Fisher-Yates with a fixed 32-bit generator, so the order never depends on the compiler's rand()
	std::vector<int> out(items);
	unsigned int x = seed * 2654435761u + 0x9E3779B9u;
	for(size_t i = out.size(); i > 1; --i)
	{
		x = x * 1103515245u + 12345u;
		size_t j = (size_t)((x >> 8) % (unsigned int)i);
		int t = out[i - 1]; out[i - 1] = out[j]; out[j] = t;
	}
	return out;
}

std::vector<int> SlPair(const std::vector<int>& players, const std::vector<int>& pool, unsigned int seed)
{
	std::set<int> taken(players.begin(), players.end());
	std::vector<int> free;
	for(size_t i = 0; i < pool.size(); ++i)
		if(taken.insert(pool[i]).second) free.push_back(pool[i]);
	free = SlShuffle(free, seed);
	std::vector<int> out(players.size(), -1);
	size_t next = 0;
	for(size_t i = 0; i < players.size(); ++i)
		if(players[i] >= 0 && next < free.size()) out[i] = free[next++];
	return out;
}

std::vector<int> SlTakingPart(const std::vector<SlSlot>& slots, const std::vector<bool>& active, const std::vector<SlSlot>& recorded, bool& fromRecord)
{
	fromRecord = recorded.size() == slots.size() && active.size() == slots.size();
	for(size_t p = 0; fromRecord && p < slots.size(); ++p)
	{
		bool in = recorded[p].civ >= 0;
		if(in && (recorded[p].civ != slots[p].civ || recorded[p].leader != slots[p].leader)) fromRecord = false;
		if(active[p] && !in) fromRecord = false;
	}
	std::vector<int> out(slots.size(), -1);
	for(size_t p = 0; p < slots.size(); ++p)
	{
		bool in = fromRecord ? recorded[p].civ >= 0 : (p < active.size() && active[p]);
		if(in && slots[p].civ >= 0 && slots[p].leader >= 0) out[p] = slots[p].civ;
	}
	return out;
}

std::string SlRecordText(const std::vector<SlSlot>& slots, const std::vector<bool>& active)
{
	std::ostringstream out;
	for(size_t p = 0; p < slots.size() && p < active.size(); ++p)
	{
		if(!active[p] || slots[p].civ < 0 || slots[p].leader < 0) continue;
		out << p << ' ' << slots[p].civ << ' ' << slots[p].leader << '\n';
	}
	return out.str();
}

std::vector<SlSlot> SlRecordParse(const std::string& text, size_t n)
{
	SlSlot blank = { -1, -1 };
	std::vector<SlSlot> out(n, blank);
	bool any = false;
	size_t at = 0;
	while(at < text.size())
	{
		size_t end = text.find('\n', at);
		if(end == std::string::npos) end = text.size();
		std::string line = text.substr(at, end - at);
		at = end + 1;
		if(!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if(line.empty()) continue;
		int p = -1, civ = -1, leader = -1;
		char tail = 0;
		if(sscanf(line.c_str(), "%d %d %d %c", &p, &civ, &leader, &tail) != 3 || p < 0 || (size_t)p >= n || civ < 0 || leader < 0)
			return std::vector<SlSlot>();
		out[p].civ = civ; out[p].leader = leader;
		any = true;
	}
	return any ? out : std::vector<SlSlot>();
}

bool SlIsSet(const char* value)
{
	if(!value || !*value) return false;
	if(strcmp(value, "false") == 0 || strcmp(value, "NONE") == 0) return false;
	char* end = 0;
	double d = strtod(value, &end);
	if(end && *end == 0) return d != 0;
	return true;
}

std::vector<std::string> SlLeftOut(const std::set<std::string>& firstSlots, const std::set<std::string>& secondSlots, const std::set<std::string>& secondBans)
{
	std::vector<std::string> out;
	for(std::set<std::string>::const_iterator it = secondSlots.begin(); it != secondSlots.end(); ++it)
		if(firstSlots.count(*it)) out.push_back(*it);
	out.insert(out.end(), secondBans.begin(), secondBans.end());
	return out;
}

std::vector<SlFreeUnit> SlMergeFreeUnits(const std::vector<SlFreeUnit>& first, const std::vector<SlFreeUnit>& second)
{
	std::vector<SlFreeUnit> out;
	for(size_t i = 0; i < second.size(); ++i)
	{
		int have = 0, ai = second[i].ai;
		for(size_t k = 0; k < first.size(); ++k)
			if(first[k].cls == second[i].cls) { have += first[k].count; ai = first[k].ai; }
		if(second[i].count <= have) continue;
		SlFreeUnit u = second[i];
		u.ai = ai;
		out.push_back(u);
	}
	return out;
}

std::vector<SlMerged> SlMergeOverrides(const std::vector<SlOverride>& first, const std::vector<SlOverride>& second)
{
	std::vector<SlMerged> out;
	for(size_t i = 0; i < second.size(); ++i)
	{
		if(second[i].item < 0) continue;
		bool own = false;
		for(size_t k = 0; k < first.size(); ++k) own |= first[k].cls == second[i].cls;
		SlMerged m;
		m.cls = second[i].cls; m.item = second[i].item; m.merged = !own;
		out.push_back(m);
	}
	return out;
}

std::string SlPlain(const std::string& text)
{
	std::string out;
	for(size_t i = 0; i < text.size(); ++i)
	{
		if(text[i] == '[')
		{
			size_t e = text.find(']', i);
			if(e != std::string::npos)
			{
				if(text.compare(i, e - i + 1, "[NEWLINE]") == 0) out += ' ';
				i = e;
				continue;
			}
		}
		out += text[i];
	}
	std::string tidy;
	for(size_t i = 0; i < out.size(); ++i)
	{
		if(out[i] == ' ' && (tidy.empty() || tidy[tidy.size() - 1] == ' ')) continue;
		tidy += out[i];
	}
	while(!tidy.empty() && tidy[tidy.size() - 1] == ' ') tidy.erase(tidy.size() - 1);
	return tidy;
}

bool SlIsTypeName(const std::string& s)
{
	if(s.empty()) return false;
	for(size_t i = 0; i < s.size(); ++i)
	{
		char c = s[i];
		if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
	}
	return true;
}

std::string SlJsonString(const std::string& s)
{
	std::string out = "\"";
	for(size_t i = 0; i < s.size(); ++i)
	{
		unsigned char ch = (unsigned char)s[i];
		if(ch == '"' || ch == '\\') { out += '\\'; out += (char)ch; }
		else if(ch == '\n') out += "\\n";
		else if(ch == '\r') out += "\\r";
		else if(ch == '\t') out += "\\t";
		else if(ch < 0x20) out += ' ';
		else out += (char)ch;
	}
	return out + "\"";
}
