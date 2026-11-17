#include "McpChronicle.h"
#include <sstream>

namespace
{
	typedef picojson::object JObj;
	typedef picojson::array JArr;
	typedef picojson::value JVal;

	const int CHAPTER_TURNS = 40;      // a chapter is due this long after it opened
	const int ENTRY_TURNS = 30;        // a relations entry holds at most this far ahead
	const char* const DOT = " \xC2\xB7 ";

	JVal J(const std::string& s) { return JVal(s); }
	JVal J(const char* s) { return JVal(std::string(s)); }
	JVal J(int n) { return JVal((double)n); }

	std::string Str(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string();
	}

	bool HasInt(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<double>();
	}

	int Int(const JObj& o, const char* key, int fallback)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<double>() ? (int)it->second.get<double>() : fallback;
	}

	const JObj* Obj(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<JObj>() ? &it->second.get<JObj>() : NULL;
	}

	const JArr* Arr(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<JArr>() ? &it->second.get<JArr>() : NULL;
	}

	std::string Num(int n) { std::ostringstream o; o << n; return o.str(); }
	std::string T(int turn) { return "T" + Num(turn); }
	std::string Count(int n, const char* one, const char* many) { return Num(n) + " " + (n == 1 ? one : many); }

	JVal Strings(const std::vector<std::string>& v)
	{
		JArr a;
		for(size_t i = 0; i < v.size(); ++i) a.push_back(J(v[i]));
		return JVal(a);
	}

	size_t DeedsWith(const McpChronicle& c, const std::string& civ)
	{
		size_t n = 0;
		for(size_t i = 0; i < c.deeds.size(); ++i) if(c.deeds[i].with == civ) ++n;
		return n;
	}

	// The deeds with a player the seat's entry does not cover yet, oldest first.
	std::vector<const McpDeed*> DeedsSince(const McpChronicle& c, const std::string& civ, size_t seen)
	{
		std::vector<const McpDeed*> out;
		size_t n = 0;
		for(size_t i = 0; i < c.deeds.size(); ++i) if(c.deeds[i].with == civ && n++ >= seen) out.push_back(&c.deeds[i]);
		return out;
	}

	std::string Sentence(const std::string& s)
	{
		if(s.empty()) return s;
		char last = s[s.size() - 1];
		return last == '.' || last == '!' || last == '?' ? s : s + ".";
	}

	bool ReachVow(const std::string& kind) { return kind == "war" || kind == "pact" || kind == "deal" || kind == "take"; }
	bool KeepVow(const std::string& kind) { return kind == "peace" || kind == "hold"; }

	// "war by T20", "take Ravenna by T30", "peace through T25"
	std::string VowText(const McpThread& t)
	{
		return t.vow + (t.subject.empty() ? "" : " " + t.subject) + (KeepVow(t.vow) ? " through " : " by ") + T(t.by);
	}

	McpThread* ThreadById(McpChronicle& c, int id)
	{
		for(size_t i = 0; i < c.threads.size(); ++i) if(c.threads[i].id == id) return &c.threads[i];
		return NULL;
	}

	void AddDeed(McpChronicle& c, int turn, const std::string& with, const std::string& text)
	{
		McpDeed d;
		d.turn = turn; d.with = with; d.text = text;
		c.deeds.push_back(d);
	}

	void SettleVow(McpChronicle& c, McpThread& t, const std::string& state, int turn)
	{
		t.vowState = state;
		t.vowTurn = turn;
		AddDeed(c, turn, t.with, "my vow " + state + ": " + VowText(t) + ", sworn " + T(t.opened));
	}

	// One event into the state. The live calls and a load both come through here, so a file replays to the same state.
	void ApplyEvent(McpChronicle& c, const JObj& e)
	{
		std::string kind = Str(e, "e");
		int turn = Int(e, "turn", 0);
		if(kind == "start")
		{
			c.started = true;
			c.opened = turn;
			c.era = Int(e, "era", -1); c.eliminations = Int(e, "eliminations", 0); c.citiesLost = Int(e, "citiesLost", 0);
			if(const JArr* f = Arr(e, "faults")) for(size_t i = 0; i < f->size(); ++i) if((*f)[i].is<std::string>()) c.faults.push_back((*f)[i].get<std::string>());
			if(const JArr* u = Arr(e, "unwritten")) for(size_t i = 0; i < u->size(); ++i) if((*u)[i].is<std::string>()) c.unwritten.push_back((*u)[i].get<std::string>());
			c.settled.assign(c.unwritten.size(), std::string());
		}
		else if(kind == "deed") AddDeed(c, turn, Str(e, "with"), Str(e, "text"));
		else if(kind == "forget") c.gone.insert(Str(e, "civ"));
		else if(kind == "relation")
		{
			McpRelation& r = c.relations[Str(e, "civ")];
			r.text = Str(e, "text"); r.written = turn; r.until = Int(e, "until", turn);
			r.deedsSeen = DeedsWith(c, Str(e, "civ"));
		}
		else if(kind == "thread_open")
		{
			McpThread t;
			t.id = (int)c.threads.size() + 1;
			t.opened = turn; t.with = Str(e, "with"); t.words = Str(e, "words");
			if(const JObj* v = Obj(e, "vow")) { t.vow = Str(*v, "kind"); t.subject = Str(*v, "subject"); t.by = Int(*v, "by", -1); }
			c.threads.push_back(t);
		}
		else if(kind == "thread_end")
		{
			McpThread* t = ThreadById(c, Int(e, "id", 0));
			if(!t) return;
			if(!t->vow.empty() && t->vowState.empty()) SettleVow(c, *t, "withdrawn", turn);
			t->ended = turn; t->ending = Str(e, "ending");
		}
		else if(kind == "vow")
		{
			McpThread* t = ThreadById(c, Int(e, "id", 0));
			if(t) SettleVow(c, *t, Str(e, "state"), turn);
		}
		else if(kind == "round")
		{
			std::set<std::string> served;
			if(const JArr* s = Arr(e, "served")) for(size_t i = 0; i < s->size(); ++i) if((*s)[i].is<std::string>()) served.insert((*s)[i].get<std::string>());
			for(std::map<std::string, McpRelation>::iterator it = c.relations.begin(); it != c.relations.end(); ++it)
				it->second.unserved = served.count(it->first) ? 0 : it->second.unserved + 1;
			for(size_t i = 0; i < c.threads.size(); ++i)
				if(c.threads[i].ended < 0) c.threads[i].unserved = served.count(c.threads[i].with) ? 0 : c.threads[i].unserved + 1;
		}
		else if(kind == "chapter")
		{
			McpChapter ch;
			ch.opened = c.opened; ch.closed = turn;
			ch.reason = Str(e, "reason"); ch.title = Str(e, "title"); ch.became = Str(e, "became"); ch.direction = Str(e, "direction");
			ch.ruled.assign(c.faults.size(), std::string());
			if(const JArr* n = Arr(e, "nature"))
				for(size_t i = 0; i < n->size(); ++i)
				{
					if(!(*n)[i].is<JObj>()) continue;
					const JObj& o = (*n)[i].get<JObj>();
					int f = Int(o, "fault", 0), u = Int(o, "unwritten", 0);
					if(f >= 1 && f <= (int)ch.ruled.size()) ch.ruled[f - 1] = Str(o, "ruled");
					if(u >= 1 && u <= (int)c.settled.size()) c.settled[u - 1] = Str(o, "settled");
				}
			if(const JArr* s = Arr(e, "said"))
				for(size_t i = 0; i < s->size(); ++i)
					if((*s)[i].is<JObj>()) ch.said.push_back(std::make_pair(Str((*s)[i].get<JObj>(), "to"), Str((*s)[i].get<JObj>(), "words")));
			if(const JArr* th = Arr(e, "threads"))
				for(size_t i = 0; i < th->size(); ++i)
				{
					if(!(*th)[i].is<JObj>()) continue;
					const JObj& o = (*th)[i].get<JObj>();
					McpThread* t = ThreadById(c, Int(o, "id", 0));
					if(!t || t->ended >= 0) continue;
					std::string state = Str(o, "state");
					if(state == "owed") { ++t->chapters; continue; }
					if(!t->vow.empty() && t->vowState.empty()) SettleVow(c, *t, "withdrawn", turn);
					t->ended = turn; t->ending = state + ": " + Str(o, "words");
				}
			c.chapters.push_back(ch);
			c.opened = turn;
			c.era = Int(e, "era", c.era); c.eliminations = Int(e, "eliminations", c.eliminations); c.citiesLost = Int(e, "citiesLost", c.citiesLost);
		}
	}

	void Emit(McpChronicle& c, const JObj& e)
	{
		ApplyEvent(c, e);
		c.log.push_back(JVal(e).serialize());
	}

	bool OneOf(const std::string& s, const char* a, const char* b, const char* c3, const char* d = "\x01")
	{
		return s == a || s == b || s == c3 || s == d;
	}

	bool Known(const McpChronicleNow& now, const std::string& name)
	{
		if(now.players.empty()) return true;
		for(size_t i = 0; i < now.players.size(); ++i) if(now.players[i] == name) return true;
		return false;
	}

	std::string Players(const McpChronicleNow& now)
	{
		std::string out;
		for(size_t i = 0; i < now.players.size(); ++i) out += (i ? ", " : "") + now.players[i];
		return out;
	}

	std::string Indented(const std::string& text, const char* pad)
	{
		std::string out;
		std::istringstream lines(text);
		std::string line;
		bool first = true;
		while(std::getline(lines, line))
		{
			if(!first) out += "\n";
			first = false;
			if(!line.empty()) out += pad + line;
		}
		return out;
	}

	std::string Upper(std::string s)
	{
		for(size_t i = 0; i < s.size(); ++i) if(s[i] >= 'a' && s[i] <= 'z') s[i] = (char)(s[i] - 'a' + 'A');
		return s;
	}
}

