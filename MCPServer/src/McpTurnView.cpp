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
#include "CvGlobals.h"
#include "CvGameCoreUtils.h"

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

	// The seat's units, military and civilian apart.
	void AddUnits(CvPlayer* me, std::vector<McpViewItem>& items, std::vector<McpWithoutOrder>& withoutOrder)
	{
		JVal result = Call(ToolUnits);
		if(!result.is<JObj>()) return;
		JObj::const_iterator li = result.get<JObj>().find("units");
		if(li == result.get<JObj>().end() || !li->second.is<JArr>()) return;
		const JArr& list = li->second.get<JArr>();
		for(size_t k = 0; k < list.size(); ++k)
		{
			if(!list[k].is<JObj>()) continue;
			const JObj& o = list[k].get<JObj>();
			Fields f;
			McpObjectFields(o, f);
			McpTrimUnitFields(f);
			CvUnit* u = me->getUnit(ArgInt(o, "id", -1));
			if(u && u->getDamage() > 0)
			{
				int rate = u->healRate(u->plot());
				f.push_back(std::make_pair(std::string("heals"), rate > 0
					? McpFormat("+%d/t while it does not move or attack: full in %dt", rate, (u->getDamage() + rate - 1) / rate)
					: std::string("not on this tile")));
			}
			bool bMilitary = u && u->IsCombatUnit();
			if(u && ArgBool(o, "needs_orders", false))
				withoutOrder.push_back(Waiting(false, u->GetID(), McpFormat("%s %d", UnitTypeName(u->getUnitType()), u->GetID())));
			std::string section = bMilitary ? "military units" : "civilian units";
			items.push_back(Item(section, McpObjectKey("unit", o, k), f));
		}
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

	// Every unit of others the seat can see.
	void AddOthersUnits(CvPlayer* me, std::vector<McpViewItem>& items)
	{
		TeamTypes eTeam = me->getTeam();
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || i == (int)me->GetID()) continue;
			std::string who = p.isBarbarian() ? std::string("Barbarians") : CivName((PlayerTypes)i);
			bool bEnemy = IsEnemyOf(me, (PlayerTypes)i);
			int iLoop;
			for(CvUnit* u = p.firstUnit(&iLoop); u; u = p.nextUnit(&iLoop))
			{
				CvPlot* plot = u->plot();
				if(!plot || !plot->isVisible(eTeam) || u->isInvisible(eTeam, false)) continue;
				Fields f;
				f.push_back(std::make_pair(std::string("owner"), who));
				f.push_back(std::make_pair(std::string("type"), std::string(UnitTypeName(u->getUnitType()))));
				f.push_back(std::make_pair(std::string("at"), McpFormat("(%d,%d)", u->getX(), u->getY())));
				f.push_back(std::make_pair(std::string("hp"), McpFormat("%d of %d", u->GetCurrHitPoints(), u->GetMaxHitPoints())));
				f.push_back(std::make_pair(std::string("at war with you"), std::string(bEnemy ? "yes" : "no")));
				f.push_back(std::make_pair(std::string("moves"), McpFormat("%d", (int)Moves(u->maxMoves()))));
				if(u->IsCombatUnit()) f.push_back(std::make_pair(std::string("strength"), McpFormat("%d", u->GetBaseCombatStrength())));
				if(u->isRanged()) f.push_back(std::make_pair(std::string("ranged strength"), McpFormat("%d, range %d", u->GetBaseRangedCombatStrength(), u->GetRange())));
				f.push_back(std::make_pair(std::string("nearest of yours"), NearestOfYours(me, u->getX(), u->getY())));
				items.push_back(Item("units of others in sight", McpFormat("seen:%d:%d", i, u->GetID()), f));
			}
		}
	}

	// Who holds each seat at the table: the seat's own, the other agent seats, the human, the game's AI. Unmet
	// civilizations are counted, not named.
	void AddPlayers(CvPlayer* me, std::vector<McpViewItem>& items)
	{
		CvTeam& myTeam = GET_TEAM(me->getTeam());
		int unmet = 0;
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive()) continue;
			bool bMe = i == (int)me->GetID();
			if(!bMe && !myTeam.isHasMet(p.getTeam())) { ++unmet; continue; }
			Fields f;
			f.push_back(std::make_pair(std::string("civilization"), CivName((PlayerTypes)i)));
			f.push_back(std::make_pair(std::string("leader"), std::string(p.getName())));
			std::string who = bMe ? "you" : McpPlayedByModel((PlayerTypes)i) ? "an agent seat" : p.isHuman() ? "the human" : "the game's AI";
			f.push_back(std::make_pair(std::string("played by"), who));
			if(!bMe) f.push_back(std::make_pair(std::string("at war with you"), std::string(myTeam.isAtWar(p.getTeam()) ? "yes" : "no")));
			items.push_back(Item("players", McpFormat("player:%d", i), f));
		}
		if(unmet > 0)
		{
			Fields f;
			f.push_back(std::make_pair(std::string("not met yet"), McpFormat("%d", unmet)));
			items.push_back(Item("players", "player:unmet", f));
		}
	}

	void AddCities(CvPlayer* me, std::vector<McpViewItem>& items, std::vector<McpWithoutOrder>& withoutOrder, std::vector<std::string>& names)
	{
		JVal result = Call(ToolCities);
		if(!result.is<JObj>()) return;
		JObj::const_iterator li = result.get<JObj>().find("cities");
		if(li == result.get<JObj>().end() || !li->second.is<JArr>()) return;
		const JArr& list = li->second.get<JArr>();
		for(size_t k = 0; k < list.size(); ++k)
		{
			if(!list[k].is<JObj>()) continue;
			const JObj& o = list[k].get<JObj>();
			Fields f;
			McpObjectFields(o, f);
			std::string key = McpObjectKey("city", o, k);
			items.push_back(Item("cities", key, f));
			CvCity* c = me->getCity(ArgInt(o, "id", -1));
			if(!c) continue;
			names.push_back(c->getName().c_str());
			if(c->getOrderQueueLength() == 0) withoutOrder.push_back(Waiting(true, c->GetID(), std::string(c->getName().c_str()) + "'s production"));
			JObj args;
			args["city_id"] = J(c->GetID());
			McpCityClocks cc = McpGatherCityClocks(c);
			AddClockItems("cities", key, std::string(c->getName().c_str()) + ": ", cc.clocks, cc.notes, cc.queue, items);
			JVal citizens = Call(ToolCityCitizens, args);
			if(citizens.is<JObj>())
			{
				Fields cf;
				cf.push_back(std::make_pair(std::string("name"), std::string(c->getName().c_str()) + " citizens"));
				McpCitizensFields(citizens.get<JObj>(), cf);
				items.push_back(Item("cities", key + ":citizens", cf));
			}
		}
	}

	void AddEmpire(std::vector<McpViewItem>& items)
	{
		JVal overview = Call(ToolOverview);
		if(!overview.is<JObj>()) return;
		JObj plain;
		const JObj& o = overview.get<JObj>();
		for(JObj::const_iterator it = o.begin(); it != o.end(); ++it)
			if(!it->second.is<JArr>()) plain[it->first] = it->second;
		Fields f;
		McpObjectFields(plain, f);
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
	AddEmpire(d.items);
	AddEmpireClocks(me, d.items);
	AddCities(me, d.items, d.withoutOrder, d.cities);
	AddUnits(me, d.items, d.withoutOrder);
	AddOthersUnits(me, d.items);
	// The research clock and the research queue in the empire's clocks already count the current research.
	JVal research = Call(ToolResearchOptions);
	if(research.is<JObj>())
	{
		JObj::iterator ri = research.get<JObj>().find("research");
		if(ri != research.get<JObj>().end() && ri->second.is<JObj>())
		{
			const char* const counted[] = { "cost", "progress", "turns_left", "queued_after" };
			for(size_t i = 0; i < sizeof(counted) / sizeof(counted[0]); ++i) ri->second.get<JObj>().erase(counted[i]);
		}
	}
	McpScreenItems("research", research, d.items);
	McpScreenItems("policies", Call(ToolPolicyOptions), d.items);
	McpScreenItems("religion", Call(ToolReligionOverview), d.items);
	AddPlayers(me, d.items);
	McpScreenItems("civilizations and city-states", Call(ToolCivs), d.items);
	McpScreenItems("diplomacy", Call(ToolDiplomacy), d.items);
	McpScreenItems("known cities", Call(ToolKnownCities), d.items);
	McpScreenItems("rankings", Call(ToolRankings), d.items);
	McpScreenItems("economy", Call(ToolEconomicOverview), d.items);
	// Its unit list repeats the military and civilian units above.
	JVal military = Call(ToolMilitaryOverview);
	if(military.is<JObj>()) military.get<JObj>().erase("units");
	McpScreenItems("military overview", military, d.items);
	McpScreenItems("trade routes", Call(ToolTradeRoutes), d.items);
	McpScreenItems("victory", Call(ToolVictoryProgress), d.items);
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
	AddLines(o, "decisions_pending", d.decisions);
	AddLines(o, "diplomacy_requests", d.decisions);
	AddLines(o, "unread_messages", d.decisions);
	AddLines(o, "warnings", d.decisions);
}
