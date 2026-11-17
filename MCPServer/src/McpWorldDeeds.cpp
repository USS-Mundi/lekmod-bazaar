#include "McpWorldDeeds.h"
#include "McpSeedCompose.h"
#include <sstream>

namespace
{
	typedef picojson::object JObj;
	typedef picojson::array JArr;
	typedef picojson::value JVal;
	typedef std::pair<std::string, std::string> Pair;
	typedef std::set<Pair> Pairs;

	const int NEAR_TILES = 6;      // a city founded this close to one of the seat's is a deed

	std::string The(const std::string& civ) { return McpSeedThe(civ); }
	std::string Num(int n) { std::ostringstream o; o << n; return o.str(); }

	struct Out
	{
		std::vector<McpWorldDeed>& deeds;
		explicit Out(std::vector<McpWorldDeed>& d) : deeds(d) {}
		void Add(const std::string& with, const std::string& text)
		{
			McpWorldDeed d;
			d.with = with; d.text = text;
			deeds.push_back(d);
		}
	};

	JVal PairsJson(const Pairs& pairs)
	{
		JArr a;
		for(Pairs::const_iterator it = pairs.begin(); it != pairs.end(); ++it)
		{
			JArr p;
			p.push_back(JVal(it->first)); p.push_back(JVal(it->second));
			a.push_back(JVal(p));
		}
		return JVal(a);
	}

	void PairsFrom(const JObj& o, const char* key, Pairs& out)
	{
		JObj::const_iterator it = o.find(key);
		if(it == o.end() || !it->second.is<JArr>()) return;
		const JArr& a = it->second.get<JArr>();
		for(size_t i = 0; i < a.size(); ++i)
			if(a[i].is<JArr>() && a[i].get<JArr>().size() >= 2 && a[i].get<JArr>()[0].is<std::string>() && a[i].get<JArr>()[1].is<std::string>())
				out.insert(Pair(a[i].get<JArr>()[0].get<std::string>(), a[i].get<JArr>()[1].get<std::string>()));
	}

	std::string Str(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string();
	}

	int Int(const JObj& o, const char* key, int fallback)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<double>() ? (int)it->second.get<double>() : fallback;
	}
}

std::pair<std::string, std::string> McpWorldPair(const std::string& a, const std::string& b) { return a < b ? Pair(a, b) : Pair(b, a); }