void McpChronicleLoad(McpChronicle& c, const std::string& fileText)
{
	std::istringstream lines(fileText);
	std::string line;
	while(std::getline(lines, line))
	{
		JVal v;
		if(line.empty() || !picojson::parse(v, line).empty() || !v.is<JObj>()) continue;
		ApplyEvent(c, v.get<JObj>());
	}
}

void McpChronicleStart(McpChronicle& c, int turn, int era, int eliminations, int citiesLost,
	const std::vector<std::string>& faults, const std::vector<std::string>& unwritten)
{
	if(c.started) return;
	JObj e;
	e["e"] = J("start"); e["turn"] = J(turn); e["era"] = J(era); e["eliminations"] = J(eliminations); e["citiesLost"] = J(citiesLost);
	e["faults"] = Strings(faults); e["unwritten"] = Strings(unwritten);
	Emit(c, e);
}

void McpChronicleDeed(McpChronicle& c, int turn, const std::string& with, const std::string& text)
{
	JObj e;
	e["e"] = J("deed"); e["turn"] = J(turn); e["with"] = J(with); e["text"] = J(text);
	Emit(c, e);
}

void McpChronicleForget(McpChronicle& c, const std::string& civ)
{
	if(c.gone.count(civ)) return;
	JObj e;
	e["e"] = J("forget"); e["civ"] = J(civ);
	Emit(c, e);
}

