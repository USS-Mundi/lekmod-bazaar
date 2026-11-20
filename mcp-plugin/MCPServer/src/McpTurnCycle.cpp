#include "McpTurnCycle.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace
{
	const int NEAR = 5;            // another civilization's unit within this many tiles of yours is worth a line
	const int GOLD_SHOWN = 50;     // a smaller move in the treasury is a turn's income or upkeep, not news
	const int HAPPINESS_SHOWN = 3;

	const char* const WHY_KINDS[] = { "enemy", "attack", "numbers", "tile", "refused", "opportunity", "mistake", "other" };

	std::string Num(int n)
	{
		std::ostringstream s;
		s << n;
		return s.str();
	}

	std::string At(int x, int y) { return "(" + Num(x) + "," + Num(y) + ")"; }

	std::string Near(const McpSeenUnit& u)
	{
		if(u.nearDist < 0) return "";
		return ", " + Num(u.nearDist) + (u.nearDist == 1 ? " tile from " : " tiles from ") + u.nearWhat;
	}

	std::string Name(const McpSeenUnit& u) { return (u.enemy ? "enemy " : "") + u.who + " " + u.type; }

	const McpSeenUnit* FindUnit(const std::vector<McpSeenUnit>& units, int id)
	{
		for(size_t i = 0; i < units.size(); ++i) if(units[i].id == id) return &units[i];
		return NULL;
	}

	const McpSeenCity* FindCity(const std::vector<McpSeenCity>& cities, int id)
	{
		for(size_t i = 0; i < cities.size(); ++i) if(cities[i].id == id) return &cities[i];
		return NULL;
	}

	bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

	// Others' units: an enemy always, anyone else when close; closest first, an enemy ahead at the same distance.
	struct Sighting
	{
		int dist;
		bool enemy;
		std::string line;
	};

	bool Sooner(const Sighting& a, const Sighting& b)
	{
		int da = a.dist < 0 ? 1 << 20 : a.dist, db = b.dist < 0 ? 1 << 20 : b.dist;
		if(da != db) return da < db;
		return a.enemy && !b.enemy;
	}
}

namespace
{
	// The changes between two views; bWakingOnly leaves out the drift a seat's own turn brings: gold that did not run
	// out and happiness that stayed on its side of zero.
	std::vector<std::string> Changes(const McpView& planned, const McpView& now, size_t limit, bool bWakingOnly)
	{
	std::vector<std::string> out;
	for(size_t i = 0; i < now.atWarWith.size(); ++i)
		if(!Contains(planned.atWarWith, now.atWarWith[i])) out.push_back("now at war with " + now.atWarWith[i]);
	for(size_t i = 0; i < planned.atWarWith.size(); ++i)
		if(!Contains(now.atWarWith, planned.atWarWith[i])) out.push_back("now at peace with " + planned.atWarWith[i]);

	for(size_t i = 0; i < planned.cities.size(); ++i)
		if(!FindCity(now.cities, planned.cities[i].id)) out.push_back("lost: " + planned.cities[i].name);
	for(size_t i = 0; i < planned.mine.size(); ++i)
		if(!FindUnit(now.mine, planned.mine[i].id))
			out.push_back("lost: your " + planned.mine[i].type + " (unit " + Num(planned.mine[i].id) + "), last at " + At(planned.mine[i].x, planned.mine[i].y));

	for(size_t i = 0; i < now.cities.size(); ++i)
	{
		const McpSeenCity* before = FindCity(planned.cities, now.cities[i].id);
		if(before && now.cities[i].hp < before->hp)
			out.push_back(now.cities[i].name + " took " + Num(before->hp - now.cities[i].hp) + " damage (now " + Num(now.cities[i].hp) + " of " + Num(now.cities[i].maxHp) + ")");
	}
	for(size_t i = 0; i < now.mine.size(); ++i)
	{
		const McpSeenUnit* before = FindUnit(planned.mine, now.mine[i].id);
		if(before && now.mine[i].hp < before->hp)
			out.push_back("your " + now.mine[i].type + " at " + At(now.mine[i].x, now.mine[i].y) + " took " + Num(before->hp - now.mine[i].hp) + " damage (now " + Num(now.mine[i].hp) + ")");
	}

	std::vector<Sighting> seen;
	for(size_t i = 0; i < now.theirs.size(); ++i)
	{
		const McpSeenUnit& u = now.theirs[i];
		if(!u.enemy && (u.cityState || u.nearDist < 0 || u.nearDist > NEAR)) continue;
		const McpSeenUnit* before = FindUnit(planned.theirs, u.id);
		Sighting s;
		s.dist = u.nearDist;
		s.enemy = u.enemy;
		if(!before) s.line = "new in sight: " + Name(u) + " at " + At(u.x, u.y) + Near(u);
		else if(before->x != u.x || before->y != u.y) s.line = Name(u) + " moved " + At(before->x, before->y) + " to " + At(u.x, u.y) + Near(u);
		else continue;
		seen.push_back(s);
	}
	std::stable_sort(seen.begin(), seen.end(), Sooner);
	for(size_t i = 0; i < seen.size(); ++i) out.push_back(seen[i].line);

	int goldMoved = now.gold - planned.gold, happyMoved = now.happiness - planned.happiness;
	bool bEmptied = now.gold == 0 && planned.gold > 0, bCrossed = (now.happiness < 0) != (planned.happiness < 0);
	if(bEmptied || (!bWakingOnly && (goldMoved >= GOLD_SHOWN || -goldMoved >= GOLD_SHOWN)))
		out.push_back("gold " + Num(now.gold) + " (was " + Num(planned.gold) + ")");
	if(bCrossed || (!bWakingOnly && (happyMoved >= HAPPINESS_SHOWN || -happyMoved >= HAPPINESS_SHOWN)))
		out.push_back("happiness " + Num(now.happiness) + " (was " + Num(planned.happiness) + ")");

	if(out.size() > limit)
	{
		size_t more = out.size() - limit;
		out.resize(limit);
		out.push_back("...and " + Num((int)more) + " more");
	}
	return out;
	}
}

