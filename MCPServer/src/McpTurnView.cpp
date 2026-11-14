#include "CvGameCoreDLLPCH.h"
#include "McpTurnView.h"
#include "McpToolSupport.h"
#include "McpToolHandlers.h"
#include "McpSeat.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvTeam.h"
#include "CvUnit.h"
#include "CvCity.h"
#include "CvPlot.h"
#include "CvImprovementClasses.h"
#include "CvGlobals.h"
#include "CvGameCoreUtils.h"
#include <algorithm>
#include <map>

using namespace McpTool;

#include "McpViewItems.h"
#include "McpClocksGame.h"

namespace
{
	typedef std::vector<std::pair<std::string, std::string> > Fields;

	McpViewItem Item(const std::string& section, const std::string& key, const Fields& fields)
	{
		McpViewItem it;
		it.section = section;
		it.key = key;
		it.fields = fields;
		return it;
	}

	McpWithoutOrder Waiting(bool bCity, int id, const std::string& label)
	{
		McpWithoutOrder w;
		w.city = bCity;
		w.id = id;
		w.label = label;
		return w;
	}

	struct FirstLess
	{
		template <class A, class B> bool operator()(const std::pair<A, B>& a, const std::pair<A, B>& b) const { return a.first < b.first; }
	};

	JVal Call(picojson::value (*tool)(const picojson::object&, std::string&), const JObj& args)
	{
		std::string err;
		JVal v = tool(args, err);
		return err.empty() ? v : JVal();
	}

	JVal Call(picojson::value (*tool)(const picojson::object&, std::string&))
	{
		JObj none;
		return Call(tool, none);
	}

	// One line per clock, marked only when where it lands moves or it reaches its edge (slack 0): its numbers tick
	// every turn and are shown, not compared. Notes and the queue's order are compared; the queue's turns are not.
	void AddClockItems(const std::string& section, const std::string& key, const std::string& prefix, const std::vector<McpClock>& clocks,
		const std::vector<std::string>& notes, const std::vector<std::pair<std::string, int> >& queue, std::vector<McpViewItem>& items)
	{
		int now = GC.getGame().getGameTurn();
		for(size_t i = 0; i < clocks.size(); ++i)
		{
			Fields f;
			f.push_back(std::make_pair(std::string("name"), prefix + clocks[i].name));
			f.push_back(std::make_pair(std::string("clock"), McpClockBody(clocks[i])));
			f.push_back(std::make_pair(std::string("lands"), McpClockLands(clocks[i], now)));
			if(McpClockAtEdge(clocks[i])) f.push_back(std::make_pair(std::string("edge"), std::string("slack 0")));
			items.push_back(Item(section, key + ":clock:" + clocks[i].name, f));
		}
		for(size_t i = 0; i < notes.size(); ++i)
		{
			Fields f;
			f.push_back(std::make_pair(std::string("name"), prefix + "note"));
			f.push_back(std::make_pair(std::string("note"), notes[i]));
			items.push_back(Item(section, key + ":note:" + notes[i].substr(0, notes[i].find(' ')), f));
		}
		if(!queue.empty())
		{
			std::string order;
			for(size_t i = 0; i < queue.size(); ++i) order += (i ? " → " : "") + queue[i].first;
			Fields f;
			f.push_back(std::make_pair(std::string("name"), prefix + "queue"));
			f.push_back(std::make_pair(std::string("order"), order));
			f.push_back(std::make_pair(std::string("clock"), McpQueueLine(queue)));
			items.push_back(Item(section, key + ":queue", f));
		}
	}

	Fields::value_type F(const char* name, const std::string& value) { return std::make_pair(std::string(name), value); }

	std::string Amount(double v)
	{
		return v == (int)v ? McpFormat("%d", (int)v) : McpFormat("%.1f", v);
	}

	// The list under key in a tool's result. It points into v, so keep v in a variable for as long as the list is read:
	// a call's temporary result is freed at the end of the statement, and its list with it (that left the cities and
	// research sections empty in play).
	const JArr* ListIn(const JVal& v, const char* key)
	{
		if(!v.is<JObj>()) return NULL;
		JObj::const_iterator it = v.get<JObj>().find(key);
		return it != v.get<JObj>().end() && it->second.is<JArr>() ? &it->second.get<JArr>() : NULL;
	}

	double Number(const JObj& o, const char* key)
	{
		JObj::const_iterator it = o.find(key);
		return it != o.end() && it->second.is<double>() ? it->second.get<double>() : 0;
	}

	// The seat's city nearest a tile, and how far: "Tunis, 2 tiles".
	std::string NearestCity(CvPlayer* me, int x, int y, int& best)
	{
		best = -1;
		std::string name;
		int iLoop;
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
		{
			int d = plotDistance(x, y, c->getX(), c->getY());
			if(best < 0 || d < best) { best = d; name = c->getName().c_str(); }
		}
		return best < 0 ? std::string() : McpFormat("%s, %d tiles", name.c_str(), best);
	}