std::vector<McpWorldDeed> McpWorldDeeds(const McpWorld& before, const McpWorld& after, std::vector<std::string>& gone)
{
	std::vector<McpWorldDeed> deeds;
	gone.clear();
	if(before.turn < 0 || after.turn < 0) return deeds;
	Out out(deeds);
	const std::string& me = after.me;
	typedef std::map<Pair, std::string> Wars;

	for(Wars::const_iterator it = after.wars.begin(); it != after.wars.end(); ++it)
	{
		if(before.wars.count(it->first)) continue;
		const std::string &a = it->first.first, &b = it->first.second, &by = it->second;
		if(a == me || b == me)
		{
			const std::string& them = a == me ? b : a;
			out.Add(them, by == them ? "declared war on me" : by == me ? "I declared war on them" : "we went to war");
		}
		else if(by == a || by == b)
		{
			const std::string& on = by == a ? b : a;
			out.Add(by, "declared war on " + The(on));
			out.Add(on, The(by) + " declared war on them");
		}
		else
		{
			out.Add(a, "went to war with " + The(b));
			out.Add(b, "went to war with " + The(a));
		}
	}
	for(Wars::const_iterator it = before.wars.begin(); it != before.wars.end(); ++it)
	{
		const std::string &a = it->first.first, &b = it->first.second;
		if(after.wars.count(it->first) || !after.players.count(a) || !after.players.count(b)) continue;
		if(a == me || b == me) out.Add(a == me ? b : a, "we made peace");
		else
		{
			out.Add(a, "made peace with " + The(b));
			out.Add(b, "made peace with " + The(a));
		}
	}

	for(std::map<int, McpWorldCity>::const_iterator it = before.cities.begin(); it != before.cities.end(); ++it)
	{
		const McpWorldCity& was = it->second;
		std::map<int, McpWorldCity>::const_iterator now = after.cities.find(it->first);
		const std::string &from = was.owner, &first = was.firstOwner;
		if(now == after.cities.end())
		{
			if(from == first) { if(from != me) out.Add(from, was.name + " is gone"); }
			else if(from == me) out.Add(first, "I razed " + was.name + ", once theirs");
			else if(first == me) out.Add(from, "razed " + was.name + ", once mine");
			else
			{
				out.Add(from, "razed " + was.name + ", once " + The(first) + "'s");
				out.Add(first, The(from) + " razed " + was.name + ", once theirs");
			}
			continue;
		}
		const std::string& to = now->second.owner;
		if(to == from) continue;
		bool capital = was.firstCapital;
		// a city-state is its one city: nobody the seat keeps an entry on lost it
		if(from == was.name && !before.players.count(from))
		{
			if(to != me) out.Add(to, "took the city-state " + was.name);
			continue;
		}
		if(to == me) out.Add(from, "I took " + was.name + (capital ? (from == first ? ", their first capital," : ", a first capital,") : "") + " from them");
		else if(from == me) out.Add(to, "took " + was.name + (capital ? (first == me ? ", my first capital," : ", a first capital,") : "") + " from me");
		else if(to == first)
		{
			out.Add(to, "took " + was.name + (capital ? ", their first capital," : "") + " back from " + The(from));
			out.Add(from, "lost " + was.name + " back to " + The(to));
		}
		else
		{
			out.Add(to, "took " + was.name + (capital ? (first == me ? ", my first capital," : ", a first capital,") : "") + " from " + The(from));
			out.Add(from, "lost " + was.name + (capital ? (from == first ? ", their first capital," : ", a first capital,") : "") + " to " + The(to));
		}
	}
	for(std::map<int, McpWorldCity>::const_iterator it = after.cities.begin(); it != after.cities.end(); ++it)
	{
		const McpWorldCity& c = it->second;
		if(before.cities.count(it->first) || c.owner == me || c.founded < before.turn || c.nearTiles < 0 || c.nearTiles > NEAR_TILES) continue;
		out.Add(c.owner, "founded " + c.name + " " + Num(c.nearTiles) + (c.nearTiles == 1 ? " tile" : " tiles") + " from my city " + c.nearCity);
	}

	for(std::set<std::string>::const_iterator it = before.players.begin(); it != before.players.end(); ++it)
	{
		if(after.players.count(*it) || *it == me) continue;
		out.Add(*it, "left the game");
		gone.push_back(*it);
	}

	for(Pairs::const_iterator it = after.friends.begin(); it != after.friends.end(); ++it)
	{
		if(before.friends.count(*it)) continue;
		if(it->first == me || it->second == me) out.Add(it->first == me ? it->second : it->first, "we declared friendship");
		else
		{
			out.Add(it->first, "declared friendship with " + The(it->second));
			out.Add(it->second, "declared friendship with " + The(it->first));
		}
	}
	for(Pairs::const_iterator it = before.friends.begin(); it != before.friends.end(); ++it)
	{
		if(after.friends.count(*it) || !after.players.count(it->first) || !after.players.count(it->second)) continue;
		if(it->first == me || it->second == me) out.Add(it->first == me ? it->second : it->first, "our friendship ended");
		else
		{
			out.Add(it->first, "their friendship with " + The(it->second) + " ended");
			out.Add(it->second, "their friendship with " + The(it->first) + " ended");
		}
	}
	for(Pairs::const_iterator it = after.denounced.begin(); it != after.denounced.end(); ++it)
	{
		if(before.denounced.count(*it)) continue;
		if(it->second == me) out.Add(it->first, "denounced me");
		else if(it->first == me) out.Add(it->second, "I denounced them");
		else
		{
			out.Add(it->first, "denounced " + The(it->second));
			out.Add(it->second, The(it->first) + " denounced them");
		}
	}
	for(Pairs::const_iterator it = after.pacts.begin(); it != after.pacts.end(); ++it)
	{
		if(before.pacts.count(*it)) continue;
		if(it->first == me || it->second == me) out.Add(it->first == me ? it->second : it->first, "we signed a defensive pact");
		else
		{
			out.Add(it->first, "signed a defensive pact with " + The(it->second));
			out.Add(it->second, "signed a defensive pact with " + The(it->first));
		}
	}

	typedef std::map<std::string, std::set<std::string> > Deals;
	static const std::set<std::string> NO_DEALS;
	for(Deals::const_iterator it = after.deals.begin(); it != after.deals.end(); ++it)
	{
		Deals::const_iterator had = before.deals.find(it->first);
		const std::set<std::string>& old = had == before.deals.end() ? NO_DEALS : had->second;
		for(std::set<std::string>::const_iterator d = it->second.begin(); d != it->second.end(); ++d)
			if(!old.count(*d)) out.Add(it->first, "we made a deal: " + *d);
	}
	for(Deals::const_iterator it = before.deals.begin(); it != before.deals.end(); ++it)
	{
		Pair war = McpWorldPair(it->first, me);
		if(!after.players.count(it->first) || (after.wars.count(war) && !before.wars.count(war))) continue;
		Deals::const_iterator has = after.deals.find(it->first);
		const std::set<std::string>& now = has == after.deals.end() ? NO_DEALS : has->second;
		for(std::set<std::string>::const_iterator d = it->second.begin(); d != it->second.end(); ++d)
			if(!now.count(*d)) out.Add(it->first, "a deal between us ended: " + *d);
	}
	return deeds;
}