std::vector<std::string> McpViewChanges(const McpView& planned, const McpView& now, size_t limit) { return Changes(planned, now, limit, false); }

std::vector<std::string> McpWakingChanges(const McpView& planned, const McpView& now, size_t limit) { return Changes(planned, now, limit, true); }

bool McpRoundOpen(int gameTurn, int lastEndedTurn) { return lastEndedTurn != gameTurn; }

std::string McpAutoRunHold(bool bSwitchedOff, bool bPlannedOnThisRoundsView, const std::vector<std::string>& waking, int diploRequests)
{
	if(bSwitchedOff) return "you check it against what changed, then run_plan";
	if(!bPlannedOnThisRoundsView) return "the plan was not made from this round's turn view";
	if(diploRequests > 0) return "another leader is waiting for your answer";
	if(waking.empty()) return "";
	return "since you planned: " + waking[0] + (waking.size() > 1 ? ", and " + Num((int)waking.size() - 1) + " more" : std::string());
}

std::vector<McpPlannedOrder> McpOrdersToRun(const std::vector<McpPlannedOrder>& plan, const std::vector<int>& drop,
	const std::vector<McpPlannedOrder>& add, std::string& err)
{
	std::vector<McpPlannedOrder> run;
	err.clear();
	for(size_t i = 0; i < drop.size(); ++i)
		if(drop[i] < 1 || drop[i] > (int)plan.size())
		{
			err = "order " + Num(drop[i]) + " is not in the plan (it has " + Num((int)plan.size()) + ")";
			return run;
		}
	for(size_t i = 0; i < plan.size(); ++i)
		if(std::find(drop.begin(), drop.end(), (int)i + 1) == drop.end()) run.push_back(plan[i]);
	bool bEnds = !run.empty() && run.back().tool == "end_turn";
	run.insert(bEnds ? run.end() - 1 : run.end(), add.begin(), add.end());
	return run;
}

bool McpIsWhyKind(const std::string& kind)
{
	for(size_t i = 0; i < sizeof(WHY_KINDS) / sizeof(WHY_KINDS[0]); ++i) if(kind == WHY_KINDS[i]) return true;
	return false;
}

std::string McpWhyKinds()
{
	std::string s;
	for(size_t i = 0; i < sizeof(WHY_KINDS) / sizeof(WHY_KINDS[0]); ++i) s += (i ? ", " : "") + std::string(WHY_KINDS[i]);
	return s;
}

const std::vector<std::string>& McpGamePlanHeadings()
{
	static std::vector<std::string> headings;
	if(headings.empty())
	{
		const char* const names[] = { "position", "direction", "next_turns", "cities", "military", "civilians", "research", "policy", "relations" };
		headings.assign(names, names + sizeof(names) / sizeof(names[0]));
	}
	return headings;
}

namespace
{
	const int PLAN_WORDS_SHOWN = 1200;
	const size_t PLAN_LOG_SHOWN = 8;
	const size_t PLAN_LOG_KEPT = 40;

	int Words(const std::string& s)
	{
		int n = 0;
		bool in = false;
		for(size_t i = 0; i < s.size(); ++i)
		{
			bool space = s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r';
			if(!space && !in) ++n;
			in = !space;
		}
		return n;
	}

	std::string Title(const std::string& heading)
	{
		std::string t = heading;
		if(!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = (char)(t[0] - 'a' + 'A');
		return t;
	}
}

bool McpGamePlanApply(McpGamePlan& plan, const std::map<std::string, std::string>& changes, const std::string& why,
	int turn, std::string& err, std::vector<std::string>& warnings)
{
	const std::vector<std::string>& headings = McpGamePlanHeadings();
	for(std::map<std::string, std::string>::const_iterator it = changes.begin(); it != changes.end(); ++it)
		if(std::find(headings.begin(), headings.end(), it->first) == headings.end())
		{
			std::string all;
			for(size_t i = 0; i < headings.size(); ++i) all += (i ? ", " : "") + headings[i];
			err = "'" + it->first + "' is not a heading of the plan; use " + all;
			return false;
		}
	McpGamePlan next = plan;
	bool bChanged = false;
	for(std::map<std::string, std::string>::const_iterator it = changes.begin(); it != changes.end(); ++it)
	{
		if(next.sections[it->first] == it->second) continue;
		next.sections[it->first] = it->second;
		next.written[it->first] = turn;
		bChanged = true;
	}
	if(!bChanged) return true;
	bool bFirst = plan.turn < 0;
	if(bFirst && next.sections["direction"].empty())
	{
		err = "a first plan needs at least direction: how you are optimizing your odds of winning from here";
		return false;
	}
	if(!bFirst && why.empty())
	{
		err = "changing the plan needs why: what made you change it, in a few words; it goes into the plan's log";
		return false;
	}
	next.turn = turn;
	next.log.push_back("T" + Num(turn) + ": " + (bFirst && why.empty() ? std::string("first plan") : why));
	if(next.log.size() > PLAN_LOG_KEPT) next.log.erase(next.log.begin(), next.log.end() - PLAN_LOG_KEPT);
	int words = 0;
	for(size_t i = 0; i < headings.size(); ++i) words += Words(next.sections[headings[i]]);
	if(words > PLAN_WORDS_SHOWN)
		warnings.push_back("the plan is " + Num(words) + " words; past about " + Num(PLAN_WORDS_SHOWN) + " it is long to read every turn");
	plan = next;
	return true;
}

namespace
{
	std::string Section(const McpGamePlan& plan, const std::string& heading)
	{
		std::map<std::string, std::string>::const_iterator it = plan.sections.find(heading);
		return it == plan.sections.end() ? std::string() : it->second;
	}