std::string McpChronicleDue(const McpChronicle& c, int era, int eliminations, int citiesLost, int turn)
{
	if(!c.started) return "";
	if(citiesLost > c.citiesLost) return "one of your cities was taken";
	if(eliminations > c.eliminations) return "a civilization left the game";
	if(era > c.era) return "your era changed";
	if(turn - c.opened >= CHAPTER_TURNS) return Num(CHAPTER_TURNS) + " turns have passed since this chapter opened";
	return "";
}

bool McpChronicleApply(McpChronicle& c, const picojson::object& args, const McpChronicleNow& now, McpChronicleResult& out)
{
	out = McpChronicleResult();
	if(!c.started) return true;
	std::vector<std::string>& no = out.refusals;
	std::vector<JObj> events;
	const int turn = now.turn;

	// relations: one entry for each player in the story
	std::set<std::string> sent;
	const JObj* plan = Obj(args, "plan");
	if(plan && plan->find("relations") != plan->end())
	{
		const JObj* rel = Obj(*plan, "relations");
		if(!rel)
			no.push_back("plan.relations is one entry for each player in your story: {\"<civilization>\": {\"text\": what they are to you and what you will do about them, "
				"\"until\": the turn the entry holds until, at most " + Num(ENTRY_TURNS) + " ahead, \"why\": when you write an entry again}}");
		else
			for(JObj::const_iterator it = rel->begin(); it != rel->end(); ++it)
			{
				const std::string& civ = it->first;
				std::string at = "plan.relations." + civ + ": ";
				if(!it->second.is<JObj>()) { no.push_back(at + "an entry is {text, until, why}"); continue; }
				const JObj& entry = it->second.get<JObj>();
				bool ok = true;
				if(!Known(now, civ)) { no.push_back(at + "no player you have met is called that; they are: " + Players(now)); ok = false; }
				if(Str(entry, "text").empty()) { no.push_back(at + "text is missing"); ok = false; }
				int until = Int(entry, "until", -1);
				if(!HasInt(entry, "until") || until <= turn || until > turn + ENTRY_TURNS)
				{
					no.push_back(at + "until is the turn this entry holds until: from " + Num(turn + 1) + " to " + Num(turn + ENTRY_TURNS));
					ok = false;
				}
				std::map<std::string, McpRelation>::const_iterator had = c.relations.find(civ);
				bool stale = had != c.relations.end() && DeedsWith(c, civ) > had->second.deedsSeen, expired = had != c.relations.end() && turn > had->second.until;
				if((stale || expired) && !c.gone.count(civ) && Str(entry, "why").empty())
				{
					no.push_back(at + (stale ? "something was done since you wrote this entry" : "this entry ran out")
						+ "; writing it again, changed or unchanged, needs a why: what it changes for you, or why it changes nothing");
					ok = false;
				}
				sent.insert(civ);
				if(!ok) continue;
				JObj e;
				e["e"] = J("relation"); e["turn"] = J(turn); e["civ"] = J(civ); e["text"] = J(Str(entry, "text")); e["until"] = J(until);
				if(!Str(entry, "why").empty()) e["why"] = J(Str(entry, "why"));
				JObj row;
				row["event"] = J("relation_entry"); row["turn"] = J(turn); row["about"] = J(civ); row["until"] = J(until);
				row["why"] = J(Str(entry, "why"));
				row["first"] = JVal(had == c.relations.end());
				if(had != c.relations.end())
				{
					row["unchanged"] = JVal(had->second.text == Str(entry, "text"));
					row["stale"] = JVal(stale); row["expired"] = JVal(expired);
					std::vector<const McpDeed*> since = DeedsSince(c, civ, had->second.deedsSeen);
					if(!since.empty()) row["answers"] = J(T(since.back()->turn) + " " + since.back()->text);
				}
				out.journal.push_back(JVal(row).serialize());
				events.push_back(e);
			}
	}
	for(std::map<std::string, McpRelation>::const_iterator it = c.relations.begin(); it != c.relations.end(); ++it)
	{
		if(sent.count(it->first) || c.gone.count(it->first)) continue;
		std::vector<const McpDeed*> since = DeedsSince(c, it->first, it->second.deedsSeen);
		if(!since.empty())
		{
			std::string what;
			size_t from = since.size() > 3 ? since.size() - 3 : 0;
			for(size_t i = from; i < since.size(); ++i) what += (i > from ? "; " : "") + T(since[i]->turn) + " " + since[i]->text;
			no.push_back("your entry on " + it->first + " was written on turn " + Num(it->second.written) + ", and since then: " + what
				+ (from ? " (and " + Num((int)from) + " before)" : "") + ". Write it again (plan.relations." + it->first
				+ " {text, until, why}), changed or unchanged, with why.");
		}
		else if(turn > it->second.until)
			no.push_back("your entry on " + it->first + " held until turn " + Num(it->second.until) + ". Write it again (plan.relations." + it->first
				+ " {text, until, why}), changed or unchanged, with why.");
	}

	// threads
	std::set<int> ending;
	if(const JObj* threads = Obj(args, "threads"))
	{
		if(const JArr* open = Arr(*threads, "open"))
			for(size_t i = 0; i < open->size(); ++i)
			{
				std::string at = "threads.open[" + Num((int)i + 1) + "]: ";
				if(!(*open)[i].is<JObj>()) { no.push_back(at + "a thread is {with, words, vow}"); continue; }
				const JObj& o = (*open)[i].get<JObj>();
				bool ok = true;
				if(Str(o, "with").empty() || Str(o, "words").empty()) { no.push_back(at + "a thread needs with (the player) and words (what you have sworn, claimed or are owed)"); ok = false; }
				else if(!Known(now, Str(o, "with"))) { no.push_back(at + "no player you have met is called " + Str(o, "with") + "; they are: " + Players(now)); ok = false; }
				JObj e;
				e["e"] = J("thread_open"); e["turn"] = J(turn); e["with"] = J(Str(o, "with")); e["words"] = J(Str(o, "words"));
				if(o.find("vow") != o.end())
				{
					const JObj* v = Obj(o, "vow");
					std::string kind = v ? Str(*v, "kind") : std::string();
					if(!ReachVow(kind) && !KeepVow(kind)) { no.push_back(at + "vow.kind is war, peace, pact, deal, take or hold"); ok = false; }
					else
					{
						if((kind == "take" || kind == "hold") && Str(*v, "subject").empty()) { no.push_back(at + "a vow to " + kind + " needs subject: the city"); ok = false; }
						if(!HasInt(*v, "by") || Int(*v, "by", -1) <= turn) { no.push_back(at + "vow.by is the turn it is due by or holds through, after this one"); ok = false; }
						JObj vow;
						vow["kind"] = J(kind); vow["by"] = J(Int(*v, "by", -1));
						if(kind == "take" || kind == "hold") vow["subject"] = J(Str(*v, "subject"));
						e["vow"] = JVal(vow);
					}
				}
				if(!ok) continue;
				JObj row = e;
				row.erase("e"); row["event"] = J("thread_open"); row["id"] = J((int)c.threads.size() + 1 + (int)i);
				out.journal.push_back(JVal(row).serialize());
				events.push_back(e);
			}
		if(const JArr* end = Arr(*threads, "end"))
			for(size_t i = 0; i < end->size(); ++i)
			{
				if(!(*end)[i].is<JObj>()) { no.push_back("threads.end: an ending is {id, ending}"); continue; }
				const JObj& o = (*end)[i].get<JObj>();
				int id = Int(o, "id", 0);
				McpThread* t = ThreadById(c, id);
				if(!t || t->ended >= 0) { no.push_back("thread " + Num(id) + ": no open thread has that id"); continue; }
				if(Str(o, "ending").empty()) { no.push_back("thread " + Num(id) + ": an ending needs words: how it ended"); continue; }
				ending.insert(id);
				JObj e;
				e["e"] = J("thread_end"); e["turn"] = J(turn); e["id"] = J(id); e["ending"] = J(Str(o, "ending"));
				JObj row = e;
				row.erase("e"); row["event"] = J("thread_end"); row["with"] = J(t->with); row["age"] = J(turn - t->opened);
				if(!t->vow.empty()) row["vow"] = J(t->vowState.empty() ? std::string("withdrawn") : t->vowState);
				out.journal.push_back(JVal(row).serialize());
				events.push_back(e);
			}
	}

	// chapter
	std::string due = McpChronicleDue(c, now.era, now.eliminations, now.citiesLost, turn);
	const JObj* chapter = Obj(args, "chapter");
	if(!chapter && !due.empty())
		no.push_back("a chapter is due (" + due + "). Close it: chapter {title, became: who you have become, threads: [{id, state, words}] for every open thread, "
			"nature: [{fault, ruled}] for every fault line of your first page (and {unwritten, settled} for an unwritten line this chapter settled), "
			"direction: written again from nothing, said: [{to, words}], which may be empty}");
	if(chapter)
	{
		if(Str(*chapter, "title").empty()) no.push_back("chapter.title is missing: the name of the chapter you are closing");
		if(Str(*chapter, "became").empty()) no.push_back("chapter.became is missing: who you have become");
		if(Str(*chapter, "direction").empty()) no.push_back("chapter.direction is missing: what your reign is for from here, written again from nothing");
		else if(Str(*chapter, "direction") == now.direction)
			no.push_back("chapter.direction is the direction you already hold, word for word: write it again for this age; if the course holds, say in it why it holds now");
		std::map<int, std::string> answered;
		JArr threads;
		if(const JArr* th = Arr(*chapter, "threads"))
			for(size_t i = 0; i < th->size(); ++i)
			{
				if(!(*th)[i].is<JObj>()) continue;
				const JObj& o = (*th)[i].get<JObj>();
				int id = Int(o, "id", 0);
				McpThread* t = ThreadById(c, id);
				if(!t || t->ended >= 0 || ending.count(id)) { no.push_back("chapter.threads: thread " + Num(id) + ": no open thread has that id"); continue; }
				if(!OneOf(Str(o, "state"), "kept", "broken", "owed") || Str(o, "words").empty())
				{
					no.push_back("chapter.threads: thread " + Num(id) + ": state is kept, broken or owed, with words");
					answered[id] = "";
					continue;
				}
				answered[id] = Str(o, "state");
				threads.push_back((*th)[i]);
			}
		for(size_t i = 0; i < c.threads.size(); ++i)
			if(c.threads[i].ended < 0 && !ending.count(c.threads[i].id) && !answered.count(c.threads[i].id))
				no.push_back("chapter.threads: thread " + Num(c.threads[i].id) + " (" + c.threads[i].with + ", \"" + c.threads[i].words
					+ "\") is open and unanswered: kept, broken or owed, with words");
		std::set<int> ruled;
		JArr nature;
		if(const JArr* n = Arr(*chapter, "nature"))
			for(size_t i = 0; i < n->size(); ++i)
			{
				if(!(*n)[i].is<JObj>()) continue;
				const JObj& o = (*n)[i].get<JObj>();
				if(o.find("fault") != o.end())
				{
					int f = Int(o, "fault", 0);
					if(f < 1 || f > (int)c.faults.size()) { no.push_back("chapter.nature: there is no fault " + Num(f)); continue; }
					if(!OneOf(Str(o, "ruled"), "first", "second", "both", "neither")) { no.push_back("chapter.nature: fault " + Num(f) + ": ruled is first, second, both or neither"); continue; }
					ruled.insert(f);
				}
				else if(o.find("unwritten") != o.end())
				{
					int u = Int(o, "unwritten", 0);
					if(u < 1 || u > (int)c.unwritten.size() || Str(o, "settled").empty())
					{
						no.push_back("chapter.nature: an unwritten line is settled as {unwritten: its number, settled: one sentence}");
						continue;
					}
				}
				else { no.push_back("chapter.nature: an entry is {fault, ruled} or {unwritten, settled}"); continue; }
				nature.push_back((*n)[i]);
			}
		for(size_t f = 1; f <= c.faults.size(); ++f)
			if(!ruled.count((int)f)) no.push_back("chapter.nature: fault " + Num((int)f) + " is unanswered: which half ruled you this age: first, second, both or neither");
		JArr said;
		if(const JArr* s = Arr(*chapter, "said"))
			for(size_t i = 0; i < s->size(); ++i)
			{
				if(!(*s)[i].is<JObj>() || Str((*s)[i].get<JObj>(), "to").empty() || Str((*s)[i].get<JObj>(), "words").empty())
				{
					no.push_back("chapter.said: a line is {to, words}");
					continue;
				}
				said.push_back((*s)[i]);
				out.say.push_back(std::make_pair(Str((*s)[i].get<JObj>(), "to"), Str((*s)[i].get<JObj>(), "words")));
			}
		JObj e;
		e["e"] = J("chapter"); e["turn"] = J(turn); e["reason"] = J(due.empty() ? std::string("the seat closed it") : due);
		e["title"] = J(Str(*chapter, "title")); e["became"] = J(Str(*chapter, "became")); e["direction"] = J(Str(*chapter, "direction"));
		e["threads"] = JVal(threads); e["nature"] = JVal(nature); e["said"] = JVal(said);
		e["era"] = J(now.era); e["eliminations"] = J(now.eliminations); e["citiesLost"] = J(now.citiesLost);
		JObj row = e;
		row.erase("e"); row["event"] = J("chapter_close"); row["number"] = J((int)c.chapters.size() + 1); row["opened"] = J(c.opened);
		int kept = 0, broken = 0, owed = 0;
		for(std::map<int, std::string>::const_iterator a = answered.begin(); a != answered.end(); ++a)
		{
			if(a->second == "kept") ++kept; else if(a->second == "broken") ++broken; else if(a->second == "owed") ++owed;
		}
		row["threads_kept"] = J(kept); row["threads_broken"] = J(broken); row["threads_owed"] = J(owed);
		out.journal.push_back(JVal(row).serialize());
		events.push_back(e);
		out.chapterClosed = true;
		out.direction = Str(*chapter, "direction");
	}

	if(!no.empty())
	{
		out.chapterClosed = false;
		out.direction.clear();
		out.say.clear();
		out.journal.clear();
		return false;
	}
	for(size_t i = 0; i < events.size(); ++i) Emit(c, events[i]);
	return true;
}

