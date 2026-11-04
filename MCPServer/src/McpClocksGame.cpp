#include "CvGameCoreDLLPCH.h"
#include "McpClocksGame.h"
#include "McpToolSupport.h"
#include "McpToolHandlers.h"
#include "McpCrashLog.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvTeam.h"
#include "CvCity.h"
#include "CvGlobals.h"
#include "CvTechClasses.h"
#include "CvReligionClasses.h"
#include <set>

using namespace McpTool;

namespace
{
	McpClock Clock(const std::string& name, long long have100, long long need100, long long first100, long long rate100, int gameTurns)
	{
		McpClock c;
		c.name = name;
		c.have100 = have100;
		c.need100 = need100;
		c.first100 = first100;
		c.rate100 = rate100;
		c.gameTurns = gameTurns;
		return c;
	}

	std::string TechTitle(TechTypes e)
	{
		CvTechEntry* info = GC.getTechInfo(e);
		return info ? std::string(info->GetDescription()) : std::string("?");
	}

	// The game's own count, or -1 when it keeps none: it reports a huge number, or production with nothing coming in.
	int GameCount(int turns, long long rate100)
	{
		return (rate100 <= 0 || turns < 0 || turns >= 100000) ? -1 : turns;
	}

	long long TechLeft100(CvPlayer* me, TechTypes e)
	{
		long long cost = (long long)me->GetPlayerTechs()->GetResearchCost(e) * 100;
		long long progress = (long long)GET_TEAM(me->getTeam()).GetTeamTechs()->GetResearchProgress(e) * 100;
		return cost > progress ? cost - progress : 0;
	}

	// The technology and every one it needs that the team does not know yet.
	void TechPath(CvPlayer* me, TechTypes e, std::set<int>& path)
	{
		if(e == NO_TECH || path.count((int)e) || GET_TEAM(me->getTeam()).GetTeamTechs()->HasTech(e)) return;
		path.insert((int)e);
		CvTechEntry* info = GC.getTechInfo(e);
		if(!info) return;
		for(int i = 0; i < GC.getNUM_AND_TECH_PREREQS(); ++i)
		{
			int pre = info->GetPrereqAndTechs(i);
			if(pre != NO_TECH) TechPath(me, (TechTypes)pre, path);
		}
	}

	CvCity* CityNamed(CvPlayer* me, const std::string& name)
	{
		int iLoop;
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
			if(MatchesKey(c->getName().c_str(), c->getName().c_str(), name)) return c;
		return NULL;
	}

	std::string Sources(CvPlayer* me, int total, int (*perCity)(CvCity*))
	{
		std::string out;
		int sum = 0, iLoop;
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
		{
			int v = perCity(c);
			sum += v;
			out += (out.empty() ? "" : " · ") + std::string(c->getName().c_str()) + McpFormat(" %d", v);
		}
		if(total != sum) out += (out.empty() ? "" : " · ") + McpFormat("other %d", total - sum);
		return out;
	}

	int CityScience(CvCity* c) { return c->getYieldRateTimes100(YIELD_SCIENCE, false) / 100; }
	int CityCulture(CvCity* c) { return c->getJONSCulturePerTurn(); }
	int CityFaith(CvCity* c) { return c->GetFaithPerTurn(); }
}