	std::string Written(const McpGamePlan& plan, const std::string& heading)
	{
		std::map<std::string, int>::const_iterator it = plan.written.find(heading);
		return it == plan.written.end() ? std::string() : " (T" + Num(it->second) + ")";
	}
}

std::string McpGamePlanText(const McpGamePlan& plan)
{
	std::string out;
	const std::vector<std::string>& headings = McpGamePlanHeadings();
	for(size_t i = 0; i < headings.size(); ++i)
	{
		const std::string& h = headings[i];
		std::string text = Section(plan, h);
		out += text.empty() ? Title(h) + ": none written\n" : Title(h) + Written(plan, h) + ": " + text + "\n";
	}
	if(!plan.log.empty())
	{
		out += "Log:\n";
		size_t from = plan.log.size() > PLAN_LOG_SHOWN ? plan.log.size() - PLAN_LOG_SHOWN : 0;
		for(size_t i = from; i < plan.log.size(); ++i) out += "  " + plan.log[i] + "\n";
	}
	return out;
}

std::string McpGamePlanLine(const McpGamePlan& plan, const std::string& heading)
{
	std::string text = Section(plan, heading);
	return text.empty() ? std::string("plan: none written") : "plan" + Written(plan, heading) + ": " + text;
}

std::string McpGamePlanBlock(const McpGamePlan& plan, int currentTurn)
{
	std::string out;
	const std::vector<std::string>& headings = McpGamePlanHeadings();
	for(size_t i = 0; i < headings.size(); ++i)
	{
		const std::string& h = headings[i];
		std::string text = Section(plan, h);
		if(text.empty()) { out += "  " + h + ": none written\n"; continue; }
		std::map<std::string, int>::const_iterator w = plan.written.find(h);
		std::string age;
		if(w != plan.written.end())
		{
			int ago = currentTurn - w->second;
			age = " (T" + Num(w->second) + ", " + (ago <= 0 ? std::string("this turn") : ago == 1 ? std::string("1 turn ago") : Num(ago) + " turns ago") + ")";
		}
		out += "  " + h + age + ": " + text + "\n";
	}
	return out;
}

int McpLastPlanTurn(const McpGamePlan& plan)
{
	int last = -1;
	for(std::map<std::string, int>::const_iterator it = plan.written.begin(); it != plan.written.end(); ++it)
		if(!Section(plan, it->first).empty() && it->second > last) last = it->second;
	return last;
}

std::string McpTurnFramingTop(int lastPlanTurn)
{
	const std::string rest = " This view is a starting point: it is not everything, and nothing in it has to be done.";
	if(lastPlanTurn < 0)
		return "You have no plan yet. What is your position, and how will you optimize your odds of winning from it?" + rest;
	return "Your plan was last revised on turn " + Num(lastPlanTurn) + ". Read it top to bottom against what has changed: position, "
		"direction, next turns, each area, the orders. Does each part follow from the one above it, and does any part contradict "
		"another or where you stand? Then hold each part, adjust it or drop it, and fit the orders to it." + rest;
}

std::string McpTurnFramingBottom()
{
	return "What here bears on your plan, and what do you need to know that this view does not show?";
}

namespace
{
	bool Starts(const std::string& s, const char* prefix)
	{
		return s.compare(0, std::string(prefix).size(), prefix) == 0;
	}
}

McpDecisionKindType McpDecisionKind(const std::string& line)
{
	if(Starts(line, "end_turn is blocked") || Starts(line, "production:") || Starts(line, "research:") || Starts(line, "free_tech:")
		|| Starts(line, "captured city:"))
		return MCP_DECISION_BLOCKS;
	if(Starts(line, "diplomacy:")) return MCP_DECISION_FROM_OTHERS;
	return MCP_DECISION_AVAILABLE;
}

std::string McpTrimNews(const std::string& line)
{
	const std::string mark = "text Researched ";
	size_t at = line.find(mark);
	if(at == std::string::npos) return line;
	size_t colon = line.find(':', at + mark.size());
	if(colon == std::string::npos) return line;
	size_t end = line.find(" · ", colon);
	return line.substr(0, colon) + (end == std::string::npos ? std::string() : line.substr(end));
}

std::vector<std::string> McpCivCodes(const std::vector<std::string>& names)
{
	std::vector<std::string> out;
	std::set<std::string> used;
	for(size_t i = 0; i < names.size(); ++i)
	{
		std::string letters;
		for(size_t k = 0; k < names[i].size(); ++k)
		{
			char c = names[i][k];
			if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			if(c >= 'A' && c <= 'Z') letters += c;
		}
		while(letters.size() < 3) letters += 'X';
		std::string code = letters.substr(0, 3);
		for(size_t n = 3; used.count(code) && n < letters.size(); ++n) code = letters.substr(0, 2) + letters[n];
		for(int d = 2; used.count(code); ++d) code = letters.substr(0, 2) + Num(d % 10);
		used.insert(code);
		out.push_back(code);
	}
	return out;
}

std::string McpUnitStats(int hp, int strength, int ranged, int range, const std::string& moves, int level)
{
	std::string out = "[" + Num(hp);
	if(strength > 0) out += " " + Num(strength);
	if(ranged > 0) out += " r" + Num(ranged) + "/" + Num(range);
	out += " " + moves;
	if(level >= 0) out += " L" + Num(level);
	return out + "]";
}

void McpMigratePlan(McpGamePlan& plan)
{
	const char* const earlier[] = { "read", "arc", "thrust", "tripwires" };
	std::string direction;
	bool bAny = false;
	for(size_t i = 0; i < sizeof(earlier) / sizeof(earlier[0]); ++i)
	{
		std::map<std::string, std::string>::iterator it = plan.sections.find(earlier[i]);
		if(it == plan.sections.end()) continue;
		bAny = true;
		std::string text = it->second;
		if(!text.empty())
		{
			if(text[text.size() - 1] != '.') text += ".";
			direction += (direction.empty() ? "" : " ") + Title(earlier[i]) + ": " + text;
		}
		plan.sections.erase(it);
	}
	std::map<std::string, std::string>::iterator rivals = plan.sections.find("rivals");
	std::string relations;
	if(rivals != plan.sections.end())
	{
		bAny = true;
		relations = rivals->second;
		plan.sections.erase(rivals);
	}
	if(!bAny) return;
	if(!direction.empty() && Section(plan, "direction").empty()) { plan.sections["direction"] = direction; plan.written["direction"] = plan.turn; }
	if(!relations.empty() && Section(plan, "relations").empty()) { plan.sections["relations"] = relations; plan.written["relations"] = plan.turn; }
}

std::vector<std::string> McpWithoutAPlan(const McpGamePlan& plan, const std::vector<std::string>& cities)
{
	std::vector<std::string> out;
	const std::vector<std::string>& headings = McpGamePlanHeadings();
	for(size_t i = 0; i < headings.size(); ++i)
		if(Section(plan, headings[i]).empty()) out.push_back(headings[i]);
	std::string citiesPlan = Section(plan, "cities");
	if(!citiesPlan.empty())
		for(size_t i = 0; i < cities.size(); ++i)
			if(citiesPlan.find(cities[i]) == std::string::npos) out.push_back(cities[i] + " in the cities plan");
	return out;
}

namespace
{
	// A field shown by its value alone: the thing's name or type, a clock's body, and any field named "_...".
	bool Unlabeled(const std::string& f) { return f == "name" || f == "type" || f == "clock" || (!f.empty() && f[0] == '_'); }

