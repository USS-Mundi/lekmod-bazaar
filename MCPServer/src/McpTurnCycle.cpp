#include "McpTurnCycle.h"
#include <algorithm>
#include <sstream>

namespace
{
	const int NEAR = 5; // anyone else's unit within this many tiles of yours is worth a line

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
		if(!u.enemy && (u.nearDist < 0 || u.nearDist > NEAR)) continue;
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

	if(now.gold != planned.gold) out.push_back("gold " + Num(now.gold) + " (was " + Num(planned.gold) + ")");
	if(now.happiness != planned.happiness) out.push_back("happiness " + Num(now.happiness) + " (was " + Num(planned.happiness) + ")");

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