McpCityClocks McpGatherCityClocks(CvCity* c)
{
	McpCityClocks out;
	if(c->getOrderQueueLength() == 0) out.notes.push_back("nothing being built");
	else if(c->isProductionProcess()) out.notes.push_back(std::string("producing ") + c->getProductionName() + " (ongoing)");
	else
	{
		out.clocks.push_back(Clock(c->getProductionName(), c->getProductionTimes100(), (long long)c->getProductionNeeded() * 100,
			c->getCurrentProductionDifferenceTimes100(false, true), c->getCurrentProductionDifferenceTimes100(false, false),
			GameCount(c->getProductionTurnsLeft(), c->getCurrentProductionDifferenceTimes100(false, false))));
		int price = -1;
		UnitTypes eUnit = c->getProductionUnit();
		BuildingTypes eBuilding = c->getProductionBuilding();
		if(eUnit != NO_UNIT && c->IsCanPurchase(false, true, eUnit, NO_BUILDING, NO_PROJECT, YIELD_GOLD)) price = c->GetPurchaseCost(eUnit);
		else if(eBuilding != NO_BUILDING && c->IsCanPurchase(false, true, NO_UNIT, eBuilding, NO_PROJECT, YIELD_GOLD)) price = c->GetPurchaseCost(eBuilding);
		if(price > 0)
		{
			int gold = GET_PLAYER(c->getOwner()).GetTreasury()->GetGold();
			out.notes.push_back(McpFormat("buy %s now: %d gold (you have %d)", c->getProductionName(), price, gold));
		}
	}
	if(c->isFoodProduction()) out.notes.push_back(std::string("growth paused while it builds ") + c->getProductionName() + " (its surplus food goes into it)");
	else
	{
		long long food = c->foodDifferenceTimes100();
		if(food < 0)
		{
			long long stored = c->getFoodTimes100();
			long long turns = stored > 0 ? (stored + (-food) - 1) / (-food) : 0;
			out.notes.push_back(McpFormat("starving at %s food a turn: the store empties in %d turns, then a citizen is lost",
				McpAmount(food, false).c_str(), (int)turns));
		}
		else out.clocks.push_back(Clock("growth", c->getFoodTimes100(), (long long)c->growthThreshold() * 100, food, food, food > 0 ? c->getFoodTurnsLeft() : -1));
	}
	out.clocks.push_back(Clock("next border tile", (long long)c->GetJONSCultureStored() * 100, (long long)c->GetJONSCultureThreshold() * 100,
		(long long)c->getJONSCulturePerTurn() * 100, (long long)c->getJONSCulturePerTurn() * 100, -1));
	int n = (int)c->getOrderQueueLength();
	for(int i = 0; i < n; ++i)
	{
		const OrderData* od = c->getOrderFromQueue(i);
		if(!od) continue;
		int turns = -1;
		switch(od->eOrderType)
		{
		case ORDER_TRAIN: turns = c->getProductionTurnsLeft((UnitTypes)od->iData1, i); break;
		case ORDER_CONSTRUCT: turns = c->getProductionTurnsLeft((BuildingTypes)od->iData1, i); break;
		case ORDER_CREATE: turns = c->getProductionTurnsLeft((ProjectTypes)od->iData1, i); break;
		default: break;
		}
		if(turns >= 0 && turns < 100000) out.queue.push_back(std::make_pair(OrderName(od), turns));
	}
	return out;
}

McpEmpireClocks McpGatherEmpireClocks(CvPlayer* me)
{
	McpEmpireClocks out;
	long long science = me->GetScienceTimes100();
	TechTypes eTech = me->GetPlayerTechs()->GetCurrentResearch();
	if(eTech != NO_TECH)
	{
		long long have = (long long)GET_TEAM(me->getTeam()).GetTeamTechs()->GetResearchProgress(eTech) * 100;
		long long need = (long long)me->GetPlayerTechs()->GetResearchCost(eTech) * 100;
		out.clocks.push_back(Clock("research: " + TechTitle(eTech), have, need, science + (long long)me->getOverflowResearch() * 100, science,
			GameCount(me->GetPlayerTechs()->GetResearchTurnsLeft(eTech, true), science)));
	}
	const CLLNode<TechTypes>* node = me->headResearchQueueNode();
	for(; node; node = me->nextResearchQueueNode(node))
	{
		TechTypes e = node->m_data;
		McpClock q = Clock(TechTitle(e), 0, TechLeft100(me, e), science, science, -1);
		out.researchQueue.push_back(std::make_pair(TechTitle(e), McpClockTurns(q)));
	}
	if(me->getNextPolicyCost() > 0)
		out.clocks.push_back(Clock("next policy", (long long)me->getJONSCulture() * 100, (long long)me->getNextPolicyCost() * 100,
			(long long)me->GetTotalJONSCulturePerTurn() * 100, (long long)me->GetTotalJONSCulturePerTurn() * 100, -1));
	CvGameReligions* religions = GC.getGame().GetGameReligions();
	if(religions && !religions->HasCreatedPantheon(me->GetID()))
		out.clocks.push_back(Clock("pantheon", (long long)me->GetFaith() * 100, (long long)religions->GetMinimumFaithNextPantheon() * 100,
			(long long)me->GetTotalFaithPerTurn() * 100, (long long)me->GetTotalFaithPerTurn() * 100, -1));
	else out.notes.push_back(McpFormat("faith %d, %+d a turn (pantheon founded)", me->GetFaith(), me->GetTotalFaithPerTurn()));
	if(me->isGoldenAge()) out.notes.push_back(McpFormat("golden age: %d turns left", me->getGoldenAgeTurns()));
	else
	{
		int excess = me->GetExcessHappiness();
		long long rate = excess > 0 ? (long long)excess * 100 : 0;
		out.clocks.push_back(Clock("golden age", (long long)me->GetGoldenAgeProgressMeter() * 100, (long long)me->GetGoldenAgeProgressThreshold() * 100, rate, rate, -1));
	}
	int gold = me->GetTreasury()->GetGold(), goldRate = me->calculateGoldRate();
	if(goldRate < 0)
	{
		McpDrain d;
		d.name = "treasury";
		d.have100 = (long long)gold * 100;
		d.rate100 = (long long)goldRate * 100;
		d.then = "then the shortfall comes out of science, and units can be disbanded";
		out.drains.push_back(d);
	}
	else out.notes.push_back(McpFormat("gold %d, %+d a turn", gold, goldRate));
	out.sources.push_back(std::make_pair(std::string("science"), Sources(me, (int)(science / 100), CityScience)));
	out.sources.push_back(std::make_pair(std::string("culture"), Sources(me, me->GetTotalJONSCulturePerTurn(), CityCulture)));
	out.sources.push_back(std::make_pair(std::string("faith"), Sources(me, me->GetTotalFaithPerTurn(), CityFaith)));
	return out;
}