	// The item as one line; a note after a field (what it was) goes right after its value.
	std::string Render(const McpViewItem& item, const std::map<std::string, std::string>& notes = std::map<std::string, std::string>())
	{
		std::string out;
		for(size_t i = 0; i < item.fields.size(); ++i)
		{
			const std::string& f = item.fields[i].first;
			if(i) out += " · ";
			out += Unlabeled(f) ? item.fields[i].second : f + " " + item.fields[i].second;
			std::map<std::string, std::string>::const_iterator n = notes.find(f);
			if(n != notes.end()) out += " " + n->second;
		}
		return out;
	}

	bool IsListText(const std::string& v)
	{
		return v.size() >= 2 && v[0] == '[' && v[v.size() - 1] == ']';
	}

	// The elements of a "[...]" list: split at the top level on "; " when it has one, else on ", ".
	std::vector<std::string> ListElements(const std::string& v)
	{
		std::string inner = v.substr(1, v.size() - 2);
		bool bSemi = false;
		int depth = 0;
		for(size_t i = 0; i + 1 < inner.size(); ++i)
		{
			char c = inner[i];
			if(c == '[' || c == '{' || c == '(') ++depth;
			else if(c == ']' || c == '}' || c == ')') --depth;
			else if(depth == 0 && c == ';' && inner[i + 1] == ' ') bSemi = true;
		}
		char sep = bSemi ? ';' : ',';
		std::vector<std::string> out;
		std::string cur;
		depth = 0;
		for(size_t i = 0; i < inner.size(); ++i)
		{
			char c = inner[i];
			if(c == '[' || c == '{' || c == '(') ++depth;
			else if(c == ']' || c == '}' || c == ')') --depth;
			if(depth == 0 && c == sep && i + 1 < inner.size() && inner[i + 1] == ' ') { out.push_back(cur); cur.clear(); ++i; continue; }
			cur += c;
		}
		if(!cur.empty()) out.push_back(cur);
		return out;
	}

	std::string ListChange(const std::string& before, const std::string& now)
	{
		std::vector<std::string> a = ListElements(before), b = ListElements(now);
		std::string removed, added;
		for(size_t i = 0; i < a.size(); ++i)
			if(std::find(b.begin(), b.end(), a[i]) == b.end()) removed += (removed.empty() ? "" : "; ") + a[i];
		for(size_t i = 0; i < b.size(); ++i)
			if(std::find(a.begin(), a.end(), b[i]) == a.end()) added += (added.empty() ? "" : "; ") + b[i];
		std::string out;
		if(!removed.empty()) out += "removed " + removed;
		if(!added.empty()) out += (out.empty() ? "" : "; ") + std::string("added ") + added;
		return out.empty() ? std::string("reordered") : out;
	}

