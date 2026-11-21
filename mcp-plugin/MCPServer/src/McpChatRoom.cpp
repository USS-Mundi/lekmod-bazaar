#include "McpChatRoom.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace
{
	const char* HUMAN = "human";
	const char* ALL = "all";

#ifdef _WIN32
	const char* SEP = "\\";

	std::wstring Wide(const std::string& s)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
		if(n <= 1) return std::wstring();
		std::wstring w(n - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
		return w;
	}

	std::string Narrow(const wchar_t* w)
	{
		int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
		if(n <= 1) return std::string();
		std::string s(n - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
		return s;
	}

	FILE* OpenFile(const std::string& path, const char* mode)
	{
		return _wfopen(Wide(path).c_str(), Wide(mode).c_str());
	}

	void MakeDir(const std::string& path)
	{
		CreateDirectoryW(Wide(path).c_str(), NULL);
	}

	// Names of the files in dir ending in suffix, without it.
	std::vector<std::string> Files(const std::string& dir, const std::string& suffix)
	{
		std::vector<std::string> out;
		WIN32_FIND_DATAW data;
		HANDLE h = FindFirstFileW(Wide(dir + SEP + "*" + suffix).c_str(), &data);
		if(h == INVALID_HANDLE_VALUE) return out;
		do
		{
			std::string n = Narrow(data.cFileName);
			if(n.size() > suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
				out.push_back(n.substr(0, n.size() - suffix.size()));
		} while(FindNextFileW(h, &data));
		FindClose(h);
		return out;
	}
#else
	const char* SEP = "/";

	FILE* OpenFile(const std::string& path, const char* mode)
	{
		return fopen(path.c_str(), mode);
	}

	void MakeDir(const std::string& path)
	{
		mkdir(path.c_str(), 0755);
	}

	std::vector<std::string> Files(const std::string& dir, const std::string& suffix)
	{
		std::vector<std::string> out;
		DIR* d = opendir(dir.c_str());
		if(!d) return out;
		while(dirent* e = readdir(d))
		{
			std::string n = e->d_name;
			if(n.size() > suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
				out.push_back(n.substr(0, n.size() - suffix.size()));
		}
		closedir(d);
		return out;
	}
#endif

	// Every folder along the path, so the first line of a game makes MCP\<game>\chat.
	void MakeDirs(const std::string& path)
	{
		for(size_t i = 1; i <= path.size(); ++i)
		{
			if(i == path.size() || path[i] == '/' || path[i] == '\\') MakeDir(path.substr(0, i));
		}
	}

	bool ReadFile(const std::string& path, std::string& out)
	{
		FILE* f = OpenFile(path, "rb");
		if(!f) return false;
		out.clear();
		char buf[4096];
		size_t n;
		while((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
		fclose(f);
		return true;
	}

	bool WriteFile(const std::string& path, const std::string& text, bool append)
	{
		FILE* f = OpenFile(path, append ? "ab" : "wb");
		if(!f) return false;
		bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
		return fclose(f) == 0 && ok;
	}

	std::vector<std::string> SplitLines(const std::string& s)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while(start < s.size())
		{
			size_t end = s.find('\n', start);
			std::string line = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if(!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
			if(!line.empty()) out.push_back(line);
			if(end == std::string::npos) break;
			start = end + 1;
		}
		return out;
	}

	std::string Trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r\n");
		if(a == std::string::npos) return "";
		size_t b = s.find_last_not_of(" \t\r\n");
		return s.substr(a, b - a + 1);
	}

	int ToInt(const std::string& s, int fallback)
	{
		if(s.empty() || s.size() > 9) return fallback;
		for(size_t i = 0; i < s.size(); ++i) if(s[i] < '0' || s[i] > '9') return fallback;
		return atoi(s.c_str());
	}

	std::string Str(int n)
	{
		std::ostringstream o;
		o << n;
		return o.str();
	}

	// Lower-case letters and digits only: "Kuala Lumpur" and "kuala-lumpur" name the same city-state.
	std::string Key(const std::string& s)
	{
		std::string out;
		for(size_t i = 0; i < s.size(); ++i)
		{
			unsigned char c = (unsigned char)s[i];
			if(c >= 'A' && c <= 'Z') out += (char)(c - 'A' + 'a');
			else if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) out += (char)c;
		}
		return out;
	}

	bool IsSlotToken(const std::string& t)
	{
		if(t.size() < 2 || t.size() > 4 || t[0] != 'p') return false;
		for(size_t i = 1; i < t.size(); ++i) if(t[i] < '0' || t[i] > '9') return false;
		return true;
	}

	bool IsMember(const std::string& t) { return t == HUMAN || IsSlotToken(t); }

	std::vector<std::string> Split(const std::string& s, char sep)
	{
		std::vector<std::string> out;
		size_t start = 0;
		for(;;)
		{
			size_t end = s.find(sep, start);
			out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if(end == std::string::npos) break;
			start = end + 1;
		}
		return out;
	}

	std::vector<std::string> MembersOf(const std::string& id)
	{
		if(id == ALL) return std::vector<std::string>();
		return Split(id, '+');
	}

	bool ValidId(const std::string& id)
	{
		if(id == ALL) return true;
		std::vector<std::string> m = MembersOf(id);
		if(m.size() < 2 || m.size() > (size_t)MCP_CHAT_MAX_MEMBERS) return false;
		for(size_t i = 0; i < m.size(); ++i)
		{
			if(!IsMember(m[i])) return false;
			if(i > 0 && !(m[i - 1] < m[i])) return false;
		}
		return true;
	}

	std::string JoinId(std::vector<std::string> members)
	{
		std::sort(members.begin(), members.end());
		members.erase(std::unique(members.begin(), members.end()), members.end());
		std::string id;
		for(size_t i = 0; i < members.size(); ++i) id += (i ? "+" : "") + members[i];
		return id;
	}

	std::string Join(const std::vector<std::string>& parts, const char* sep)
	{
		std::string out;
		for(size_t i = 0; i < parts.size(); ++i) out += (i ? sep : "") + parts[i];
		return out;
	}

	// "[HH:MM:SS T<turn>] <from>: <text>", or without " T<turn>" as lines from before turns were kept.
	bool ParseLine(const std::string& raw, McpChatLine& out)
	{
		if(raw.size() < 4 || raw[0] != '[') return false;
		size_t close = raw.find("] ");
		if(close == std::string::npos) return false;
		std::string stamp = raw.substr(1, close - 1);
		size_t t = stamp.find(" T");
		out.time = stamp.substr(0, t);
		out.turn = t == std::string::npos ? -1 : ToInt(stamp.substr(t + 2), -1);
		size_t colon = raw.find(": ", close + 2);
		if(colon == std::string::npos) return false;
		out.from = raw.substr(close + 2, colon - close - 2);
		out.text = raw.substr(colon + 2);
		return !out.from.empty();
	}

	std::string FormatLine(const McpChatLine& l)
	{
		return "[" + l.time + " T" + Str(l.turn) + "] " + l.from + ": " + l.text + "\n";
	}

	// Line breaks become spaces: a chat line is one line on disk and on screen.
	std::string OneLine(const std::string& text)
	{
		std::string out;
		for(size_t i = 0; i < text.size(); ++i)
		{
			char c = text[i];
			if(c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
			if(c == '\r' || c == '\n') out += ' ';
			else if(c != '\0') out += c;
		}
		return Trim(out);
	}

	const McpChatSeat* SeatOf(const McpChatTable& t, const std::string& token)
	{
		if(!IsSlotToken(token)) return NULL;
		int slot = ToInt(token.substr(1), -1);
		for(size_t i = 0; i < t.seats.size(); ++i) if(t.seats[i].slot == slot) return &t.seats[i];
		return NULL;
	}

	std::string TokenOf(const McpChatSeat& s) { return "p" + Str(s.slot); }

	bool IsCityState(const McpChatTable& t, const std::string& token)
	{
		const McpChatSeat* s = SeatOf(t, token);
		return s && s->cityState;
	}

	// What a member is called to another: "the human", a civilization's name, or the token when the game no longer
	// knows it.
	std::string Display(const McpChatTable& t, const std::string& token)
	{
		if(token == HUMAN) return "the human";
		const McpChatSeat* s = SeatOf(t, token);
		return s ? s->name : token;
	}

	// What `to` says to reach a member.
	std::string Address(const McpChatTable& t, const std::string& token)
	{
		if(token == HUMAN) return HUMAN;
		const McpChatSeat* s = SeatOf(t, token);
		return s ? s->name : token;
	}

	// The names an agent can write to, for a refusal: models' civilizations at the table, then human and all.
	std::string Writable(const McpChatTable& t, const std::string& from)
	{
		std::vector<std::string> names;
		for(size_t i = 0; i < t.seats.size(); ++i)
		{
			const McpChatSeat& s = t.seats[i];
			if(s.alive && s.readable && TokenOf(s) != from && (!s.cityState || t.cityStateSeatsOn)) names.push_back(s.name);
		}
		if(from != HUMAN) names.push_back(HUMAN);
		names.push_back(ALL);
		return Join(names, ", ");
	}

	struct Candidate
	{
		int turn;
		std::string time;
		std::string id;
		size_t index;
	};

	bool Earlier(const Candidate& a, const Candidate& b)
	{
		if(a.turn != b.turn) return a.turn < b.turn;
		if(a.time != b.time) return a.time < b.time;
		if(a.id != b.id) return a.id < b.id;
		return a.index < b.index;
	}
}

bool McpWakeLetThrough(bool everLetThrough, unsigned long lastMs, unsigned long nowMs, unsigned long cooldownMs)
{
	if(!everLetThrough) return true;
	unsigned int since = (unsigned int)nowMs - (unsigned int)lastMs; // GetTickCount is 32 bits and wraps
	return since >= (unsigned int)cooldownMs;
}

McpChatRoom::McpChatRoom() {}

std::string McpChatRoom::IdOf(const std::string& sender, const std::vector<std::string>& members)
{
	std::vector<std::string> all(1, sender);
	for(size_t i = 0; i < members.size(); ++i) all.push_back(members[i]);
	for(size_t i = 0; i < all.size(); ++i) if(!IsMember(all[i])) return "";
	std::string id = JoinId(all);
	return ValidId(id) ? id : "";
}

void McpChatRoom::Open(const std::string& folder)
{
	m_folder = folder;
	m_chats.clear();
	if(folder.empty()) return;
	std::set<std::string> ids;
	std::vector<std::string> logs = Files(folder, ".log"), names = Files(folder, ".name");
	ids.insert(logs.begin(), logs.end());
	ids.insert(names.begin(), names.end());
	for(std::set<std::string>::const_iterator it = ids.begin(); it != ids.end(); ++it)
	{
		const std::string& id = *it;
		if(!ValidId(id)) continue;
		Chat& c = m_chats[id];
		c.members = MembersOf(id);
		std::string s;
		if(ReadFile(folder + SEP + id + ".log", s))
		{
			std::vector<std::string> raw = SplitLines(s);
			for(size_t i = 0; i < raw.size(); ++i)
			{
				McpChatLine l;
				if(ParseLine(raw[i], l)) c.lines.push_back(l); // a torn last line is skipped
			}
		}
		if(ReadFile(folder + SEP + id + ".name", s))
		{
			std::vector<std::string> lines = SplitLines(s);
			if(!lines.empty()) c.name = Trim(lines[0]);
		}
		if(ReadFile(folder + SEP + id + ".started", s))
		{
			std::vector<std::string> w = Split(Trim(s), ' ');
			if(!w.empty()) c.startedBy = w[0];
			if(w.size() > 1) c.startedTurn = ToInt(w[1], -1);
		}
		if(ReadFile(folder + SEP + id + ".cursors", s))
		{
			std::vector<std::string> lines = SplitLines(s);
			for(size_t i = 0; i < lines.size(); ++i)
			{
				std::vector<std::string> w = Split(lines[i], ' ');
				if(w.size() != 3 || !IsMember(w[1])) continue;
				int n = ToInt(w[2], -1);
				if(n < 0) continue;
				if(w[0] == "seen") c.seen[w[1]] = n;
				else if(w[0] == "delivered") c.delivered[w[1]] = n;
			}
		}
	}
}

bool McpChatRoom::SaveCursor(const char* kind, const std::string& who, const std::string& id, int value)
{
	std::map<std::string, Chat>::iterator it = m_chats.find(id);
	if(it == m_chats.end()) return false;
	std::map<std::string, int>& m = std::string(kind) == "seen" ? it->second.seen : it->second.delivered;
	if(m.count(who) && m[who] == value) return true;
	m[who] = value;
	if(m_folder.empty()) return true;
	std::string text;
	for(std::map<std::string, int>::const_iterator s = it->second.seen.begin(); s != it->second.seen.end(); ++s)
		text += "seen " + s->first + " " + Str(s->second) + "\n";
	for(std::map<std::string, int>::const_iterator d = it->second.delivered.begin(); d != it->second.delivered.end(); ++d)
		text += "delivered " + d->first + " " + Str(d->second) + "\n";
	MakeDirs(m_folder);
	return WriteFile(m_folder + SEP + id + ".cursors", text, false);
}

McpChatSent McpChatRoom::Send(const McpChatTable& table, const std::string& from, const std::string& to, const std::string& text,
	const std::string& name)
{
	McpChatSent r;
	std::string words = OneLine(text);
	if(words.empty()) { r.reason = "There is nothing to say: words is empty."; return r; }
	if(words.size() > MCP_CHAT_MAX_TEXT) { r.reason = "Say it in shorter lines: 2000 characters at most."; return r; }
	bool fromCityState = IsCityState(table, from);

	std::vector<std::string> parts = Split(to, ',');
	std::vector<std::string> members(1, from);
	bool all = false;
	size_t named = 0;
	for(size_t i = 0; i < parts.size(); ++i)
	{
		std::string part = Trim(parts[i]);
		if(part.empty()) continue;
		++named;
		std::string key = Key(part);
		if(key == ALL) { all = true; continue; }
		std::string token;
		if(key == HUMAN || key == Key(table.humanName)) token = HUMAN;
		for(size_t k = 0; token.empty() && k < table.humanKeys.size(); ++k) if(key == Key(table.humanKeys[k])) token = HUMAN;
		const McpChatSeat* seat = IsSlotToken(part) ? SeatOf(table, part) : NULL;
		for(size_t s = 0; token.empty() && !seat && s < table.seats.size(); ++s)
		{
			const McpChatSeat& c = table.seats[s];
			bool match = key == Key(c.name);
			for(size_t k = 0; !match && k < c.keys.size(); ++k) match = key == Key(c.keys[k]);
			if(match) seat = &c;
		}
		if(token.empty() && !seat)
		{
			r.reason = "Nobody at the table is called '" + part + "'. You can write to: " + Writable(table, from) + ".";
			return r;
		}
		if(seat)
		{
			if(!seat->alive) { r.reason = seat->name + " is gone from the game."; return r; }
			if(seat->cityState && !table.cityStateSeatsOn)
			{
				r.reason = "City-states are not seated in this game, so " + seat->name + " reads no chat.";
				return r;
			}
			if(!seat->readable)
			{
				r.reason = seat->name + " is played by the game's own AI; no one reads chat for it. Speak to it through diplomacy.";
				return r;
			}
			token = TokenOf(*seat);
		}
		members.push_back(token);
	}
	if(named == 0) { r.reason = "to is empty: name a civilization, several joined by commas, human, or all."; return r; }

	std::string id;
	if(all)
	{
		if(named > 1) { r.reason = "'all' is the whole table and cannot be combined with other names."; return r; }
		if(fromCityState) { r.reason = "A city-state speaks in groups only, never to the whole table."; return r; }
		id = ALL;
	}
	else
	{
		id = JoinId(members);
		std::vector<std::string> set = MembersOf(id);
		if(set.size() < 2) { r.reason = "You cannot write only to yourself."; return r; }
		if(set.size() > (size_t)MCP_CHAT_MAX_MEMBERS)
		{
			r.reason = "A chat holds at most 8 members, you included; this one would hold " + Str((int)set.size()) + ".";
			return r;
		}
		bool anyCityState = false;
		for(size_t i = 0; i < set.size(); ++i) anyCityState = anyCityState || IsCityState(table, set[i]);
		if(anyCityState && set.size() == 2)
		{
			r.reason = "A city-state speaks in groups of three or more; alone it is reached only by its ally, with chat.";
			return r;
		}
	}

	McpChatLine line;
	line.time = table.clock;
	line.turn = table.turn;
	line.from = from;
	line.text = words;
	bool isNew = m_chats.find(id) == m_chats.end() || m_chats[id].lines.empty();
	if(!m_folder.empty())
	{
		MakeDirs(m_folder);
		if(!WriteFile(m_folder + SEP + id + ".log", FormatLine(line), true))
		{
			r.reason = "The chat could not be written to " + m_folder + ".";
			return r;
		}
	}
	Chat& c = m_chats[id];
	c.members = MembersOf(id);
	if(isNew && c.startedBy.empty())
	{
		c.startedBy = from;
		c.startedTurn = table.turn;
		if(!m_folder.empty()) WriteFile(m_folder + SEP + id + ".started", from + " " + Str(table.turn) + "\n", false);
	}
	c.lines.push_back(line);
	if(!name.empty()) Name(from, id, name);

	r.ok = true;
	r.id = id;
	r.label = Label(table, id, c, from);
	r.replyTo = ReplyTo(table, id, c, from);
	if(id == ALL)
	{
		if(from != HUMAN) r.wake.push_back(HUMAN);
		for(size_t i = 0; i < table.seats.size(); ++i)
		{
			const McpChatSeat& s = table.seats[i];
			if(s.alive && s.readable && !s.cityState && TokenOf(s) != from) r.wake.push_back(TokenOf(s));
		}
	}
	else
	{
		for(size_t i = 0; i < c.members.size(); ++i) if(c.members[i] != from) r.wake.push_back(c.members[i]);
	}
	return r;
}

bool McpChatRoom::MayRead(const std::string& reader, const std::string& id, const McpChatTable* table) const
{
	if(!IsMember(reader)) return false;
	if(id == ALL) return reader == HUMAN || !table || !IsCityState(*table, reader);
	std::vector<std::string> m = MembersOf(id);
	return std::find(m.begin(), m.end(), reader) != m.end();
}

std::string McpChatRoom::Label(const McpChatTable& table, const std::string& id, const Chat& chat, const std::string& reader) const
{
	if(id == ALL) return "the whole table";
	std::vector<std::string> others;
	for(size_t i = 0; i < chat.members.size(); ++i) if(chat.members[i] != reader) others.push_back(Display(table, chat.members[i]));
	if(!chat.name.empty()) return chat.name + " (you, " + Join(others, ", ") + ")";
	if(others.size() == 1) return "you and " + others[0];
	return "you, " + Join(others, ", ");
}

std::string McpChatRoom::ReplyTo(const McpChatTable& table, const std::string& id, const Chat& chat, const std::string& reader) const
{
	if(id == ALL) return ALL;
	std::vector<std::string> others;
	for(size_t i = 0; i < chat.members.size(); ++i) if(chat.members[i] != reader) others.push_back(Address(table, chat.members[i]));
	return Join(others, ", ");
}

std::vector<McpChatInfo> McpChatRoom::ChatsOf(const McpChatTable& table, const std::string& viewer) const
{
	std::vector<McpChatInfo> out;
	for(std::map<std::string, Chat>::const_iterator it = m_chats.begin(); it != m_chats.end(); ++it)
	{
		const Chat& c = it->second;
		if(c.lines.empty() || !MayRead(viewer, it->first, &table)) continue;
		McpChatInfo info;
		info.id = it->first;
		info.members = c.members;
		info.name = c.name;
		info.startedBy = c.startedBy;
		info.startedTurn = c.startedTurn;
		info.lines = (int)c.lines.size();
		std::map<std::string, int>::const_iterator s = c.seen.find(viewer);
		for(size_t i = (s == c.seen.end()) ? 0 : (size_t)s->second; i < c.lines.size(); ++i)
			if(c.lines[i].from != viewer) ++info.unread;
		info.last = c.lines.back();
		info.lastTurn = info.last.turn >= 0 ? info.last.turn : c.startedTurn;
		info.stale = info.lastTurn >= 0 && table.turn - info.lastTurn >= MCP_CHAT_STALE_TURNS;
		out.push_back(info);
	}
	return out;
}

bool McpChatRoom::Lines(const std::string& viewer, const std::string& id, int from, std::vector<McpChatLine>& out) const
{
	out.clear();
	if(!MayRead(viewer, id, NULL)) return false;
	std::map<std::string, Chat>::const_iterator it = m_chats.find(id);
	if(it == m_chats.end()) return true;
	for(size_t i = from < 0 ? 0 : (size_t)from; i < it->second.lines.size(); ++i) out.push_back(it->second.lines[i]);
	return true;
}

bool McpChatRoom::Name(const std::string& viewer, const std::string& id, const std::string& name)
{
	std::string n = OneLine(name);
	if(n.empty() || id == ALL || !ValidId(id) || !MayRead(viewer, id, NULL)) return false;
	std::vector<std::string> members = MembersOf(id);
	if(members.size() < 3) return false;
	Chat& c = m_chats[id];
	c.members = members;
	if(!c.name.empty()) return false;
	c.name = n.substr(0, 60);
	if(!m_folder.empty())
	{
		MakeDirs(m_folder);
		WriteFile(m_folder + SEP + id + ".name", c.name + "\n", false);
	}
	return true;
}

bool McpChatRoom::Seen(const std::string& viewer, const std::string& id)
{
	std::map<std::string, Chat>::iterator it = m_chats.find(id);
	if(it == m_chats.end() || !MayRead(viewer, id, NULL)) return false;
	return SaveCursor("seen", viewer, id, (int)it->second.lines.size());
}

std::vector<McpChatDelivery> McpChatRoom::Take(const McpChatTable& table, const std::string& reader, int max, int& more)
{
	more = 0;
	std::vector<McpChatDelivery> out;
	// Each chat's waiting lines in order; the oldest across chats goes first, and a chat's own order always holds.
	std::map<std::string, std::vector<Candidate> > waiting;
	for(std::map<std::string, Chat>::const_iterator it = m_chats.begin(); it != m_chats.end(); ++it)
	{
		if(!MayRead(reader, it->first, &table)) continue;
		const Chat& c = it->second;
		std::map<std::string, int>::const_iterator d = c.delivered.find(reader);
		for(size_t i = (d == c.delivered.end()) ? 0 : (size_t)d->second; i < c.lines.size(); ++i)
		{
			if(c.lines[i].from == reader) continue;
			Candidate k;
			k.turn = c.lines[i].turn;
			k.time = c.lines[i].time;
			k.id = it->first;
			k.index = i;
			waiting[it->first].push_back(k);
		}
	}
	std::map<std::string, size_t> next;
	std::map<std::string, size_t> lastTaken;
	for(int n = 0; n < max; ++n)
	{
		const Candidate* best = NULL;
		for(std::map<std::string, std::vector<Candidate> >::const_iterator w = waiting.begin(); w != waiting.end(); ++w)
		{
			size_t at = next[w->first];
			if(at < w->second.size() && (!best || Earlier(w->second[at], *best))) best = &w->second[at];
		}
		if(!best) break;
		const Chat& c = m_chats[best->id];
		const McpChatLine& l = c.lines[best->index];
		McpChatDelivery d;
		d.chat = Label(table, best->id, c, reader);
		d.replyTo = ReplyTo(table, best->id, c, reader);
		d.from = Display(table, l.from);
		d.turn = l.turn;
		d.words = l.text;
		out.push_back(d);
		lastTaken[best->id] = best->index;
		++next[best->id];
	}
	for(std::map<std::string, Chat>::const_iterator it = m_chats.begin(); it != m_chats.end(); ++it)
	{
		if(!MayRead(reader, it->first, &table)) continue;
		std::map<std::string, std::vector<Candidate> >::const_iterator w = waiting.find(it->first);
		size_t left = (w == waiting.end()) ? 0 : w->second.size() - next[it->first];
		more += (int)left;
		// Everything handed over, and the reader's own lines between, counts as delivered.
		if(left > 0 && lastTaken.find(it->first) == lastTaken.end()) continue;
		int to = left == 0 ? (int)it->second.lines.size() : (int)lastTaken[it->first] + 1;
		std::map<std::string, int>::const_iterator d = it->second.delivered.find(reader);
		int from = d == it->second.delivered.end() ? 0 : d->second;
		if(to > from) SaveCursor("delivered", reader, it->first, to);
	}
	return out;
}

bool McpChatRoom::Pending(const McpChatTable& table, const std::string& reader) const
{
	for(std::map<std::string, Chat>::const_iterator it = m_chats.begin(); it != m_chats.end(); ++it)
	{
		if(!MayRead(reader, it->first, &table)) continue;
		const Chat& c = it->second;
		std::map<std::string, int>::const_iterator d = c.delivered.find(reader);
		for(size_t i = (d == c.delivered.end()) ? 0 : (size_t)d->second; i < c.lines.size(); ++i)
			if(c.lines[i].from != reader) return true;
	}
	return false;
}
