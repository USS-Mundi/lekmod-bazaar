#include "McpTurnCycle.h"
#include <algorithm>
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

std::vector<std::string> McpViewChanges(const McpView& planned, const McpView& now, size_t limit)
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
	if((now.gold == 0 && planned.gold > 0) || goldMoved >= GOLD_SHOWN || -goldMoved >= GOLD_SHOWN)
		out.push_back("gold " + Num(now.gold) + " (was " + Num(planned.gold) + ")");
	if((now.happiness < 0) != (planned.happiness < 0) || happyMoved >= HAPPINESS_SHOWN || -happyMoved >= HAPPINESS_SHOWN)
		out.push_back("happiness " + Num(now.happiness) + " (was " + Num(planned.happiness) + ")");

	if(out.size() > limit)
	{
		size_t more = out.size() - limit;
		out.resize(limit);
		out.push_back("...and " + Num((int)more) + " more");
	}
	return out;
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
		const char* const names[] = { "direction", "cities", "military", "civilians", "research", "policy", "relations" };
		headings.assign(names, names + sizeof(names) / sizeof(names[0]));
	}
	return headings;
}

namespace
{
	const int PLAN_WORDS_SHOWN = 700;
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
			err = "'" + it->first + "' is not a heading of the plan; use direction, cities, military, civilians, research, policy and relations";
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
		err = "a first plan needs at least direction: how you win from here, who is in the way, what would change it";
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
	std::string Render(const McpViewItem& item)
	{
		std::string out;
		for(size_t i = 0; i < item.fields.size(); ++i)
		{
			const std::string& f = item.fields[i].first;
			if(i) out += " · ";
			out += (f == "name" || f == "type") ? item.fields[i].second : f + " " + item.fields[i].second;
		}
		return out;
	}

	// The fields that changed, outside the ticking ones, as "<field> <old value>"; empty when nothing did.
	std::string Was(const McpViewItem& before, const McpViewItem& now, const std::set<std::string>& ticking)
	{
		std::map<std::string, std::string> oldValues, newValues;
		for(size_t i = 0; i < before.fields.size(); ++i) oldValues[before.fields[i].first] = before.fields[i].second;
		for(size_t i = 0; i < now.fields.size(); ++i) newValues[now.fields[i].first] = now.fields[i].second;
		std::vector<std::string> names;
		for(size_t i = 0; i < before.fields.size(); ++i) names.push_back(before.fields[i].first);
		for(size_t i = 0; i < now.fields.size(); ++i)
			if(!oldValues.count(now.fields[i].first)) names.push_back(now.fields[i].first);
		std::string out;
		for(size_t i = 0; i < names.size(); ++i)
		{
			const std::string& f = names[i];
			if(ticking.count(f)) continue;
			std::map<std::string, std::string>::const_iterator o = oldValues.find(f), n = newValues.find(f);
			std::string was = o == oldValues.end() ? std::string("none") : o->second;
			if(n != newValues.end() && o != oldValues.end() && n->second == o->second) continue;
			if(!out.empty()) out += "; ";
			out += f + " " + was;
		}
		return out;
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
			std::string changed = Was(before[old->second], now[i], ticking);
			s.lines.push_back(changed.empty() ? "  " + line : "~ " + line + "  (was: " + changed + ")");
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