std::string McpTargetStatus(CvPlayer* me, const McpTarget& t)
{
	std::string label = McpTargetLabel(t);
	int now = GC.getGame().getGameTurn();
	if(t.kind == "tech")
	{
		TechTypes e = FindTechType(t.subject);
		if(e == NO_TECH) return label + ": no technology is named " + t.subject;
		if(GET_TEAM(me->getTeam()).GetTeamTechs()->HasTech(e)) return label + ": researched";
		std::set<int> path;
		TechPath(me, e, path);
		long long left = 0;
		for(std::set<int>::const_iterator it = path.begin(); it != path.end(); ++it) left += TechLeft100(me, (TechTypes)*it);
		long long science = me->GetScienceTimes100();
		std::string line = McpTargetLine(label, Clock(t.subject, 0, left, science, science, -1), t.by, now);
		return path.size() > 1 ? line + McpFormat(" (%d technologies on the way)", (int)path.size()) : line;
	}
	if(t.kind == "gold")
	{
		long long rate = (long long)me->calculateGoldRate() * 100;
		return McpTargetLine(label, Clock("gold", (long long)me->GetTreasury()->GetGold() * 100, (long long)t.amount * 100, rate, rate, -1), t.by, now);
	}
	if(t.kind == "policy")
	{
		long long rate = (long long)me->GetTotalJONSCulturePerTurn() * 100;
		return McpTargetLine(label, Clock("policy", (long long)me->getJONSCulture() * 100, (long long)me->getNextPolicyCost() * 100, rate, rate, -1), t.by, now);
	}
	CvCity* c = CityNamed(me, t.city);
	if(!c) return label + ": no city of yours is named " + t.city;
	if(t.kind == "size")
	{
		int pop = c->getPopulation();
		if(pop >= t.amount) return label + ": reached";
		long long left = (long long)c->growthThreshold() * 100 - c->getFoodTimes100();
		for(int s = pop + 1; s < t.amount; ++s) left += (long long)me->getGrowthThreshold(s) * 100;
		long long rate = c->isFoodProduction() ? 0 : c->foodDifferenceTimes100();
		return McpTargetLine(label, Clock("growth", 0, left, rate, rate, -1), t.by, now) + " (no food kept between sizes)";
	}
	CityItem item = FindCityItem(me, t.subject);
	if(item.eUnit == NO_UNIT && item.eBuilding == NO_BUILDING) return label + ": nothing buildable is named " + t.subject;
	if(item.eBuilding != NO_BUILDING && c->GetCityBuildings()->GetNumBuilding(item.eBuilding) > 0) return label + ": built";
	long long left = 0;
	bool bFound = false;
	int n = (int)c->getOrderQueueLength();
	for(int i = 0; i < n && !bFound; ++i)
	{
		const OrderData* od = c->getOrderFromQueue(i);
		if(!od) continue;
		long long needed = 0;
		if(od->eOrderType == ORDER_TRAIN) needed = c->getProductionNeeded((UnitTypes)od->iData1);
		else if(od->eOrderType == ORDER_CONSTRUCT) needed = c->getProductionNeeded((BuildingTypes)od->iData1);
		else if(od->eOrderType == ORDER_CREATE) needed = c->getProductionNeeded((ProjectTypes)od->iData1);
		left += needed * 100 - (i == 0 ? c->getProductionTimes100() : 0);
		bFound = (od->eOrderType == ORDER_TRAIN && od->iData1 == (int)item.eUnit) || (od->eOrderType == ORDER_CONSTRUCT && od->iData1 == (int)item.eBuilding);
	}
	if(!bFound) left += (long long)(item.eUnit != NO_UNIT ? c->getProductionNeeded(item.eUnit) : c->getProductionNeeded(item.eBuilding)) * 100;
	std::string line = McpTargetLine(label, Clock(t.subject, 0, left, c->getCurrentProductionDifferenceTimes100(false, true),
		c->getCurrentProductionDifferenceTimes100(false, false), -1), t.by, now);
	return line + (bFound ? " (with what is ahead of it in the queue, at the city's current production)" : " (not queued: after the whole queue, at the city's current production)");
}