void McpVowsResolve(McpChronicle& c, const std::map<int, McpVowFact>& factsByThread, int turn)
{
	for(size_t i = 0; i < c.threads.size(); ++i)
	{
		const McpThread& t = c.threads[i];
		if(t.ended >= 0 || t.vow.empty() || !t.vowState.empty()) continue;
		std::map<int, McpVowFact>::const_iterator f = factsByThread.find(t.id);
		bool met = f != factsByThread.end() && f->second.met, failed = f != factsByThread.end() && f->second.failed;
		std::string state;
		if(ReachVow(t.vow)) state = met ? "kept" : turn > t.by ? "broken" : "";
		else state = failed ? "broken" : turn >= t.by ? "kept" : "";
		if(state.empty()) continue;
		JObj e;
		e["e"] = J("vow"); e["turn"] = J(turn); e["id"] = J(t.id); e["state"] = J(state);
		JObj row;
		row["event"] = J("vow_resolved"); row["turn"] = J(turn); row["id"] = J(t.id); row["with"] = J(t.with); row["vow"] = J(VowText(t));
		row["state"] = J(state); row["sworn"] = J(t.opened);
		c.journal.push_back(JVal(row).serialize());
		Emit(c, e);
	}
}

std::vector<std::string> McpChronicleMirror(McpChronicle& c, const std::vector<std::string>& served, int turn)
{
	std::vector<std::string> out;
	if(!c.started) return out;
	JObj e;
	e["e"] = J("round"); e["turn"] = J(turn); e["served"] = Strings(served);
	Emit(c, e);
	for(std::map<std::string, McpRelation>::const_iterator it = c.relations.begin(); it != c.relations.end(); ++it)
		if(it->second.unserved > 0 && !c.gone.count(it->first))
			out.push_back("your entry on " + it->first + ": no order and no say served it this round (" + Count(it->second.unserved, "round", "rounds") + " running)");
	for(size_t i = 0; i < c.threads.size(); ++i)
		if(c.threads[i].ended < 0 && c.threads[i].unserved > 0)
			out.push_back("thread " + Num(c.threads[i].id) + " (" + c.threads[i].with + ", \"" + c.threads[i].words + "\"): no order and no say served it this round ("
				+ Count(c.threads[i].unserved, "round", "rounds") + " running)");
	return out;
}