	// What changed, outside the ticking fields: a note for each changed field to follow its value ("(was 3)", for a list
	// "(was: removed ...; added ...)", "(new)" for a field it did not have), and the fields it no longer has as
	// "<field> <old value>". False when nothing changed.
	bool Was(const McpViewItem& before, const McpViewItem& now, const std::set<std::string>& ticking,
		std::map<std::string, std::string>& notes, std::string& gone)
	{
		std::map<std::string, std::string> oldValues, newValues;
		for(size_t i = 0; i < before.fields.size(); ++i) oldValues[before.fields[i].first] = before.fields[i].second;
		for(size_t i = 0; i < now.fields.size(); ++i) newValues[now.fields[i].first] = now.fields[i].second;
		bool bChanged = false;
		for(size_t i = 0; i < now.fields.size(); ++i)
		{
			const std::string& f = now.fields[i].first;
			if(ticking.count(f)) continue;
			std::map<std::string, std::string>::const_iterator o = oldValues.find(f);
			if(o == oldValues.end()) { notes[f] = "(new)"; bChanged = true; continue; }
			if(o->second == now.fields[i].second) continue;
			notes[f] = IsListText(o->second) && IsListText(now.fields[i].second)
				? "(was: " + ListChange(o->second, now.fields[i].second) + ")" : "(was " + o->second + ")";
			bChanged = true;
		}
		for(size_t i = 0; i < before.fields.size(); ++i)
		{
			const std::string& f = before.fields[i].first;
			if(ticking.count(f) || newValues.count(f)) continue;
			gone += (gone.empty() ? "" : "; ") + (Unlabeled(f) ? before.fields[i].second : f + " " + before.fields[i].second);
			bChanged = true;
		}
		return bChanged;
	}