picojson::value McpWorldToJson(const McpWorld& w)
{
	JObj o;
	o["turn"] = JVal((double)w.turn);
	o["me"] = JVal(w.me);
	JArr players;
	for(std::set<std::string>::const_iterator it = w.players.begin(); it != w.players.end(); ++it) players.push_back(JVal(*it));
	o["players"] = JVal(players);
	JArr wars;
	for(std::map<Pair, std::string>::const_iterator it = w.wars.begin(); it != w.wars.end(); ++it)
	{
		JArr p;
		p.push_back(JVal(it->first.first)); p.push_back(JVal(it->first.second)); p.push_back(JVal(it->second));
		wars.push_back(JVal(p));
	}
	o["wars"] = JVal(wars);
	o["friends"] = PairsJson(w.friends);
	o["pacts"] = PairsJson(w.pacts);
	o["denounced"] = PairsJson(w.denounced);
	JObj cities;
	for(std::map<int, McpWorldCity>::const_iterator it = w.cities.begin(); it != w.cities.end(); ++it)
	{
		const McpWorldCity& c = it->second;
		JObj co;
		co["name"] = JVal(c.name); co["owner"] = JVal(c.owner); co["first"] = JVal(c.firstOwner); co["capital"] = JVal(c.firstCapital);
		co["founded"] = JVal((double)c.founded); co["near"] = JVal(c.nearCity); co["tiles"] = JVal((double)c.nearTiles);
		cities[Num(it->first)] = JVal(co);
	}
	o["cities"] = JVal(cities);
	JObj deals;
	for(std::map<std::string, std::set<std::string> >::const_iterator it = w.deals.begin(); it != w.deals.end(); ++it)
	{
		JArr list;
		for(std::set<std::string>::const_iterator d = it->second.begin(); d != it->second.end(); ++d) list.push_back(JVal(*d));
		deals[it->first] = JVal(list);
	}
	o["deals"] = JVal(deals);
	return JVal(o);
}

bool McpWorldFromJson(const picojson::value& v, McpWorld& w)
{
	if(!v.is<JObj>()) return false;
	const JObj& o = v.get<JObj>();
	w = McpWorld();
	w.turn = Int(o, "turn", -1);
	w.me = Str(o, "me");
	JObj::const_iterator it = o.find("players");
	if(it != o.end() && it->second.is<JArr>())
		for(size_t i = 0; i < it->second.get<JArr>().size(); ++i)
			if(it->second.get<JArr>()[i].is<std::string>()) w.players.insert(it->second.get<JArr>()[i].get<std::string>());
	it = o.find("wars");
	if(it != o.end() && it->second.is<JArr>())
		for(size_t i = 0; i < it->second.get<JArr>().size(); ++i)
		{
			const JVal& p = it->second.get<JArr>()[i];
			if(!p.is<JArr>() || p.get<JArr>().size() < 3 || !p.get<JArr>()[0].is<std::string>() || !p.get<JArr>()[1].is<std::string>() || !p.get<JArr>()[2].is<std::string>()) continue;
			w.wars[Pair(p.get<JArr>()[0].get<std::string>(), p.get<JArr>()[1].get<std::string>())] = p.get<JArr>()[2].get<std::string>();
		}
	PairsFrom(o, "friends", w.friends);
	PairsFrom(o, "pacts", w.pacts);
	PairsFrom(o, "denounced", w.denounced);
	it = o.find("cities");
	if(it != o.end() && it->second.is<JObj>())
		for(JObj::const_iterator c = it->second.get<JObj>().begin(); c != it->second.get<JObj>().end(); ++c)
		{
			if(!c->second.is<JObj>()) continue;
			const JObj& co = c->second.get<JObj>();
			McpWorldCity city;
			city.name = Str(co, "name"); city.owner = Str(co, "owner"); city.firstOwner = Str(co, "first");
			JObj::const_iterator cap = co.find("capital");
			city.firstCapital = cap != co.end() && cap->second.is<bool>() && cap->second.get<bool>();
			city.founded = Int(co, "founded", -1); city.nearCity = Str(co, "near"); city.nearTiles = Int(co, "tiles", -1);
			std::istringstream id(c->first);
			int n = 0;
			if(id >> n) w.cities[n] = city;
		}
	it = o.find("deals");
	if(it != o.end() && it->second.is<JObj>())
		for(JObj::const_iterator d = it->second.get<JObj>().begin(); d != it->second.get<JObj>().end(); ++d)
		{
			if(!d->second.is<JArr>()) continue;
			for(size_t i = 0; i < d->second.get<JArr>().size(); ++i)
				if(d->second.get<JArr>()[i].is<std::string>()) w.deals[d->first].insert(d->second.get<JArr>()[i].get<std::string>());
		}
	return w.turn >= 0 && !w.me.empty();
}