std::string McpChronicleBecame(const McpChronicle& c) { return c.chapters.empty() ? std::string() : c.chapters.back().became; }

std::string McpChronicleBlock(const McpChronicle& c, const std::string& civ, const std::string& firstPage, int turn, const std::string& due)
{
	if(!c.started) return "";
	std::string b = "THE CHRONICLE OF " + Upper(civ) + DOT + "turn " + Num(turn) + DOT + "chapter " + Num((int)c.chapters.size() + 1) + ", opened " + T(c.opened);
	if(!c.chapters.empty())
	{
		const McpChapter& last = c.chapters.back();
		b += " (chapter " + Num((int)c.chapters.size()) + " \"" + last.title + "\", " + T(last.opened) + "-" + T(last.closed) + ")";
	}
	b += "\n\nFIRST PAGE\n" + Indented(firstPage, "  ");
	if(!c.chapters.empty())
	{
		b += "\n\nWHO I HAVE BECOME (written " + T(c.chapters.back().closed) + ")\n" + Indented(c.chapters.back().became, "  ");
		for(size_t i = 0; i < c.settled.size(); ++i) if(!c.settled[i].empty()) b += "\n  " + c.settled[i];
	}
	if(!c.faults.empty())
	{
		b += "\n\nTHE NATURES, AGE BY AGE";
		for(size_t f = 0; f < c.faults.size(); ++f)
		{
			b += "\n  " + Num((int)f + 1) + ". " + c.faults[f] + "\n     ";
			static const char* const HALF[] = { "first", "second", "both", "neither" };
			int n[4] = { 0, 0, 0, 0 }, ages = 0;
			for(size_t k = 0; k < c.chapters.size(); ++k)
				for(int h = 0; h < 4; ++h)
					if(f < c.chapters[k].ruled.size() && c.chapters[k].ruled[f] == HALF[h]) { ++n[h]; ++ages; }
			if(!ages) { b += "no age closed yet"; continue; }
			bool first = true;
			for(int h = 0; h < 4; ++h)
			{
				if(!n[h]) continue;
				b += std::string(first ? "" : DOT) + HALF[h] + (h < 3 ? " ruled " : " ") + Num(n[h]) + (first ? " of " + Count(ages, "age", "ages") : "");
				first = false;
			}
		}
	}
	std::string unwritten;
	for(size_t i = 0; i < c.unwritten.size(); ++i)
		if(i >= c.settled.size() || c.settled[i].empty()) unwritten += "\n  " + Num((int)i + 1) + ". " + c.unwritten[i];
	if(!unwritten.empty()) b += "\n\nSTILL UNWRITTEN" + unwritten;

	// deeds the seat's entries do not cover, by player in the order they first appear
	std::vector<std::string> order;
	std::map<std::string, std::vector<const McpDeed*> > open;
	size_t width = 0;
	for(size_t i = 0; i < c.deeds.size(); ++i)
	{
		const std::string& civName = c.deeds[i].with;
		if(c.gone.count(civName) || open.count(civName)) continue;
		std::map<std::string, McpRelation>::const_iterator r = c.relations.find(civName);
		std::vector<const McpDeed*> since = DeedsSince(c, civName, r == c.relations.end() ? 0 : r->second.deedsSeen);
		if(since.empty()) continue;
		open[civName] = since;
		order.push_back(civName);
		if(civName.size() > width) width = civName.size();
	}
	if(!order.empty())
	{
		b += "\n\nDEEDS NOT YET IN YOUR RELATIONS";
		for(size_t i = 0; i < order.size(); ++i)
		{
			const std::vector<const McpDeed*>& since = open[order[i]];
			std::map<std::string, McpRelation>::const_iterator r = c.relations.find(order[i]);
			size_t from = since.size() > 5 ? since.size() - 5 : 0;
			for(size_t k = from; k < since.size(); ++k)
			{
				b += "\n  " + order[i] + std::string(width + 2 - order[i].size(), ' ') + T(since[k]->turn) + " " + Sentence(since[k]->text);
				if(k == from)
					b += r == c.relations.end() ? "   you have no entry on " + order[i] : "   your entry on " + order[i] + " was written " + T(r->second.written);
				if(k == from && from) b += " (" + Num((int)from) + " earlier not shown)";
			}
		}
	}
	std::string ranOut;
	for(std::map<std::string, McpRelation>::const_iterator it = c.relations.begin(); it != c.relations.end(); ++it)
		if(turn > it->second.until && !c.gone.count(it->first)) ranOut += "\n  " + it->first + "  held until " + T(it->second.until);
	if(!ranOut.empty()) b += "\n\nYOUR ENTRIES THAT HAVE RUN OUT" + ranOut;
	std::string threads;
	for(size_t i = 0; i < c.threads.size(); ++i)
	{
		const McpThread& t = c.threads[i];
		if(t.ended >= 0) continue;
		threads += "\n  #" + Num(t.id) + "  " + t.with + "  \"" + t.words + "\"  opened " + T(t.opened) + DOT + Count(turn - t.opened, "turn", "turns");
		if(t.chapters) threads += std::string(DOT) + "carried through " + Count(t.chapters, "chapter", "chapters");
		if(!t.vow.empty()) threads += std::string(DOT) + "vow: " + VowText(t) + (t.vowState.empty() ? "" : std::string(DOT) + t.vowState + " " + T(t.vowTurn));
	}
	if(!threads.empty()) b += "\n\nOPEN THREADS" + threads;
	if(!due.empty()) b += "\n\nCHAPTER DUE: " + due + " (plan_turn will ask for it)";
	return b;
}