	McpViewSection& SectionNamed(std::vector<McpViewSection>& view, const std::string& name)
	{
		for(size_t i = 0; i < view.size(); ++i) if(view[i].name == name) return view[i];
		McpViewSection s;
		s.name = name;
		view.push_back(s);
		return view.back();
	}
}

std::vector<McpViewSection> McpMarkView(const std::vector<McpViewItem>& before, const std::vector<McpViewItem>& now,
	const std::set<std::string>& ticking, bool bHasBefore)
{
	std::map<std::string, size_t> was, is;
	for(size_t i = 0; i < before.size(); ++i) was[before[i].key] = i;
	for(size_t i = 0; i < now.size(); ++i) is[now[i].key] = i;
	std::vector<McpViewSection> view;
	for(size_t i = 0; i < now.size(); ++i)
	{
		McpViewSection& s = SectionNamed(view, now[i].section);
		std::string line = Render(now[i]);
		std::map<std::string, size_t>::const_iterator old = was.find(now[i].key);
		if(!bHasBefore) s.lines.push_back("  " + line);
		else if(old == was.end()) s.lines.push_back("+ " + line);
		else
		{
			std::map<std::string, std::string> notes;
			std::string gone;
			if(!Was(before[old->second], now[i], ticking, notes, gone)) s.lines.push_back("  " + line);
			else s.lines.push_back("~ " + Render(now[i], notes) + (gone.empty() ? std::string() : "  (gone: " + gone + ")"));
		}
	}
	if(bHasBefore)
		for(size_t i = 0; i < before.size(); ++i)
			if(!is.count(before[i].key)) SectionNamed(view, before[i].section).lines.push_back("- " + Render(before[i]));
	return view;
}

int McpTurnsToAfford(int cost, int gold, int rate)
{
	if(gold >= cost) return 0;
	if(rate <= 0) return -1;
	return (cost - gold + rate - 1) / rate;
}

namespace
{
	bool HpOf(const McpViewItem& it, int& hp)
	{
		for(size_t i = 0; i < it.fields.size(); ++i)
			if(it.fields[i].first == "hp") { hp = atoi(it.fields[i].second.c_str()); return true; }
		return false;
	}
}

void McpAddFallClocks(const std::vector<McpViewItem>& before, std::vector<McpViewItem>& now)
{
	std::map<std::string, const McpViewItem*> was;
	for(size_t i = 0; i < before.size(); ++i) was[before[i].key] = &before[i];
	for(size_t i = 0; i < now.size(); ++i)
	{
		McpViewItem& it = now[i];
		if(it.section != "cities" && it.section != "their cities") continue;
		int hpNow = 0, hpBefore = 0;
		std::map<std::string, const McpViewItem*>::const_iterator w = was.find(it.key);
		if(!HpOf(it, hpNow) || w == was.end() || !HpOf(*w->second, hpBefore) || hpNow >= hpBefore) continue;
		int drop = hpBefore - hpNow;
		std::string body = "-" + Num(drop) + " since your last turn: ";
		if(hpNow <= 0) body += "at 0, a melee unit can take it now";
		else body += "0 in " + Num((hpNow + drop - 1) / drop) + "t at this rate; at 0 a melee unit can take it";
		it.fields.push_back(std::make_pair(std::string("falling"), body));
	}
}

McpEndTurnState McpEndTurnOutcome(bool bTurnOver, bool bMyTurn)
{
	if(bTurnOver) return MCP_TURN_ENDED;
	return bMyTurn ? MCP_TURN_HANDED_BACK : MCP_TURN_ENDING;
}

bool McpEndTurnAgain(bool bBlocked, int iUnread, int iTimesAgain)
{
	return !bBlocked && iUnread == 0 && iTimesAgain == 0;
}

std::string McpPlanEndHeld(bool bOrderFailed, int iMessagesCame)
{
	if(bOrderFailed) return "not run: an order before it failed, so the turn is still yours";
	if(iMessagesCame > 0) return "not run: " + Num(iMessagesCame) + " game message(s) came while your orders ran (messages_while_running), so the turn is still yours";
	return "";
}

const char* const MCP_ALREADY_RESTING = "the unit is already resting (fortified, asleep or on alert)";
const char* const MCP_CITY_HAS_IT = "the city already has it";
const char* const MCP_TEAM_HAS_IT = "your team already has it";
const char* const MCP_KNOWN_TECH = "you already know ";
const char* const MCP_HAVE_POLICY = "you already have that policy";
const char* const MCP_NO_MOVES_LEFT = "this unit has no moves left this turn; try again next turn";
const char* const MCP_NO_NEW_UNIT = "finished no new unit this turn, so this order has nothing to move";

bool McpOrderAlreadyDone(const std::string& tool, const std::string& err)
{
	if(tool == "end_turn" || tool == "attack" || tool == "city_attack") return false;
	if(err.find(MCP_NO_MOVES_LEFT) != std::string::npos) return tool == "unit_action";
	const char* const already[] = { MCP_ALREADY_RESTING, MCP_CITY_HAS_IT, MCP_TEAM_HAS_IT, MCP_KNOWN_TECH, MCP_HAVE_POLICY, MCP_NO_NEW_UNIT };
	for(size_t i = 0; i < sizeof(already) / sizeof(already[0]); ++i)
		if(err.find(already[i]) != std::string::npos) return true;
	return false;
}

std::string McpOrderDoneNote(const std::string& err)
{
	return err.find(MCP_NO_MOVES_LEFT) != std::string::npos ? "skipped, no moves left this turn" : "already so";
}

McpNewUnitPick McpNewUnitType(const std::vector<int>& made, int named, int& type)
{
	if(made.empty()) return MCP_NEW_UNIT_NONE;
	if(named >= 0)
	{
		for(size_t i = 0; i < made.size(); ++i) if(made[i] == named) { type = named; return MCP_NEW_UNIT_OK; }
		return MCP_NEW_UNIT_NOT_MADE;
	}
	for(size_t i = 1; i < made.size(); ++i) if(made[i] != made[0]) return MCP_NEW_UNIT_WHICH;
	type = made[0];
	return MCP_NEW_UNIT_OK;
}

int McpPickNewUnit(const std::vector<std::pair<int, int> >& candidates, int nth)
{
	if(nth < 1 || nth > (int)candidates.size()) return -1;
	std::vector<std::pair<std::pair<int, int>, int> > order;
	for(size_t i = 0; i < candidates.size(); ++i) order.push_back(std::make_pair(candidates[i], (int)i));
	std::sort(order.begin(), order.end());
	return order[nth - 1].second;
}

McpBuildStep McpChooseBuildStep(bool bCanHere, bool bQueuedMoveEndsElsewhere, bool bCanThere)
{
	if(bCanHere) return MCP_BUILD_HERE;
	return (bQueuedMoveEndsElsewhere && bCanThere) ? MCP_BUILD_ON_ARRIVAL : MCP_BUILD_REFUSED;
}

bool McpPathFaultRefuses(McpPathFault eFault)
{
	return eFault != MCP_PATH_OK && eFault != MCP_PATH_UNEXPLORED;
}

std::vector<int> McpQueueMoveSwaps(int iFrom, int iTo)
{
	std::vector<int> out;
	for(int i = iFrom; i > iTo; --i) out.push_back(i - 1);
	for(int i = iFrom; i < iTo; ++i) out.push_back(i);
	return out;
}

std::string McpQueuePositionFault(int iPos, int n, bool bInsert)
{
	int last = bInsert ? n + 1 : n;
	if(last < 1) return "the queue is empty";
	if(iPos >= 1 && iPos <= last) return "";
	return "the queue has " + Num(n) + " items: give a position from 1 to " + Num(last);
}

std::vector<std::string> McpOpeningOwed(const McpOpening& o, const McpPlanCovers& c, bool bTurnOpen)
{
	std::vector<std::string> out;
	for(size_t i = 0; i < o.idle.size(); ++i)
		if(!c.units.count(o.idle[i].first)) out.push_back(o.idle[i].second);
	for(size_t i = 0; i < o.cities.size(); ++i)
	{
		const McpOpeningCity& city = o.cities[i];
		if(city.nothing)
		{
			if(!c.cities.count(city.id)) out.push_back(city.name + " has nothing to build");
			continue;
		}
		if(bTurnOpen || !city.finishes || city.process) continue;
		if(city.unit && !c.newUnits.count(city.id))
			out.push_back(city.name + " finishes " + city.item + " as the turn opens: give the new unit its orders in this plan, with from_city "
				+ Num(city.id) + " in place of unit_id");
		if(!city.queuedAfter && !c.cities.count(city.id))
			out.push_back(city.name + " finishes " + city.item + " as the turn opens, with nothing queued after it: queue what comes next");
	}
	if(!c.research)
	{
		if(o.researchNone) out.push_back("research: nothing chosen");
		else if(!bTurnOpen && o.researchFinishes && !o.researchAfter)
			out.push_back("research: " + o.research + " finishes as the turn opens, with nothing queued after it: set what comes next");
	}
	return out;
}

int McpPlanMadeOnTurn(int gameTurn, bool bMyTurn, int lastEndedTurn)
{
	if(bMyTurn || lastEndedTurn < 0 || lastEndedTurn > gameTurn) return gameTurn;
	return lastEndedTurn;
}

std::vector<McpPicture> McpPlanPictures(const std::vector<std::vector<int> >& dist, int reach, int maxPictures)
{
	std::vector<McpPicture> pictures;
	size_t nCities = dist.empty() ? 0 : dist[0].size();
	std::vector<bool> shown(nCities, false);
	size_t left = nCities;
	while(left > 0 && (int)pictures.size() < maxPictures)
	{
		McpPicture best;
		int bestNew = 0;
		for(size_t c = 0; c < dist.size(); ++c)
		{
			McpPicture p;
			p.center = (int)c;
			int fresh = 0;
			for(size_t k = 0; k < nCities && k < dist[c].size(); ++k)
			{
				if(dist[c][k] > reach) continue;
				p.cities.push_back((int)k);
				if(dist[c][k] > p.farthest) p.farthest = dist[c][k];
				if(!shown[k]) ++fresh;
			}
			if(fresh > bestNew || (fresh == bestNew && fresh > 0 && p.farthest < best.farthest)) { best = p; bestNew = fresh; }
		}
		if(bestNew == 0) break;
		for(size_t k = 0; k < best.cities.size(); ++k)
			if(!shown[best.cities[k]]) { shown[best.cities[k]] = true; --left; }
		pictures.push_back(best);
	}
	return pictures;
}

namespace
{
	std::string TargetText(const picojson::object& o, const char* key)
	{
		picojson::object::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string();
	}