	// The top bar: era, size, happiness and the stocks with what they gain a turn.
	void AddTopBar(std::vector<McpViewItem>& items)
	{
		JVal overview = Call(ToolOverview);
		if(!overview.is<JObj>()) return;
		const JObj& o = overview.get<JObj>();
		Fields f;
		f.push_back(F("_era", ArgStr(o, "era")));
		f.push_back(F("cities", Amount(Number(o, "num_cities"))));
		f.push_back(F("units", McpFormat("%s (%s military)", Amount(Number(o, "num_units")).c_str(), Amount(Number(o, "military_units")).c_str())));
		f.push_back(F("happiness", McpFormat("%+d", (int)Number(o, "happiness"))));
		f.push_back(F("gold", McpFormat("%s (%+d/t)", Amount(Number(o, "gold")).c_str(), (int)Number(o, "gold_per_turn"))));
		f.push_back(F("science", Amount(Number(o, "science_per_turn")) + "/t"));
		f.push_back(F("culture", McpFormat("%s (%+d/t)", Amount(Number(o, "culture")).c_str(), (int)Number(o, "culture_per_turn"))));
		f.push_back(F("faith", McpFormat("%s (%+d/t)", Amount(Number(o, "faith")).c_str(), (int)Number(o, "faith_per_turn"))));
		f.push_back(F("techs", Amount(Number(o, "techs_known"))));
		f.push_back(F("score", Amount(Number(o, "score"))));
		items.push_back(Item("empire", "empire", f));
	}

	void AddEmpireClocks(CvPlayer* me, std::vector<McpViewItem>& items)
	{
		McpEmpireClocks ec = McpGatherEmpireClocks(me);
		AddClockItems("empire", "empire", "", ec.clocks, ec.notes, ec.researchQueue, items);
		int now = GC.getGame().getGameTurn();
		for(size_t i = 0; i < ec.drains.size(); ++i)
		{
			const McpDrain& dr = ec.drains[i];
			Fields f;
			f.push_back(std::make_pair(std::string("name"), dr.name));
			f.push_back(std::make_pair(std::string("clock"), McpDrainBody(dr.have100, dr.rate100) + ", " + dr.then));
			f.push_back(std::make_pair(std::string("lands"), McpDrainLands(dr.have100, dr.rate100, now)));
			items.push_back(Item("empire", "empire:drain:" + dr.name, f));
		}
		Fields f;
		f.push_back(std::make_pair(std::string("name"), std::string("where it comes from")));
		for(size_t i = 0; i < ec.sources.size(); ++i) f.push_back(std::make_pair("sources " + ec.sources[i].first, ec.sources[i].second));
		items.push_back(Item("empire", "empire:sources", f));
	}

	// The seat's unit or city nearest to a tile, and how far.
	std::string NearestOfYours(CvPlayer* me, int x, int y)
	{
		int best = -1;
		std::string what;
		int iLoop;
		for(CvUnit* u = me->firstUnit(&iLoop); u; u = me->nextUnit(&iLoop))
		{
			int d = plotDistance(x, y, u->getX(), u->getY());
			if(best < 0 || d < best) { best = d; what = McpFormat("%s %d", UnitTypeName(u->getUnitType()), u->GetID()); }
		}
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
		{
			int d = plotDistance(x, y, c->getX(), c->getY());
			if(best < 0 || d < best) { best = d; what = c->getName().c_str(); }
		}
		return best < 0 ? std::string("none") : McpFormat("%s, %d tiles", what.c_str(), best);
	}

	// Every ancient ruin the seat knows of, the nearest to its units and cities first, with how far that is.
	void AddRuins(CvPlayer* me, std::vector<McpViewItem>& items)
	{
		TeamTypes eTeam = me->getTeam();
		CvMap& map = GC.getMap();
		std::vector<std::pair<int, McpViewItem> > rows;
		for(int i = 0; i < map.numPlots(); ++i)
		{
			CvPlot* p = map.plotByIndexUnchecked(i);
			if(!p || !p->isRevealed(eTeam)) continue;
			ImprovementTypes eImp = p->getRevealedImprovementType(eTeam);
			if(eImp == NO_IMPROVEMENT) continue;
			CvImprovementEntry* pImp = GC.getImprovementInfo(eImp);
			if(!pImp || !pImp->IsGoody()) continue;
			Fields f;
			f.push_back(F("name", "ancient ruins"));
			f.push_back(F("_at", McpFormat("(%d,%d)", p->getX(), p->getY())));
			std::string nearest = NearestOfYours(me, p->getX(), p->getY());
			size_t comma = nearest.rfind(", ");
			int dist = comma == std::string::npos ? 9999 : atoi(nearest.c_str() + comma + 2);
			if(comma != std::string::npos) f.push_back(F("_near", McpFormat("%d from your %s", dist, nearest.substr(0, comma).c_str())));
			if(!p->isVisible(eTeam)) f.push_back(F("_seen", "out of sight now"));
			rows.push_back(std::make_pair(dist, Item("ruins", McpFormat("ruins:%d:%d", p->getX(), p->getY()), f)));
		}
		std::stable_sort(rows.begin(), rows.end(), FirstLess());
		for(size_t k = 0; k < rows.size(); ++k) items.push_back(rows[k].second);
	}