picojson::value McpChronicleFull(const McpChronicle& c)
{
	JObj o;
	o["started"] = JVal(c.started);
	if(!c.started) return JVal(o);
	o["chapter"] = J((int)c.chapters.size() + 1);
	o["chapter_opened"] = J(c.opened);
	o["fault_lines"] = Strings(c.faults);
	o["unwritten"] = Strings(c.unwritten);
	o["settled"] = Strings(c.settled);
	JArr chapters;
	for(size_t i = 0; i < c.chapters.size(); ++i)
	{
		const McpChapter& ch = c.chapters[i];
		JObj co;
		co["number"] = J((int)i + 1); co["opened"] = J(ch.opened); co["closed"] = J(ch.closed); co["why_closed"] = J(ch.reason);
		co["title"] = J(ch.title); co["became"] = J(ch.became); co["direction"] = J(ch.direction); co["ruled"] = Strings(ch.ruled);
		JArr said;
		for(size_t k = 0; k < ch.said.size(); ++k) { JObj s; s["to"] = J(ch.said[k].first); s["words"] = J(ch.said[k].second); said.push_back(JVal(s)); }
		co["said"] = JVal(said);
		chapters.push_back(JVal(co));
	}
	o["chapters"] = JVal(chapters);
	JArr threads;
	for(size_t i = 0; i < c.threads.size(); ++i)
	{
		const McpThread& t = c.threads[i];
		JObj to;
		to["id"] = J(t.id); to["with"] = J(t.with); to["words"] = J(t.words); to["opened"] = J(t.opened);
		if(t.ended >= 0) { to["ended"] = J(t.ended); to["ending"] = J(t.ending); }
		if(t.chapters) to["carried_through_chapters"] = J(t.chapters);
		if(t.unserved) to["rounds_unserved"] = J(t.unserved);
		if(!t.vow.empty()) { to["vow"] = J(VowText(t)); if(!t.vowState.empty()) to["vow_state"] = J(t.vowState + " " + T(t.vowTurn)); }
		threads.push_back(JVal(to));
	}
	o["threads"] = JVal(threads);
	JObj relations;
	for(std::map<std::string, McpRelation>::const_iterator it = c.relations.begin(); it != c.relations.end(); ++it)
	{
		JObj r;
		r["text"] = J(it->second.text); r["written"] = J(it->second.written); r["until"] = J(it->second.until);
		if(it->second.unserved) r["rounds_unserved"] = J(it->second.unserved);
		if(c.gone.count(it->first)) r["left_the_game"] = JVal(true);
		relations[it->first] = JVal(r);
	}
	o["relations"] = JVal(relations);
	JObj ledger;
	for(size_t i = 0; i < c.deeds.size(); ++i)
	{
		JVal& list = ledger[c.deeds[i].with];
		if(!list.is<JArr>()) list = JVal(JArr());
		list.get<JArr>().push_back(J(T(c.deeds[i].turn) + " " + c.deeds[i].text));
	}
	o["ledger"] = JVal(ledger);
	return JVal(o);
}

std::string McpChronicleRelationsText(const McpChronicle& c)
{
	std::string out;
	for(std::map<std::string, McpRelation>::const_iterator it = c.relations.begin(); it != c.relations.end(); ++it)
		if(!c.gone.count(it->first)) out += (out.empty() ? "" : " ") + it->first + " (holds until " + T(it->second.until) + "): " + Sentence(it->second.text);
	return out;
}