	bool TargetInt(const picojson::object& o, const char* key, int& out)
	{
		picojson::object::const_iterator it = o.find(key);
		if(it == o.end() || !it->second.is<double>()) return false;
		out = (int)it->second.get<double>();
		return true;
	}
}

bool McpParseTargets(const picojson::value& list, int nowTurn, std::vector<McpTarget>& out, std::string& err)
{
	if(!list.is<picojson::array>()) { err = "targets must be a list"; return false; }
	const picojson::array& a = list.get<picojson::array>();
	std::vector<McpTarget> parsed;
	for(size_t i = 0; i < a.size(); ++i)
	{
		std::string head = "target " + Num((int)i + 1) + ": ";
		if(!a[i].is<picojson::object>()) { err = head + "each target is an object"; return false; }
		const picojson::object& o = a[i].get<picojson::object>();
		McpTarget t;
		if(!TargetInt(o, "by", t.by)) { err = head + "by (the turn it is meant to be done by) is required"; return false; }
		if(t.by <= nowTurn) { err = head + "by must be a turn after this one (T" + Num(nowTurn) + ")"; return false; }
		t.city = TargetText(o, "city");
		if(!TargetText(o, "tech").empty()) { t.kind = "tech"; t.subject = TargetText(o, "tech"); }
		else if(!TargetText(o, "build").empty()) { t.kind = "build"; t.subject = TargetText(o, "build"); }
		else if(TargetInt(o, "size", t.amount)) t.kind = "size";
		else if(TargetInt(o, "gold", t.amount)) t.kind = "gold";
		else if(o.find("policy") != o.end()) t.kind = "policy";
		else { err = head + "name what it is for: tech, build, size, gold or policy"; return false; }
		if((t.kind == "build" || t.kind == "size") && t.city.empty()) { err = head + t.kind + " needs city: the city's name"; return false; }
		parsed.push_back(t);
	}
	out = parsed;
	return true;
}

std::string McpTargetLabel(const McpTarget& t)
{
	std::string by = " by T" + Num(t.by);
	if(t.kind == "tech") return t.subject + by;
	if(t.kind == "build") return t.city + " " + t.subject + by;
	if(t.kind == "size") return t.city + " size " + Num(t.amount) + by;
	if(t.kind == "gold") return Num(t.amount) + " gold" + by;
	return "next policy" + by;
}

namespace
{
	typedef picojson::object ArgObj;

	bool WholeNumber(const picojson::value& v)
	{
		if(!v.is<double>()) return false;
		double d = v.get<double>();
		return d >= -2147483648.0 && d < 2147483648.0 && d == (double)(long long)d;
	}

	// What a value must be for its property, or NULL when it fits (or the property names no type).
	const char* TypeMismatch(const picojson::value& property, const picojson::value& value)
	{
		if(!property.is<ArgObj>() || value.is<picojson::null>()) return NULL;
		ArgObj::const_iterator t = property.get<ArgObj>().find("type");
		if(t == property.get<ArgObj>().end() || !t->second.is<std::string>()) return NULL;
		const std::string& type = t->second.get<std::string>();
		if(type == "boolean") return value.is<bool>() ? NULL : "true or false";
		if(type == "integer") return WholeNumber(value) ? NULL : "a whole number";
		if(type == "number") return value.is<double>() ? NULL : "a number";
		if(type == "string") return value.is<std::string>() ? NULL : "text";
		if(type == "array") return value.is<picojson::array>() ? NULL : "a list";
		if(type == "object") return value.is<ArgObj>() ? NULL : "an object";
		return NULL;
	}

	bool Given(const ArgObj& args, const std::string& k)
	{
		ArgObj::const_iterator it = args.find(k);
		return it != args.end() && !it->second.is<picojson::null>();
	}