	// Every unit of others in sight, grouped by owner: a line for each owner (its code, name and whether you are at
	// war), then its units nearest to yours first, each with its numbers in one bracket and how far it is from your
	// nearest unit or city. Owners at war with you come first, then the nearest.
	void AddOthersUnits(CvPlayer* me, std::vector<McpViewItem>& items)
	{
		TeamTypes eTeam = me->getTeam();
		struct Group { bool war; int nearest; std::string code; std::vector<std::pair<int, McpViewItem> > rows; McpViewItem head; };
		std::vector<Group> groups;
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || i == (int)me->GetID()) continue;
			Group g;
			g.war = IsEnemyOf(me, (PlayerTypes)i);
			g.nearest = 9999;
			g.code = "@" + CivCode((PlayerTypes)i);
			int iLoop;
			for(CvUnit* u = p.firstUnit(&iLoop); u; u = p.nextUnit(&iLoop))
			{
				CvPlot* plot = u->plot();
				if(!plot || !plot->isVisible(eTeam) || u->isInvisible(eTeam, false)) continue;
				Fields f;
				f.push_back(F("name", g.code + " " + UnitTypeName(u->getUnitType())));
				f.push_back(F("_at", McpFormat("(%d,%d)", u->getX(), u->getY())));
				f.push_back(F("_stats", McpUnitStats(u->GetCurrHitPoints(), u->IsCombatUnit() ? u->GetBaseCombatStrength() : 0,
					u->isRanged() ? u->GetBaseRangedCombatStrength() : 0, u->GetRange(), Amount(Moves(u->maxMoves())), -1)));
				std::string nearest = NearestOfYours(me, u->getX(), u->getY());
				int dist = atoi(nearest.substr(nearest.rfind(", ") == std::string::npos ? 0 : nearest.rfind(", ") + 2).c_str());
				size_t comma = nearest.rfind(", ");
				if(comma != std::string::npos) f.push_back(F("_near", McpFormat("%d from your %s", dist, nearest.substr(0, comma).c_str())));
				g.rows.push_back(std::make_pair(dist, Item("others in sight", McpFormat("seen:%d:%d", i, u->GetID()), f)));
				if(dist < g.nearest) g.nearest = dist;
			}
			if(g.rows.empty()) continue;
			std::stable_sort(g.rows.begin(), g.rows.end(), FirstLess());
			Fields h;
			h.push_back(F("name", g.code + " " + CivName((PlayerTypes)i)));
			h.push_back(F("_war", g.war ? "AT WAR with you" : "at peace"));
			g.head = Item("others in sight", McpFormat("seen:%d", i), h);
			groups.push_back(g);
		}
		for(size_t a = 0; a < groups.size(); ++a)
			for(size_t b = a + 1; b < groups.size(); ++b)
				if(groups[b].war > groups[a].war || (groups[b].war == groups[a].war && groups[b].nearest < groups[a].nearest)) std::swap(groups[a], groups[b]);
		for(size_t k = 0; k < groups.size(); ++k)
		{
			items.push_back(groups[k].head);
			for(size_t i = 0; i < groups[k].rows.size(); ++i) items.push_back(groups[k].rows[i].second);
		}
	}

	// Others' cities the seat knows, nearest to its own first, at most twelve: owner, at war or not, and while in sight
	// their size, health and defense.
	void AddTheirCities(CvPlayer* me, std::vector<McpViewItem>& items)
	{
		TeamTypes eTeam = me->getTeam();
		std::vector<std::pair<int, McpViewItem> > rows;
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || i == (int)me->GetID()) continue;
			int iLoop;
			for(CvCity* c = p.firstCity(&iLoop); c; c = p.nextCity(&iLoop))
			{
				if(!c->isRevealed(eTeam, false)) continue;
				bool bSeen = c->plot()->isVisible(eTeam);
				Fields f;
				f.push_back(F("name", McpFormat("%s @%s", c->getName().c_str(), CivCode((PlayerTypes)i).c_str())));
				f.push_back(F("_at", McpFormat("(%d,%d)", c->getX(), c->getY())));
				if(bSeen)
				{
					f.push_back(F("pop", McpFormat("%d", c->getPopulation())));
					f.push_back(F("hp", McpFormat("%d/%d", c->GetMaxHitPoints() - c->getDamage(), c->GetMaxHitPoints())));
					f.push_back(F("defense", McpFormat("%d", c->getStrengthValue() / 100)));
				}
				else f.push_back(F("_seen", "out of sight, as last seen"));
				int dist = -1;
				std::string closest = NearestCity(me, c->getX(), c->getY(), dist);
				if(!closest.empty()) f.push_back(F("near", closest));
				rows.push_back(std::make_pair(dist < 0 ? 999 : dist, Item("their cities", McpFormat("theirs:%d:%d", i, c->GetID()), f)));
			}
		}
		std::stable_sort(rows.begin(), rows.end(), FirstLess());
		for(size_t i = 0; i < rows.size() && i < 12; ++i) items.push_back(rows[i].second);
	}

	// The seat's units of one kind, grouped by the city they are nearest: where, what they are doing, health, strength,
	// moves, and whether they wait for orders.
	void AddUnits(CvPlayer* me, bool bMilitary, std::vector<McpViewItem>& items, std::vector<McpWithoutOrder>& withoutOrder)
	{
		std::map<int, JObj> byId;
		JVal result = Call(ToolUnits);
		const JArr* list = ListIn(result, "units");
		for(size_t k = 0; list && k < list->size(); ++k)
			if((*list)[k].is<JObj>()) byId[ArgInt((*list)[k].get<JObj>(), "id", -1)] = (*list)[k].get<JObj>();
		std::vector<std::pair<std::string, McpViewItem> > rows;
		int iLoop;
		for(CvUnit* u = me->firstUnit(&iLoop); u; u = me->nextUnit(&iLoop))
		{
			if(u->IsCombatUnit() != bMilitary) continue;
			std::map<int, JObj>::const_iterator jo = byId.find(u->GetID());
			Fields f;
			f.push_back(F("name", McpFormat("%s %d", UnitTypeName(u->getUnitType()), u->GetID())));
			f.push_back(F("_at", McpFormat("(%d,%d)", u->getX(), u->getY())));
			std::string activity = jo != byId.end() ? ArgStr(jo->second, "activity") : std::string();
			if(!activity.empty()) f.push_back(F("_activity", activity));
			std::string moves = Amount(Moves(u->getMoves())) + "/" + Amount(Moves(u->maxMoves()));
			f.push_back(F("_stats", McpUnitStats(u->GetCurrHitPoints(), bMilitary ? u->GetBaseCombatStrength() : 0,
				bMilitary && u->isRanged() ? u->GetBaseRangedCombatStrength() : 0, u->GetRange(), moves, bMilitary ? u->getLevel() : -1)));
			if(u->getDamage() > 0)
			{
				int rate = u->healRate(u->plot());
				f.push_back(F("heals", rate > 0 ? McpFormat("+%d/t resting, whole in %dt", rate, (u->getDamage() + rate - 1) / rate) : std::string("not here")));
			}
			if(u->isPromotionReady()) f.push_back(F("_promote", "can promote"));
			if(u->CanUpgradeRightNow(false)) f.push_back(F("_upgrade", McpFormat("upgrade to %s %dg", UnitTypeName(u->GetUpgradeUnitType()), u->upgradePrice(u->GetUpgradeUnitType()))));
			if(u->isEmbarked()) f.push_back(F("_embarked", "embarked"));
			if(jo != byId.end() && ArgBool(jo->second, "can_found_city_here", false)) f.push_back(F("_found", "can found a city here"));
			if(jo != byId.end() && ArgBool(jo->second, "needs_orders", false))
			{
				f.push_back(F("_orders", "idle"));
				withoutOrder.push_back(Waiting(false, u->GetID(), McpFormat("%s %d", UnitTypeName(u->getUnitType()), u->GetID())));
			}
			int dist = -1;
			std::string closest = NearestCity(me, u->getX(), u->getY(), dist);
			if(!closest.empty()) f.push_back(F("near", closest));
			rows.push_back(std::make_pair(closest.substr(0, closest.find(',')) + McpFormat("%04d", dist < 0 ? 0 : dist),
				Item(bMilitary ? "your military units" : "your civilians", McpFormat("unit:%d", u->GetID()), f)));
		}
		std::stable_sort(rows.begin(), rows.end(), FirstLess());
		for(size_t i = 0; i < rows.size(); ++i) items.push_back(rows[i].second);
	}

	std::string Yields(const JObj& o)
	{
		JObj::const_iterator it = o.find("yields_per_turn");
		if(it == o.end() || !it->second.is<JObj>()) return "";
		const JObj& y = it->second.get<JObj>();
		const char* const keys[] = { "food", "production", "gold", "science", "culture", "faith" };
		const char* const letters[] = { "f", "p", "g", "s", "c", "fa" };
		std::string out;
		for(size_t i = 0; i < 6; ++i)
		{
			double v = Number(y, keys[i]);
			if(v != 0) out += (out.empty() ? "" : " ") + Amount(v) + letters[i];
		}
		return out;
	}

	// One city in full: its line with buildings, its clocks (build, growth, next border tile, the price to buy the
	// build), its queue, and the tiles it works and could work. What city_detail shows.
	void AddCityDetail(CvPlayer* me, CvCity* c, const JObj& o, std::vector<McpViewItem>& items)
	{
		(void)me;
		std::string name = c->getName().c_str();
		std::string key = McpFormat("city:%d", c->GetID());
		Fields f;
		f.push_back(F("name", name));
		f.push_back(F("_at", McpFormat("(%d,%d)", c->getX(), c->getY())));
		f.push_back(F("id", McpFormat("%d", c->GetID())));
		f.push_back(F("pop", McpFormat("%d", c->getPopulation())));
		f.push_back(F("hp", McpFormat("%d/%d", c->GetMaxHitPoints() - c->getDamage(), c->GetMaxHitPoints())));
		f.push_back(F("defense", McpFormat("%d", c->getStrengthValue() / 100)));
		std::string yields = Yields(o);
		if(!yields.empty()) f.push_back(F("yields", yields));
		std::string focus = ArgStr(o, "focus");
		if(!focus.empty() && focus != "default") f.push_back(F("focus", focus));
		JObj::const_iterator b = o.find("buildings");
		if(b != o.end() && b->second.is<JArr>())
		{
			std::string list;
			const JArr& a = b->second.get<JArr>();
			for(size_t i = 0; i < a.size(); ++i) if(a[i].is<std::string>()) list += (list.empty() ? "" : ", ") + a[i].get<std::string>();
			if(!list.empty()) f.push_back(F("buildings", "[" + list + "]"));
		}
		items.push_back(Item("city", key, f));
		McpCityClocks cc = McpGatherCityClocks(c);
		AddClockItems("city", key, "", cc.clocks, cc.notes, cc.queue, items);
		JObj args;
		args["city_id"] = J(c->GetID());
		JVal citizens = Call(ToolCityCitizens, args);
		if(citizens.is<JObj>())
		{
			Fields cf;
			cf.push_back(F("name", "tiles"));
			McpCitizensFields(citizens.get<JObj>(), cf);
			items.push_back(Item("city", key + ":citizens", cf));
		}
	}

	// A clock's turns as a word: "5t", or "never" when it never completes at this rate.
	std::string ClockTurns(const McpClock& c)
	{
		int t = McpClockTurns(c);
		return t < 0 ? std::string("never") : McpFormat("%dt", t);
	}

	// Each of the seat's cities in one line: size, what it builds and in how many turns, when it grows and when its
	// border next grows, its yields, its health when damaged, and anything stopping its growth. city_detail has the
	// rest.
	void AddCities(CvPlayer* me, std::vector<McpViewItem>& items, std::vector<McpWithoutOrder>& withoutOrder, std::vector<std::string>& names)
	{
		JVal result = Call(ToolCities);
		const JArr* list = ListIn(result, "cities");
		for(size_t k = 0; list && k < list->size(); ++k)
		{
			if(!(*list)[k].is<JObj>()) continue;
			const JObj& o = (*list)[k].get<JObj>();
			CvCity* c = me->getCity(ArgInt(o, "id", -1));
			if(!c) continue;
			std::string name = c->getName().c_str();
			names.push_back(name);
			McpCityClocks cc = McpGatherCityClocks(c);
			std::string build = c->getOrderQueueLength() == 0 ? std::string("NOTHING") : std::string(c->getProductionName());
			std::string grows = "?", border = "?";
			for(size_t i = 0; i < cc.clocks.size(); ++i)
			{
				const McpClock& clk = cc.clocks[i];
				if(clk.name == "growth") grows = ClockTurns(clk);
				else if(clk.name == "next border tile") border = ClockTurns(clk);
				else if(c->getOrderQueueLength() > 0 && clk.name == build) build += " " + ClockTurns(clk);
			}
			Fields f;
			f.push_back(F("name", name));
			f.push_back(F("id", McpFormat("%d", c->GetID())));
			if(c->isCapital()) f.push_back(F("_capital", "capital"));
			f.push_back(F("pop", McpFormat("%d", c->getPopulation())));
			f.push_back(F("builds", build));
			f.push_back(F("grows", grows));
			f.push_back(F("border", border));
			std::string yields = Yields(o);
			if(!yields.empty()) f.push_back(F("_yields", yields));
			if(c->getDamage() > 0) f.push_back(F("hp", McpFormat("%d/%d", c->GetMaxHitPoints() - c->getDamage(), c->GetMaxHitPoints())));
			for(size_t i = 0; i < cc.notes.size(); ++i)
				if(cc.notes[i].find("starv") != std::string::npos || cc.notes[i].find("paused") != std::string::npos) f.push_back(F("_note", cc.notes[i]));
			items.push_back(Item("your cities (city_detail shows tiles, buildings and clocks)", McpFormat("city:%d", c->GetID()), f));
			if(c->getOrderQueueLength() == 0) withoutOrder.push_back(Waiting(true, c->GetID(), name + "'s production"));
		}
	}

	// The technologies open to research now, with their turns.
	void AddResearch(std::vector<McpViewItem>& items)
	{
		JVal result = Call(ToolResearchOptions);
		const JArr* list = ListIn(result, "available");
		if(!list || list->empty()) return;
		std::string open;
		for(size_t i = 0; i < list->size(); ++i)
		{
			if(!(*list)[i].is<JObj>()) continue;
			const JObj& t = (*list)[i].get<JObj>();
			open += (open.empty() ? "" : " · ") + ArgStr(t, "name") + " " + Amount(Number(t, "turns")) + "t";
		}
		Fields f;
		f.push_back(F("name", "open to research"));
		f.push_back(F("turns", open));
		items.push_back(Item("research", "research:open", f));
	}

	// The policy branches: how far each opened one is, which can be opened, and whether a policy can be adopted now.
	void AddPolicies(std::vector<McpViewItem>& items)
	{
		JVal result = Call(ToolPolicyOptions);
		const JArr* list = ListIn(result, "branches");
		if(!list) return;
		std::string opened, canOpen, locked;
		for(size_t i = 0; i < list->size(); ++i)
		{
			if(!(*list)[i].is<JObj>()) continue;
			const JObj& b = (*list)[i].get<JObj>();
			std::string name = ArgStr(b, "name");
			if(ArgBool(b, "unlocked", false))
			{
				int owned = 0, total = 0;
				std::string left;
				JObj::const_iterator pl = b.find("policies");
				if(pl != b.end() && pl->second.is<JArr>())
					for(size_t k = 0; k < pl->second.get<JArr>().size(); ++k)
					{
						const JVal& pv = pl->second.get<JArr>()[k];
						if(!pv.is<JObj>()) continue;
						++total;
						if(ArgBool(pv.get<JObj>(), "owned", false)) ++owned;
						else left += (left.empty() ? "" : ", ") + ArgStr(pv.get<JObj>(), "name");
					}
				opened += (opened.empty() ? "" : " · ") + McpFormat("%s %d/%d", name.c_str(), owned, total) + (left.empty() ? "" : " (left: " + left + ")");
			}
			else if(ArgBool(b, "can_unlock_now", false)) canOpen += (canOpen.empty() ? "" : ", ") + name;
			else locked += (locked.empty() ? "" : ", ") + name;
		}
		Fields f;
		f.push_back(F("name", "branches"));
		if(!opened.empty()) f.push_back(F("opened", opened));
		if(!canOpen.empty()) f.push_back(F("can open", canOpen));
		if(!locked.empty()) f.push_back(F("locked", locked));
		if(result.is<JObj>() && ArgBool(result.get<JObj>(), "can_adopt_now", false)) f.push_back(F("_adopt", "A POLICY CAN BE ADOPTED NOW"));
		items.push_back(Item("policies", "policies", f));
	}

	std::string Flag(const JObj& o, const char* key) { return ArgBool(o, key, false) ? "y" : ""; }

	// One deal as text from the seat's side: "you give Whales 1 (20t); they give 6 gold/t (20t); 4t left".
	std::string DealText(const JObj& d, const std::string& me)
	{
		std::string give, get;
		JObj::const_iterator di = d.find("deal");
		const JObj* deal = di != d.end() && di->second.is<JObj>() ? &di->second.get<JObj>() : &d;
		JObj::const_iterator items = deal->find("items");
		if(items != deal->end() && items->second.is<JArr>())
			for(size_t i = 0; i < items->second.get<JArr>().size(); ++i)
			{
				const JVal& v = items->second.get<JArr>()[i];
				if(!v.is<JObj>()) continue;
				const JObj& it = v.get<JObj>();
				std::string kind = ArgStr(it, "item"), what;
				if(kind == "resource") what = ArgStr(it, "resource") + " " + Amount(Number(it, "amount"));
				else if(kind == "gold_per_turn") what = Amount(Number(it, "amount")) + " gold/t";
				else if(kind == "gold") what = Amount(Number(it, "amount")) + " gold";
				else
				{
					what = kind;
					for(size_t k = 0; k < what.size(); ++k) if(what[k] == '_') what[k] = ' ';
				}
				if(Number(it, "turns") > 0) what += " (" + Amount(Number(it, "turns")) + "t)";
				std::string& side = ArgStr(it, "given_by") == me ? give : get;
				side += (side.empty() ? "" : ", ") + what;
			}
		std::string out;
		if(!give.empty()) out += "you give " + give;
		if(!get.empty()) out += (out.empty() ? "" : "; ") + std::string("they give ") + get;
		if(Number(d, "turns_left") > 0) out += McpFormat("; %dt left", (int)Number(d, "turns_left"));
		return out;
	}

	// Each civilization met: who plays it, war or peace with the seat and with others, its army against the seat's,
	// its size, its capital, what the game's AI thinks of the seat, the ties between them and their deals; then each
	// city-state met, its standing with the seat and whose ally it is.
	void AddPlayers(CvPlayer* me, std::vector<McpViewItem>& items)
	{
		JVal civs = Call(ToolCivs), diplomacy = Call(ToolDiplomacy);
		std::string myName = CivName(me->GetID());
		std::map<std::string, JObj> relations;
		const JArr* dl = ListIn(diplomacy, "civilizations");
		for(size_t i = 0; dl && i < dl->size(); ++i)
		{
			if(!(*dl)[i].is<JObj>()) continue;
			JObj::const_iterator r = (*dl)[i].get<JObj>().find("relations");
			if(r != (*dl)[i].get<JObj>().end() && r->second.is<JObj>()) relations[ArgStr((*dl)[i].get<JObj>(), "civilization")] = r->second.get<JObj>();
		}
		const JArr* deals = ListIn(diplomacy, "current_deals");
		const JArr* cl = ListIn(civs, "civilizations");
		for(size_t i = 0; cl && i < cl->size(); ++i)
		{
			if(!(*cl)[i].is<JObj>()) continue;
			const JObj& c = (*cl)[i].get<JObj>();
			std::string civ = ArgStr(c, "civilization");
			Fields f;
			f.push_back(F("name", McpFormat("%s (%s, %s)", civ.c_str(), ArgStr(c, "leader").c_str(), ArgStr(c, "played_by").c_str())));
			bool bWar = ArgBool(c, "at_war", false);
			f.push_back(F("_war", bWar ? "AT WAR with you" : "at peace with you"));
			if(!bWar && !ArgBool(c, "can_declare_war", true)) f.push_back(F("_treaty", "no war yet: a peace treaty holds"));
			std::map<std::string, JObj>::const_iterator rel = relations.find(civ);
			if(rel != relations.end())
			{
				JObj::const_iterator w = rel->second.find("at_war_with");
				if(w != rel->second.end() && w->second.is<JArr>() && !w->second.get<JArr>().empty())
				{
					std::string list;
					for(size_t k = 0; k < w->second.get<JArr>().size(); ++k)
						if(w->second.get<JArr>()[k].is<std::string>()) list += (list.empty() ? "" : ", ") + w->second.get<JArr>()[k].get<std::string>();
					f.push_back(F("at war with", list));
				}
			}
			if(Number(c, "military_strength_vs_yours") > 0) f.push_back(F("army", McpFormat("%.1fx yours", Number(c, "military_strength_vs_yours"))));
			f.push_back(F("cities", Amount(Number(c, "num_cities"))));
			f.push_back(F("score", Amount(Number(c, "score"))));
			std::string capital = ArgStr(c, "capital");
			if(!capital.empty()) f.push_back(F("capital", capital));
			std::string attitude = ArgStr(c, "their_attitude"), opinion = ArgStr(c, "their_opinion_of_you");
			if(!attitude.empty()) f.push_back(F("attitude", attitude + (opinion.empty() ? "" : ", sees you as " + opinion)));
			if(rel != relations.end())
			{
				const JObj& r = rel->second;
				std::string ties;
				bool ours = ArgBool(r, "our_embassy_with_them", false), theirs = ArgBool(r, "their_embassy_with_us", false);
				if(ours || theirs) ties += ours && theirs ? "embassies both ways" : ours ? "your embassy with them" : "their embassy with you";
				bool toThem = ArgBool(r, "open_borders_for_them", false), toUs = ArgBool(r, "open_borders_for_us", false);
				if(toThem || toUs) ties += (ties.empty() ? "" : ", ") + std::string(toThem && toUs ? "open borders both ways" : toThem ? "your borders open to them" : "their borders open to you");
				if(ArgBool(r, "declared_friendship", false)) ties += (ties.empty() ? "" : ", ") + std::string("declared friends");
				if(ArgBool(r, "defensive_pact", false)) ties += (ties.empty() ? "" : ", ") + std::string("defensive pact");
				if(ArgBool(r, "research_agreement", false)) ties += (ties.empty() ? "" : ", ") + std::string("research agreement");
				if(ArgBool(r, "denounced_you", false)) ties += (ties.empty() ? "" : ", ") + std::string("they denounced you");
				if(ArgBool(r, "you_denounced_them", false)) ties += (ties.empty() ? "" : ", ") + std::string("you denounced them");
				if(!ties.empty()) f.push_back(F("ties", ties));
			}
			std::string dealText;
			for(size_t k = 0; deals && k < deals->size(); ++k)
			{
				if(!(*deals)[k].is<JObj>()) continue;
				const JObj& d = (*deals)[k].get<JObj>();
				JObj::const_iterator di = d.find("deal");
				const JObj& deal = di != d.end() && di->second.is<JObj>() ? di->second.get<JObj>() : d;
				if(ArgStr(deal, "from") != civ && ArgStr(deal, "to") != civ) continue;
				dealText += (dealText.empty() ? "" : " | ") + DealText(d, myName);
			}
			if(!dealText.empty()) f.push_back(F("deals", dealText));
			items.push_back(Item("players", "player:" + civ, f));
		}
		int unmet = 0;
		CvTeam& myTeam = GET_TEAM(me->getTeam());
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(p.isAlive() && i != (int)me->GetID() && !myTeam.isHasMet(p.getTeam())) ++unmet;
		}
		if(unmet > 0)
		{
			Fields f;
			f.push_back(F("name", McpFormat("%d civilization(s) not met yet", unmet)));
			items.push_back(Item("players", "player:unmet", f));
		}
		const JArr* cs = ListIn(civs, "city_states");
		for(size_t i = 0; cs && i < cs->size(); ++i)
		{
			if(!(*cs)[i].is<JObj>()) continue;
			const JObj& c = (*cs)[i].get<JObj>();
			std::string name = ArgStr(c, "name"), type = ArgStr(c, "type"), at = ArgStr(c, "city_at");
			Fields f;
			f.push_back(F("name", type.empty() ? name : name + " (" + type + ")"));
			if(!at.empty()) f.push_back(F("_at", at));
			if(ArgBool(c, "at_war", false)) f.push_back(F("_war", "AT WAR with you"));
			f.push_back(F("_standing", ArgBool(c, "allies", false) ? "your ally" : ArgBool(c, "friends", false) ? "your friend" : "neither friend nor ally"));
			f.push_back(F("influence", Amount(Number(c, "friendship"))));
			std::string ally = ArgStr(c, "ally");
			if(!ally.empty() && !ArgBool(c, "allies", false)) f.push_back(F("ally of", ally));
			items.push_back(Item("city-states", "cs:" + name, f));
		}
	}

	// The screens left to the tools, each reduced to its words: the turn view says which changed since the seat's last
	// turn, not counting numbers that move every turn.
	void AddScreens(std::map<std::string, std::string>& screens)
	{
		struct Screen { const char* name; picojson::value (*tool)(const picojson::object&, std::string&); };
		const Screen list[] = {
			{ "religion_overview", ToolReligionOverview }, { "trade_routes", ToolTradeRoutes }, { "victory_progress", ToolVictoryProgress },
			{ "economic_overview", ToolEconomicOverview }, { "rankings", ToolRankings }, { "military_overview", ToolMilitaryOverview },
		};
		for(size_t i = 0; i < sizeof(list) / sizeof(list[0]); ++i)
		{
			std::string text = Call(list[i].tool).serialize(), words;
			for(size_t k = 0; k < text.size(); ++k)
				if(!isdigit((unsigned char)text[k]) && text[k] != '.' && text[k] != '-') words += text[k];
			screens[list[i].name] = words;
		}
	}

	void AddLines(const JObj& o, const char* key, std::vector<std::string>& out)
	{
		JObj::const_iterator it = o.find(key);
		if(it == o.end()) return;
		if(it->second.is<JArr>())
		{
			const JArr& a = it->second.get<JArr>();
			for(size_t i = 0; i < a.size(); ++i)
			{
				if(a[i].is<std::string>()) { out.push_back(a[i].get<std::string>()); continue; }
				if(!a[i].is<JObj>()) continue;
				Fields f;
				McpObjectFields(a[i].get<JObj>(), f);
				std::string line;
				for(size_t k = 0; k < f.size(); ++k) line += (k ? " · " : "") + (f[k].first == "name" ? f[k].second : f[k].first + " " + f[k].second);
				out.push_back(line);
			}
		}
		else if(it->second.is<std::string>() && !it->second.get<std::string>().empty()) out.push_back(it->second.get<std::string>());
	}
}