MCP_TOOL(ToolClocks)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	McpTrail("clocks tool", me->GetID());
	int turns = ArgInt(args, "turns", 0);
	int byTurn = ArgInt(args, "by_turn", 0);
	int now = GC.getGame().getGameTurn();
	if(byTurn > 0) turns = byTurn - now;
	if((HasArg(args, "turns") || HasArg(args, "by_turn")) && turns < 1) { err = "turns must be at least 1, and by_turn after this turn"; return JVal(); }
	std::vector<McpClock> clocks;
	JObj o;
	JArr notes;
	if(HasArg(args, "city_id"))
	{
		CvCity* c = RequireCity(me, args, err);
		if(!c) return JVal();
		McpCityClocks cc = McpGatherCityClocks(c);
		clocks = cc.clocks;
		for(size_t i = 0; i < cc.notes.size(); ++i) notes.push_back(J(cc.notes[i]));
		o["city"] = J(c->getName().c_str());
		if(!cc.queue.empty()) o["queue"] = J(McpQueueLine(cc.queue));
	}
	else
	{
		McpEmpireClocks ec = McpGatherEmpireClocks(me);
		clocks = ec.clocks;
		for(size_t i = 0; i < ec.drains.size(); ++i)
			notes.push_back(J(ec.drains[i].name + " " + McpDrainBody(ec.drains[i].have100, ec.drains[i].rate100) + ", " + ec.drains[i].then));
		for(size_t i = 0; i < ec.notes.size(); ++i) notes.push_back(J(ec.notes[i]));
		if(!ec.researchQueue.empty()) o["research_queue"] = J(McpQueueLine(ec.researchQueue));
		JObj sources;
		for(size_t i = 0; i < ec.sources.size(); ++i) sources[ec.sources[i].first] = J(ec.sources[i].second);
		o["sources"] = JVal(sources);
	}
	JArr lines;
	for(size_t i = 0; i < clocks.size(); ++i)
	{
		std::string line = McpClockLine(clocks[i]);
		if(turns >= 1)
		{
			int t = McpClockTurns(clocks[i]);
			bool bAgrees = clocks[i].gameTurns < 0 || clocks[i].gameTurns == t;
			if(!bAgrees) line += " · in " + McpFormat("%d turns: unavailable", turns);
			else if(t >= 0 && t <= turns) line += McpFormat(" · in %d turns: done already, slack ", turns) + McpAmount(McpClockSlackFor(clocks[i], turns), false);
			else line += McpFormat(" · in %d turns: need +", turns) + McpAmount(McpClockNeedFor(clocks[i], turns), true);
		}
		lines.push_back(J(line));
	}
	o["clocks"] = JVal(lines);
	if(!notes.empty()) o["notes"] = JVal(notes);
	o["definitions"] = J("turns at the current rate, as the game counts them; need: more per turn to finish a turn sooner (or within the turns asked); slack: how much less per turn still finishes on the same turn; queue: each item's own turns added up");
	return JVal(o);
}