	picojson::value Typed(const char* type)
	{
		ArgObj o;
		o["type"] = picojson::value(std::string(type));
		return picojson::value(o);
	}
}

std::string McpArgsFault(const char* tool, const char* schema, const picojson::object& args)
{
	picojson::value parsed;
	if(!picojson::parse(parsed, std::string(schema)).empty() || !parsed.is<ArgObj>())
		return std::string("the argument schema for ") + tool + " is broken; this is a bug in the mod";
	const ArgObj& s = parsed.get<ArgObj>();
	ArgObj props;
	ArgObj::const_iterator pit = s.find("properties");
	if(pit != s.end() && pit->second.is<ArgObj>()) props = pit->second.get<ArgObj>();
	// A tool that takes unit_id takes from_city in its place, for the unit a city makes as the turn opens
	// (McpNewUnitType), with unit (its type), nth (which of one kind) and _unit_type (written by plan_turn) beside it.
	const bool bFromCity = props.count("unit_id") && Given(args, "from_city");
	if(bFromCity)
	{
		props["from_city"] = Typed("integer");
		props["unit"] = Typed("string");
		props["nth"] = Typed("integer");
		props["_unit_type"] = Typed("integer");
	}
	for(ArgObj::const_iterator it = args.begin(); it != args.end(); ++it)
	{
		ArgObj::const_iterator prop = props.find(it->first);
		if(prop != props.end())
		{
			const char* kind = TypeMismatch(prop->second, it->second);
			if(kind) return "argument '" + it->first + "' for " + tool + " must be " + kind;
			continue;
		}
		std::string known;
		for(ArgObj::const_iterator k = props.begin(); k != props.end(); ++k) known += (known.empty() ? "" : ", ") + k->first;
		return "unknown argument '" + it->first + "' for " + tool + "; it accepts " + (known.empty() ? std::string("no arguments") : known);
	}
	ArgObj::const_iterator rit = s.find("required");
	if(rit != s.end() && rit->second.is<picojson::array>())
	{
		const picojson::array& req = rit->second.get<picojson::array>();
		for(size_t i = 0; i < req.size(); ++i)
			if(req[i].is<std::string>() && !Given(args, req[i].get<std::string>()) && !(bFromCity && req[i].get<std::string>() == "unit_id"))
				return "missing required argument '" + req[i].get<std::string>() + "' for " + tool;
	}
	return "";
}

void McpCoverBoundUnits(const std::vector<McpNewUnitBinding>& bound, McpPlanCovers& c)
{
	for(size_t i = 0; i < bound.size(); ++i) c.units.insert(bound[i].unit);
}

std::string McpBindingLine(const McpNewUnitBinding& b)
{
	return "order " + Num(b.order) + " is for " + b.unitText + ", which " + b.city + " finished as the turn opened: it runs as planned; give it no other order";
}

const char* const MCP_SERVES_ADDED = "added when the turn opened";

bool McpOrderServes(const std::string& tool, const std::string& given, bool bAddedAtTurnOpen, std::string& serves)
{
	serves = given;
	if(!given.empty() || tool == "end_turn") return true;
	if(!bAddedAtTurnOpen) return false;
	serves = MCP_SERVES_ADDED;
	return true;
}

std::string McpPlanBuildFault(bool bCan, bool bCanOnceTechKnown, bool bTechLandsAtOpen, const std::string& techName)
{
	if(bCan || (bCanOnceTechKnown && bTechLandsAtOpen)) return "";
	if(bCanOnceTechKnown) return "it needs " + techName + " first, and that is not due as the turn opens";
	return "it cannot make that build there (wrong unit, or the tile does not take it)";
}

std::string McpPlanStrikeFault(bool bMovesFirst, int iDistance, int iRange, bool bLineOfSight)
{
	if(bMovesFirst) return "";
	const char* what = "; move it first in this plan, or plan another attacker";
	if(iDistance > iRange) return "out of range: the target is " + Num(iDistance) + " tiles away and this unit's range is " + Num(iRange) + what;
	if(!bLineOfSight) return std::string("no line of sight from where it stands: hills, mountains, forest or jungle block the view") + what;
	return "";
}

std::string McpNotYourTurn(bool bRoundOpenForSeat)
{
	if(bRoundOpenForSeat) return "it is not your turn yet: put this in plan_turn's orders and it runs when your turn opens; with preview_only it answers now";
	return "it is not your turn right now (the turn is ending or other civilizations are moving); call wait_for_my_turn";
}

bool McpAskForMissingPlan(bool bRoundOpen, bool bViewRead, bool bHasPlan, bool bAskedThisRound)
{
	return bRoundOpen && bViewRead && !bHasPlan && !bAskedThisRound;
}

McpHandBack McpAfterHandBack(bool bBlocked, bool bOnlyUnitsWait, bool bPlanRunning, int iUnread, int iTimesAgain)
{
	if(iUnread > 0) return MCP_HAND_BACK;
	if(!bBlocked) return iTimesAgain == 0 ? MCP_END_AGAIN : MCP_HAND_BACK;
	if(bOnlyUnitsWait && bPlanRunning && iTimesAgain < 2) return MCP_END_AGAIN_SKIPPING_UNITS;
	return MCP_HAND_BACK;
}

bool McpMessageAsksNothing(const std::string& kind)
{
	const char* const kinds[] = { "BUTTONPOPUP_GOODY_HUT_REWARD", "BUTTONPOPUP_BARBARIAN_CAMP_REWARD", "BUTTONPOPUP_CITY_STATE_GREETING",
		"BUTTONPOPUP_NATURAL_WONDER_REWARD", "BUTTONPOPUP_GREAT_PERSON_REWARD", "BUTTONPOPUP_GOLDEN_AGE_REWARD", "BUTTONPOPUP_TECH_AWARD",
		"BUTTONPOPUP_NEW_ERA", "BUTTONPOPUP_WONDER_COMPLETED_ACTIVE_PLAYER", "BUTTONPOPUP_GREAT_WORK_COMPLETED_ACTIVE_PLAYER",
		"BUTTONPOPUP_ADVISOR_COUNSEL", "BUTTONPOPUP_ADVISOR_INFO", "BUTTONPOPUP_WHOS_WINNING", "BUTTONPOPUP_LEAGUE_SPLASH" };
	for(size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) if(kind == kinds[i]) return true;
	return false;
}