McpTurnViewData McpGatherTurnView(CvPlayer* me)
{
	McpTurnViewData d;
	AddTopBar(d.items);
	AddEmpireClocks(me, d.items);
	AddOthersUnits(me, d.items);
	AddTheirCities(me, d.items);
	AddRuins(me, d.items);
	AddUnits(me, true, d.items, d.withoutOrder);
	AddCities(me, d.items, d.withoutOrder, d.cities);
	AddUnits(me, false, d.items, d.withoutOrder);
	AddResearch(d.items);
	AddPolicies(d.items);
	AddPlayers(me, d.items);
	AddScreens(d.screens);
	McpUniqueKeys(d.items);
	d.ticking = McpTickingFields(d.items);
	return d;
}

void McpGatherTurnNews(McpTurnViewData& d)
{
	JVal briefing = Call(ToolBriefing);
	if(!briefing.is<JObj>()) return;
	const JObj& o = briefing.get<JObj>();
	AddLines(o, "events", d.news);
	AddLines(o, "game_messages", d.news);
	AddLines(o, "units_gone_since_last_turn", d.news);
	for(size_t i = 0; i < d.news.size(); ++i) d.news[i] = McpTrimNews(d.news[i]);
	std::vector<std::string> pending;
	AddLines(o, "decisions_pending", pending);
	for(size_t i = 0; i < pending.size(); ++i)
	{
		if(pending[i].compare(0, 10, "promotion:") == 0 || pending[i].compare(0, 8, "upgrade:") == 0) continue;
		McpDecisionKindType kind = McpDecisionKind(pending[i]);
		(kind == MCP_DECISION_BLOCKS ? d.blocks : kind == MCP_DECISION_FROM_OTHERS ? d.fromOthers : d.available).push_back(pending[i]);
	}
	AddLines(o, "diplomacy_requests", d.fromOthers);
	AddLines(o, "warnings", d.state);
}

picojson::value McpCityDetailText(CvPlayer* me, int cityId, std::string& err)
{
	CvCity* c = me->getCity(cityId);
	if(!c) { err = "no city of yours with that id; the turn view lists each city's id"; return picojson::value(); }
	JVal result = Call(ToolCities);
	const JArr* list = ListIn(result, "cities");
	JObj found;
	for(size_t k = 0; list && k < list->size(); ++k)
		if((*list)[k].is<JObj>() && ArgInt((*list)[k].get<JObj>(), "id", -1) == cityId) found = (*list)[k].get<JObj>();
	std::vector<McpViewItem> items;
	AddCityDetail(me, c, found, items);
	std::vector<McpViewSection> view = McpMarkView(std::vector<McpViewItem>(), items, std::set<std::string>(), false);
	std::string text;
	for(size_t i = 0; i < view.size(); ++i)
		for(size_t k = 0; k < view[i].lines.size(); ++k) text += view[i].lines[k] + "\n";
	JObj o;
	o["city"] = J(text);
	return JVal(o);
}
