#include "CvGameCoreDLLPCH.h"
#include "McpTurnPlan.h"
#include "McpTurnCycle.h"
#include "McpPlanJournal.h"
#include "McpToolSupport.h"
#include "McpToolHandlers.h"
#include "McpTools.h"
#include "McpBridge.h"
#include "McpSeat.h"
#include "McpCityStateFiles.h"
#include "McpReplyText.h"
#include "McpDiplomacy.h"
#include "McpMapImage.h"
#include "McpTurnView.h"
#include "McpClocksGame.h"
#include "McpCrashLog.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvTeam.h"
#include "CvUnit.h"
#include "CvCity.h"
#include "CvPlot.h"
#include "CvGlobals.h"
#include "CvGameCoreUtils.h"
#include <map>
#include <set>
#include <sstream>

using namespace McpTool;

namespace
{
	const size_t MCP_CHANGES_SHOWN = 30;

	struct SeatPlan
	{
		int plannedTurn;       // due from the next game turn, so a turn that reopens this round never runs it early
		std::vector<McpPlannedOrder> orders;
		std::string aim;       // what in the game plan the turn serves, or the course held and until when
		std::string note;
		McpView view;          // what the seat could see when it planned
		int viewTurn;          // the game turn of the turn view it was planned from
		bool changesShown;
		std::vector<std::string> changes; // what changed, as shown at the turn's start
		std::string held;      // why the turn was held for the seat when it opened, once it has been
		SeatPlan() : plannedTurn(0), viewTurn(-1), changesShown(false) {}
	};

	std::map<int, SeatPlan> s_plans;
	volatile LONG s_owesPlan[MAX_PLAYERS] = { 0 };
	std::map<int, int> s_lastEnded; // the game turn each seat last ended since the game loaded
	std::map<int, int> s_landShown; // the game turn each seat was last shown its land
	std::map<int, int> s_viewShown; // the game turn each seat was last shown its turn view
	std::map<int, std::vector<McpViewItem> > s_viewAtEnd; // what each seat could see when it last ended a turn
	std::map<int, std::map<std::string, std::string> > s_screensAtEnd; // the screens left to tools, as they were then
	std::map<int, std::string> s_viewText;  // the turn view each seat was last shown, sent again on a later wait that turn
	// Round-start planning, for a major civilization's seat: it plans its turn from the view it opens once the round is
	// open, before its turn.
	std::map<int, int> s_viewOnTurn;   // the game turn each seat was last shown its view with its turn open
	std::map<int, int> s_roundAsked;   // the game turn each seat was last asked to plan its round
	std::map<int, int> s_roundViewTurn; // the game turn of each seat's last view before its turn
	std::map<int, std::vector<McpViewItem> > s_viewAtRound; // what it could see then
	std::map<int, std::map<std::string, std::string> > s_screensAtRound;

	bool Major(const CvPlayer* me) { return (int)me->GetID() < MAX_MAJOR_CIVS; }

	// Waiting, planning and the seat's own housekeeping are not orders.
	bool Plannable(const std::string& tool)
	{
		const char* const never[] = { "wait_for_my_turn", MCP_TOOL_PLAN_TURN, MCP_TOOL_RUN_PLAN, "advance_round", "skip_human_turn",
			"take_seat", "release_seat", "set_identity", "testing_mode" };
		for(size_t i = 0; i < sizeof(never) / sizeof(never[0]); ++i) if(tool == never[i]) return false;
		return true;
	}

	bool ParseOrders(const JArr& list, std::vector<McpPlannedOrder>& out, std::string& err)
	{
		for(size_t i = 0; i < list.size(); ++i)
		{
			if(!list[i].is<JObj>()) { err = McpFormat("order %d: each order is {tool, args, serves}", (int)i + 1); return false; }
			const JObj& o = list[i].get<JObj>();
			std::string tool = ArgStr(o, "tool");
			const McpToolDef* def = McpFindTool(tool);
			if(!def) { err = McpFormat("order %d: unknown tool '%s'", (int)i + 1, tool.c_str()); return false; }
			if(!Plannable(tool)) { err = McpFormat("order %d: %s does not go in a plan", (int)i + 1, tool.c_str()); return false; }
			JObj a;
			JObj::const_iterator ai = o.find("args");
			if(ai != o.end())
			{
				if(!ai->second.is<JObj>()) { err = McpFormat("order %d: args must be an object", (int)i + 1); return false; }
				a = ai->second.get<JObj>();
			}
			std::string bad = McpCheckArgs(*def, a);
			if(!bad.empty()) { err = McpFormat("order %d (%s): %s", (int)i + 1, tool.c_str(), bad.c_str()); return false; }
			McpPlannedOrder p;
			p.tool = tool;
			p.args = JVal(a).serialize();
			p.serves = ArgStr(o, "serves");
			if(p.serves.empty() && tool != "end_turn")
			{
				err = McpFormat("order %d (%s): serves is required: the part of your plan this order serves, and how", (int)i + 1, tool.c_str());
				return false;
			}
			out.push_back(p);
		}
		return true;
	}

	JArr OrdersJson(const std::vector<McpPlannedOrder>& orders)
	{
		JArr list;
		for(size_t i = 0; i < orders.size(); ++i)
		{
			JObj o;
			o["n"] = J((int)i + 1);
			o["tool"] = J(orders[i].tool);
			JVal a;
			picojson::parse(a, orders[i].args);
			o["args"] = a;
			if(!orders[i].serves.empty()) o["serves"] = J(orders[i].serves);
			list.push_back(JVal(o));
		}
		return list;
	}

	JArr Lines(const std::vector<std::string>& lines)
	{
		JArr list;
		for(size_t i = 0; i < lines.size(); ++i) list.push_back(J(lines[i]));
		return list;
	}

	std::vector<int> Ints(const JObj& args, const char* key)
	{
		std::vector<int> out;
		JObj::const_iterator it = args.find(key);
		if(it == args.end() || !it->second.is<JArr>()) return out;
		const JArr& list = it->second.get<JArr>();
		for(size_t i = 0; i < list.size(); ++i) if(list[i].is<double>()) out.push_back((int)list[i].get<double>());
		return out;
	}

	McpSeenUnit Seen(CvUnit* u, const std::string& who)
	{
		McpSeenUnit s;
		s.id = u->GetID();
		s.owner = (int)u->getOwner();
		s.who = who;
		s.type = UnitTypeName(u->getUnitType());
		s.x = u->getX();
		s.y = u->getY();
		s.hp = u->GetCurrHitPoints();
		return s;
	}

	McpView Capture(CvPlayer* me)
	{
		McpView v;
		TeamTypes eTeam = me->getTeam();
		v.gold = me->GetTreasury()->GetGold();
		v.happiness = me->GetExcessHappiness();
		int iLoop;
		for(CvUnit* u = me->firstUnit(&iLoop); u; u = me->nextUnit(&iLoop)) v.mine.push_back(Seen(u, "you"));
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
		{
			McpSeenCity sc;
			sc.id = c->GetID();
			sc.name = c->getName().c_str();
			sc.maxHp = c->GetMaxHitPoints();
			sc.hp = sc.maxHp - c->getDamage();
			v.cities.push_back(sc);
		}
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || i == (int)me->GetID()) continue;
			bool bEnemy = p.isBarbarian() || GET_TEAM(eTeam).isAtWar(p.getTeam());
			if(!p.isBarbarian() && bEnemy) v.atWarWith.push_back(CivName((PlayerTypes)i));
			std::string who = p.isBarbarian() ? std::string("Barbarians") : CivName((PlayerTypes)i);
			for(CvUnit* u = p.firstUnit(&iLoop); u; u = p.nextUnit(&iLoop))
			{
				CvPlot* plot = u->plot();
				if(!plot || !plot->isVisible(eTeam) || u->isInvisible(eTeam, false)) continue;
				McpSeenUnit s = Seen(u, who);
				s.enemy = bEnemy;
				s.cityState = p.isMinorCiv();
				for(size_t k = 0; k < v.mine.size(); ++k)
				{
					int d = plotDistance(s.x, s.y, v.mine[k].x, v.mine[k].y);
					if(s.nearDist < 0 || d < s.nearDist) { s.nearDist = d; s.nearWhat = "your " + v.mine[k].type; }
				}
				int iCityLoop;
				for(CvCity* c = me->firstCity(&iCityLoop); c; c = me->nextCity(&iCityLoop))
				{
					int d = plotDistance(s.x, s.y, c->getX(), c->getY());
					if(s.nearDist < 0 || d < s.nearDist) { s.nearDist = d; s.nearWhat = c->getName().c_str(); }
				}
				v.theirs.push_back(s);
			}
		}
		return v;
	}

	int LastEnded(int seat)
	{
		std::map<int, int>::const_iterator it = s_lastEnded.find(seat);
		return it == s_lastEnded.end() ? -1 : it->second;
	}

	bool Due(const SeatPlan& p)
	{
		return GC.getGame().getGameTurn() > p.plannedTurn;
	}

	bool HasDuePlan(int seat)
	{
		std::map<int, SeatPlan>::const_iterator it = s_plans.find(seat);
		return it != s_plans.end() && Due(it->second);
	}

	// A major seat whose round is open and whose turn has not opened yet.
	bool BeforeItsTurn(CvPlayer* me)
	{
		return Major(me) && me->isAlive() && me->isHuman() && !McpIsPlayerTurn(*me) && McpRoundOpen(GC.getGame().getGameTurn(), LastEnded((int)me->GetID()));
	}

	void ShowChanges(CvPlayer* me, SeatPlan& p)
	{
		p.changes = McpViewChanges(p.view, Capture(me), MCP_CHANGES_SHOWN);
		p.changesShown = true;
	}
}

namespace
{
	// The game plan each seat keeps, by player; after a load it is read back from the game's plans folder.
	std::map<int, McpGamePlan> s_gamePlans;

	std::string PlanFile(CvPlayer* me) { return std::string(me->getCivilizationShortDescription()) + ".jsonl"; }

	McpGamePlan& GamePlan(CvPlayer* me)
	{
		std::map<int, McpGamePlan>::iterator found = s_gamePlans.find((int)me->GetID());
		if(found != s_gamePlans.end()) return found->second;
		McpGamePlan& plan = s_gamePlans[(int)me->GetID()];
		std::istringstream lines(McpGameFileRead(McpGameId(), "plans", PlanFile(me)));
		std::string line;
		while(std::getline(lines, line))
		{
			JVal v;
			if(!picojson::parse(v, line).empty() || !v.is<JObj>()) continue;
			const JObj& o = v.get<JObj>();
			JObj::const_iterator si = o.find("sections");
			if(si == o.end() || !si->second.is<JObj>()) continue;
			plan.sections.clear();
			const JObj& sections = si->second.get<JObj>();
			for(JObj::const_iterator k = sections.begin(); k != sections.end(); ++k)
				if(k->second.is<std::string>()) plan.sections[k->first] = k->second.get<std::string>();
			plan.written.clear();
			JObj::const_iterator wi = o.find("written");
			if(wi != o.end() && wi->second.is<JObj>())
			{
				const JObj& written = wi->second.get<JObj>();
				for(JObj::const_iterator k = written.begin(); k != written.end(); ++k) plan.written[k->first] = ArgInt(written, k->first.c_str(), -1);
			}
			JObj::const_iterator ti = o.find("targets");
			if(ti != o.end())
			{
				std::string ignored;
				McpParseTargets(ti->second, -1000000, plan.targets, ignored);
			}
			plan.turn = ArgInt(o, "turn", plan.turn);
			std::string entry = ArgStr(o, "log");
			if(!entry.empty()) plan.log.push_back(entry);
		}
		McpMigratePlan(plan);
		return plan;
	}

	void SaveGamePlan(CvPlayer* me, const McpGamePlan& plan)
	{
		JObj o;
		o["turn"] = J(plan.turn);
		o["log"] = J(plan.log.empty() ? std::string() : plan.log.back());
		JObj sections;
		for(std::map<std::string, std::string>::const_iterator it = plan.sections.begin(); it != plan.sections.end(); ++it)
			sections[it->first] = J(it->second);
		o["sections"] = JVal(sections);
		JObj written;
		for(std::map<std::string, int>::const_iterator it = plan.written.begin(); it != plan.written.end(); ++it) written[it->first] = J(it->second);
		o["written"] = JVal(written);
		JArr targets;
		for(size_t i = 0; i < plan.targets.size(); ++i)
		{
			const McpTarget& t = plan.targets[i];
			JObj to;
			to["by"] = J(t.by);
			if(!t.city.empty()) to["city"] = J(t.city);
			if(t.kind == "tech") to["tech"] = J(t.subject);
			else if(t.kind == "build") to["build"] = J(t.subject);
			else if(t.kind == "size") to["size"] = J(t.amount);
			else if(t.kind == "gold") to["gold"] = J(t.amount);
			else to["policy"] = J("next");
			targets.push_back(JVal(to));
		}
		o["targets"] = JVal(targets);
		McpGameFileAppend(McpGameId(), "plans", PlanFile(me), "", JVal(o).serialize() + "\n");
	}

	std::string Turns(int n) { return McpFormat(n == 1 ? "%d turn" : "%d turns", n); }

	// What the next few turns bring, from the game's own counts: each city's build and growth, research, the next
	// policy and gold.
	JArr Coming(CvPlayer* me)
	{
		JArr out;
		int iLoop;
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
		{
			std::string line = std::string(c->getName().c_str()) + ": ";
			if(c->getOrderQueueLength() == 0) line += "nothing being built";
			else if(c->isProductionProcess()) line += std::string(c->getProductionName()) + " (ongoing)";
			else
			{
				int left = c->getProductionTurnsLeft();
				line += std::string(c->getProductionName()) + " in " + Turns(left);
				if(c->getOrderQueueLength() > 1) line += ", then " + std::string(OrderName(c->getOrderFromQueue(1)));
				else if(left <= 1) line += ", nothing queued after it";
			}
			int food = c->foodDifference();
			if(food < 0) line += "; starving";
			else if(food == 0) line += "; not growing";
			else line += "; grows in " + Turns(c->getFoodTurnsLeft());
			out.push_back(J(line));
		}
		TechTypes eTech = me->GetPlayerTechs()->GetCurrentResearch();
		if(eTech == NO_TECH) out.push_back(J("research: nothing chosen"));
		else
		{
			int left = me->GetPlayerTechs()->GetResearchTurnsLeft(eTech, true);
			out.push_back(J(std::string("research: ") + GC.getTechInfo(eTech)->GetDescription() + " in " + Turns(left) + (left <= 1 ? ", choose what comes after" : "")));
		}
		int cost = me->getNextPolicyCost(), culture = me->getJONSCulture(), perTurn = me->GetTotalJONSCulturePerTurn();
		int policy = McpTurnsToAfford(cost, culture, perTurn);
		out.push_back(J(policy < 0 ? McpFormat("next policy: %d of %d culture, none coming in", culture, cost)
			: McpFormat("next policy in %s (%d of %d culture, +%d a turn)", Turns(policy).c_str(), culture, cost, perTurn)));
		out.push_back(J(McpFormat("gold %d, %+d a turn", me->GetTreasury()->GetGold(), me->calculateGoldRate())));
		return out;
	}

	// The tech research finishes before the next turn opens, or NO_TECH.
	TechTypes TechDueNextTurn(CvPlayer* me)
	{
		TechTypes eTech = me->GetPlayerTechs()->GetCurrentResearch();
		return (eTech != NO_TECH && me->GetPlayerTechs()->GetResearchTurnsLeft(eTech, true) <= 1) ? eTech : NO_TECH;
	}

	// Orders that would not work when the turn opens, as far as the game can tell now: purchases against the gold the
	// turn will open with (and what earlier purchases in the plan spent), builds the city cannot take unless the tech
	// finishing first unlocks them, and moves with no way there. Warnings only; the plan is kept either way.
	std::vector<std::string> CheckOrders(CvPlayer* me, const std::vector<McpPlannedOrder>& orders)
	{
		std::vector<std::string> out;
		int rate = me->calculateGoldRate(), gold = me->GetTreasury()->GetGold() + rate;
		int faithRate = me->GetTotalFaithPerTurn(), faith = me->GetFaith() + faithRate;
		TechTypes eDue = TechDueNextTurn(me);
		std::set<int> moved;
		for(size_t i = 0; i < orders.size(); ++i)
		{
			JVal v;
			picojson::parse(v, orders[i].args);
			if(!v.is<JObj>()) continue;
			const JObj& a = v.get<JObj>();
			const std::string& tool = orders[i].tool;
			std::string head = McpFormat("order %d (%s): ", (int)i + 1, tool.c_str());
			if(tool == "buy" || tool == "set_production")
			{
				CvCity* c = me->getCity(ArgInt(a, "city_id", -1));
				if(!c) { out.push_back(head + "no city of yours has that city_id"); continue; }
				CityItem item = FindCityItem(me, ArgStr(a, "item"));
				if(item.eUnit == NO_UNIT && item.eBuilding == NO_BUILDING) continue;
				bool bBuy = tool == "buy";
				std::string why = item.eUnit != NO_UNIT ? TrainRefusal(c, item.eUnit, bBuy) : BuildRefusal(c, item.eBuilding, bBuy);
				int prereq = item.eUnit != NO_UNIT ? GC.getUnitInfo(item.eUnit)->GetPrereqAndTech() : GC.getBuildingInfo(item.eBuilding)->GetPrereqAndTech();
				if(!why.empty() && !(eDue != NO_TECH && prereq == (int)eDue))
				{
					out.push_back(head + c->getName().c_str() + " cannot take it as things stand: " + why);
					continue;
				}
				if(!bBuy) continue;
				bool bFaith = ArgBool(a, "with_faith", false);
				int cost = bFaith ? (item.eUnit != NO_UNIT ? c->GetFaithPurchaseCost(item.eUnit, true) : c->GetFaithPurchaseCost(item.eBuilding))
					: (item.eUnit != NO_UNIT ? c->GetPurchaseCost(item.eUnit) : c->GetPurchaseCost(item.eBuilding));
				int& have = bFaith ? faith : gold;
				int perTurn = bFaith ? faithRate : rate;
				const char* what = bFaith ? "faith" : "gold";
				if(cost <= 0) { out.push_back(head + std::string("it cannot be bought with ") + what + " there"); continue; }
				if(cost > have)
				{
					int wait = McpTurnsToAfford(cost, have, perTurn);
					out.push_back(head + McpFormat("costs %d %s; the turn opens with %d after earlier purchases in this plan (%+d a turn)%s",
						cost, what, have, perTurn, wait < 0 ? ", and that never covers it" : McpFormat(", enough in %s more", Turns(wait).c_str()).c_str()));
					continue;
				}
				have -= cost;
			}
			else if(tool == "move_unit")
			{
				int id = ArgInt(a, "unit_id", -1);
				CvUnit* u = me->getUnit(id);
				if(!u) { out.push_back(head + "no unit of yours has that unit_id"); continue; }
				if(moved.count(id)) continue;
				moved.insert(id);
				std::vector<CvPlot*> steps;
				std::string why;
				if(!ReadPath(a, u, steps, why)) { out.push_back(head + why); continue; }
				why = MoveRefusal(u, steps.back());
				if(!why.empty()) out.push_back(head + "as things stand: " + why);
			}
			else if(tool == "unit_action")
				moved.insert(ArgInt(a, "unit_id", -1));
		}
		return out;
	}

	// What a plan leaves for the seat to decide when its turn opens, as far as the game can tell now: a unit ready
	// with no order, a city with nothing to build or about to finish with nothing after, research not chosen.
	std::vector<std::string> LeftOpen(CvPlayer* me, const std::vector<McpPlannedOrder>& orders)
	{
		const size_t LIMIT = 12;
		std::vector<std::string> out;
		std::set<int> units, cities;
		bool bResearch = false;
		for(size_t i = 0; i < orders.size(); ++i) if(orders[i].tool == "set_research") bResearch = true;
		for(size_t i = 0; i < orders.size(); ++i)
		{
			JVal v;
			picojson::parse(v, orders[i].args);
			if(!v.is<JObj>()) continue;
			int unit = ArgInt(v.get<JObj>(), "unit_id", -1), city = ArgInt(v.get<JObj>(), "city_id", -1);
			if(unit >= 0) units.insert(unit);
			if(city >= 0 && (orders[i].tool == "set_production" || orders[i].tool == "buy")) cities.insert(city);
		}
		int iLoop;
		for(CvUnit* u = me->firstUnit(&iLoop); u; u = me->nextUnit(&iLoop))
			if(u->ReadyToMove() && !u->isDelayedDeath() && !units.count(u->GetID()))
				out.push_back(McpFormat("%s %d at (%d,%d) has no order", u->getName().GetCString(), u->GetID(), u->getX(), u->getY()));
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
		{
			if(c->IsPuppet() || cities.count(c->GetID())) continue;
			if(c->getOrderQueueLength() == 0) out.push_back(std::string(c->getName().c_str()) + " has nothing to build");
			else if(c->getOrderQueueLength() == 1 && !c->isProductionProcess() && c->getProductionTurnsLeft() <= 1)
				out.push_back(std::string(c->getName().c_str()) + " finishes " + c->getProductionName() + " as the turn opens, with nothing queued after it");
		}
		if(!bResearch)
		{
			TechTypes eTech = me->GetPlayerTechs()->GetCurrentResearch();
			if(eTech == NO_TECH) out.push_back("research: nothing chosen");
			else if(TechDueNextTurn(me) != NO_TECH)
				out.push_back(std::string("research: ") + GC.getTechInfo(eTech)->GetDescription() + " finishes as the turn opens; what comes after is not chosen");
		}
		if(out.size() > LIMIT)
		{
			size_t more = out.size() - LIMIT;
			out.resize(LIMIT);
			out.push_back(McpFormat("...and %d more", (int)more));
		}
		return out;
	}
}

void McpAddPlanningView(CvPlayer* me, picojson::object& o)
{
	o["coming"] = JVal(Coming(me));
	McpGamePlan& plan = GamePlan(me);
	if(plan.turn >= 0) o["your_game_plan"] = J(McpGamePlanText(plan));
	else o["your_game_plan"] = J("none yet: give plan_turn a plan (direction at least)");
}

MCP_TOOL(ToolPlanTurn)
{
	CvPlayer* me = Me(err);
	if(!me || !RequireIdentity(err)) return JVal();
	JObj::const_iterator it = args.find("orders");
	if(it == args.end() || !it->second.is<JArr>())
	{
		err = "orders is required: the turn's orders, in order, each {tool, args, serves}; [] for none yet";
		return JVal();
	}
	// A major seat plans a turn from this round's turn view: once the round is open, or with its turn open.
	bool bMajor = Major(me);
	if(bMajor)
	{
		int seat = (int)me->GetID();
		if(!McpIsPlayerTurn(*me) && !BeforeItsTurn(me))
		{
			err = "DO NOW: call wait_for_my_turn. Your next turn is planned when the next round opens, once everyone else has moved; the wait returns then.";
			return JVal();
		}
		std::map<int, int>::const_iterator seen = s_viewShown.find(seat);
		if(seen == s_viewShown.end() || seen->second != GC.getGame().getGameTurn())
		{
			err = "DO NOW: call turn_view. A turn is planned from this round's turn view.";
			return JVal();
		}
	}
	SeatPlan plan;
	if(!ParseOrders(it->second.get<JArr>(), plan.orders, err)) return JVal();
	plan.aim = ArgStr(args, "aim");
	if(plan.aim.empty())
	{
		err = "aim is required: what in your game plan this turn moves, or the course you are holding, until when, and what would end it";
		return JVal();
	}
	int iTurn = GC.getGame().getGameTurn();
	std::vector<std::string> warnings;
	McpGamePlan& game = GamePlan(me);
	bool bPlanChanged = false;
	JObj::const_iterator pi = args.find("plan");
	if(pi != args.end())
	{
		if(!pi->second.is<JObj>()) { err = "plan must be an object of headings: position, direction, next_turns, cities, military, civilians, research, policy, relations"; return JVal(); }
		std::map<std::string, std::string> changes;
		const JObj& given = pi->second.get<JObj>();
		for(JObj::const_iterator k = given.begin(); k != given.end(); ++k)
		{
			if(!k->second.is<std::string>()) { err = "plan." + k->first + " must be text"; return JVal(); }
			changes[k->first] = k->second.get<std::string>();
		}
		int before = game.turn;
		size_t logBefore = game.log.size();
		if(!McpGamePlanApply(game, changes, ArgStr(args, "why"), iTurn, err, warnings)) return JVal();
		bPlanChanged = game.log.size() != logBefore || game.turn != before;
		if(bPlanChanged) SaveGamePlan(me, game);
	}
	JObj::const_iterator ti = args.find("targets");
	if(ti != args.end())
	{
		std::vector<McpTarget> targets;
		if(!McpParseTargets(ti->second, iTurn, targets, err)) return JVal();
		game.targets = targets;
		SaveGamePlan(me, game);
	}
	if(pi == args.end() && game.turn < 0)
		warnings.push_back("you have no game plan yet: give plan_turn a plan with at least direction (how you are optimizing your odds of winning from here)");
	std::vector<std::string> checks = CheckOrders(me, plan.orders);
	warnings.insert(warnings.end(), checks.begin(), checks.end());
	// A major seat's plan is for the turn of the round it is made in; a city-state's is for its next turn.
	plan.plannedTurn = bMajor ? iTurn - 1 : McpPlanMadeOnTurn(iTurn, McpIsPlayerTurn(*me), LastEnded(me->GetID()));
	plan.viewTurn = iTurn;
	plan.note = ArgStr(args, "note");
	plan.view = Capture(me);
	std::vector<std::string> open;
	if(bMajor) open = LeftOpen(me, plan.orders);
	bool bReplaced = s_plans.count((int)me->GetID()) > 0;
	s_plans[(int)me->GetID()] = plan;
	InterlockedExchange(&s_owesPlan[me->GetID()], 0);
	if(McpPlanJournalOn())
	{
		JObj journal;
		journal["event"] = J("plan_turn");
		journal["game"] = J(McpGameId());
		journal["turn"] = J(iTurn);
		journal["civ"] = J(me->getCivilizationShortDescription());
		journal["aim"] = J(plan.aim);
		journal["orders"] = J((int)plan.orders.size());
		journal["game_plan_changed"] = J(bPlanChanged);
		if(bPlanChanged) journal["why"] = J(ArgStr(args, "why"));
		journal["warnings"] = JVal(Lines(warnings));
		McpPlanJournalAppend(JVal(journal).serialize());
	}
	JObj o;
	o["ok"] = J(true);
	o["orders"] = J((int)plan.orders.size());
	if(bReplaced) o["replaced"] = J("your earlier plan");
	if(bPlanChanged) o["game_plan"] = J("saved: each turn view shows each part beside the state it is about");
	if(!warnings.empty()) o["warnings"] = JVal(Lines(warnings));
	if(!open.empty()) o["left_open"] = JVal(Lines(open));
	if(!bMajor)
		o["next"] = J(warnings.empty() ? "call wait_for_my_turn; when your turn starts, call turn_view"
			: "fix what the warnings show with another plan_turn if it matters, then call wait_for_my_turn");
	else if(warnings.empty() && open.empty())
		o["next"] = J("call wait_for_my_turn: when your turn opens these orders run and the turn ends by itself, unless something that matters has changed; then the turn comes to you");
	else
		o["next"] = J("what warnings and left_open show brings the turn to you when it opens instead of running by itself: cover it with another plan_turn, "
			"or leave it if you mean to decide then. Then call wait_for_my_turn");
	return JVal(o);
}

MCP_TOOL(ToolWaitForMyTurn)
{
	JVal v = ToolStatus(args, err);
	if(!err.empty() || !v.is<JObj>()) return v;
	std::string why;
	CvPlayer* me = Me(why);
	if(me && McpIsPlayerTurn(*me)) v.get<JObj>()[MCP_DO_NOW] = J("call turn_view, before any other tool. Your turn has started.");
	else if(me && McpRoundAwaitsPlan(me))
	{
		s_roundAsked[(int)me->GetID()] = GC.getGame().getGameTurn();
		v.get<JObj>()[MCP_DO_NOW] = J("call turn_view, then send this turn's orders with plan_turn. The round has opened: everyone else has moved, "
			"and your turn opens once the players before you have played.");
	}
	return v;
}

MCP_TOOL(ToolCityDetail)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	return McpCityDetailText(me, ArgInt(args, "city_id", -1), err);
}

MCP_TOOL(ToolTurnView)
{
	CvPlayer* me = Me(err);
	if(!me || !RequireIdentity(err)) return JVal();
	bool bMine = McpIsPlayerTurn(*me);
	if(!bMine && !BeforeItsTurn(me)) { err = "it is not your turn right now; call wait_for_my_turn"; return JVal(); }
	JObj o;
	McpAddPlanToTurnStart(me, o);
	if(!bMine)
		o[MCP_DO_NOW] = J(HasDuePlan((int)me->GetID()) ? "call wait_for_my_turn. Your plan for this turn is in; send plan_turn again only to change it."
			: "send this turn's orders with plan_turn: every order for the turn, in order. A complete plan runs by itself when your turn opens.");
	return JVal(o);
}

namespace
{
	std::string Upper(const std::string& s)
	{
		std::string u = s;
		for(size_t i = 0; i < u.size(); ++i) if(u[i] >= 'a' && u[i] <= 'z') u[i] = (char)(u[i] - 'a' + 'A');
		return u;
	}

	std::string Group(const std::string& section)
	{
		size_t colon = section.find(':');
		return colon == std::string::npos ? section : section.substr(0, colon);
	}

	// The plan heading shown after a group of sections.
	const char* PlanAfter(const std::string& group)
	{
		if(group == "your military units") return "military";
		if(group == "cities") return "cities";
		if(group == "your civilians") return "civilians";
		if(group == "research") return "research";
		if(group == "policies") return "policy";
		if(group == "players") return "relations";
		return NULL;
	}

	// The units and cities a plan's orders name.
	void PlannedIds(const std::vector<McpPlannedOrder>& orders, std::set<int>& units, std::set<int>& cities)
	{
		for(size_t i = 0; i < orders.size(); ++i)
		{
			JVal v;
			picojson::parse(v, orders[i].args);
			if(!v.is<JObj>()) continue;
			int unit = ArgInt(v.get<JObj>(), "unit_id", -1), city = ArgInt(v.get<JObj>(), "city_id", -1);
			if(unit >= 0) units.insert(unit);
			if(city >= 0) cities.insert(city);
		}
	}

	std::string JoinLines(const std::vector<std::string>& lines, const char* none)
	{
		if(lines.empty()) return std::string("  ") + none + "\n";
		std::string out;
		for(size_t i = 0; i < lines.size(); ++i) out += "  " + lines[i] + "\n";
		return out;
	}

	// The whole turn view as text: what happened and what the game waits on first, then each section with what changed
	// marked and each part of the game plan after the state it is about, then the screens left to tools that changed,
	// and what has no plan or no order.
	std::string TurnViewText(CvPlayer* me, const McpTurnViewData& d, const McpGamePlan& plan, const std::vector<McpViewItem>* before,
		const std::map<std::string, std::string>* screensBefore, const std::set<int>& plannedUnits, const std::set<int>& plannedCities,
		int& added, int& changed, int& gone, bool bBeforeTurn, bool bSinceRoundView)
	{
		const char* since = bSinceRoundView ? "the view you planned from" : "your last turn";
		std::vector<McpViewItem> none, items = d.items;
		std::set<std::string> ticking = d.ticking;
		if(before)
		{
			McpAddFallClocks(*before, items);
			ticking.insert("falling");
		}
		std::vector<McpViewSection> view = McpMarkView(before ? *before : none, items, ticking, before != NULL);
		std::string out = McpFormat("TURN %d · %s (%s)\n", GC.getGame().getGameTurn(), me->getCivilizationShortDescription(), me->getName());
		int iTurn = GC.getGame().getGameTurn();
		if(bBeforeTurn)
			out += "BEFORE YOUR TURN: the round as it stands now that everyone else has moved. This turn's income, production, growth and research "
				"are applied when your turn opens; your units have their moves already.\n";
		out += McpTurnFramingTop(McpLastPlanTurn(plan)) + "\n\n";
		if(before) out += std::string("Marks: + new since ") + since + " · ~ changed, the old value in (was ...) · - gone\n";
		std::string codes;
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || i == (int)me->GetID()) continue;
			if(!p.isBarbarian() && !GET_TEAM(me->getTeam()).isHasMet(p.getTeam())) continue;
			codes += (codes.empty() ? "" : " · ") + std::string("@") + CivCode((PlayerTypes)i) + " " + CivName((PlayerTypes)i);
		}
		out += "Codes: @" + CivCode(me->GetID()) + " you" + (codes.empty() ? std::string() : " · " + codes) + "\n";
		out += "Units: [hp strength moves], ranged [hp strength r<ranged>/<range> moves], L level\n";
		out += "Clocks: turns at the current rate · need: more per turn to finish one turn sooner · slack: how much less per turn "
			"still finishes on the same turn · tiles: f food, p production, g gold, s science, c culture, fa faith.\n";
		out += "\nYOUR PLAN\n" + McpGamePlanBlock(plan, iTurn);
		if(plan.targets.empty()) out += "  targets: none\n";
		for(size_t i = 0; i < plan.targets.size(); ++i) out += "  target: " + McpTargetStatus(me, plan.targets[i]) + "\n";
		std::vector<std::string> news = d.news;
		for(size_t i = 0; i < d.state.size(); ++i) news.push_back("state: " + d.state[i]);
		out += "\nSINCE " + Upper(since) + "\n" + JoinLines(news, "nothing reported");
		if(!d.blocks.empty()) out += "\nEND TURN BLOCKED UNTIL\n" + JoinLines(d.blocks, "");
		if(!d.fromOthers.empty()) out += "\nFROM OTHER PLAYERS\n" + JoinLines(d.fromOthers, "");
		if(!d.available.empty()) out += "\nAVAILABLE THIS TURN (none of it required)\n" + JoinLines(d.available, "");
		added = changed = gone = 0;
		for(size_t i = 0; i < view.size(); ++i)
		{
			out += "\n" + Upper(view[i].name) + "\n";
			for(size_t k = 0; k < view[i].lines.size(); ++k)
			{
				const std::string& line = view[i].lines[k];
				if(line.compare(0, 2, "+ ") == 0) ++added;
				else if(line.compare(0, 2, "~ ") == 0) ++changed;
				else if(line.compare(0, 2, "- ") == 0) ++gone;
				out += line + "\n";
			}
		}
		std::string changedScreens, sameScreens;
		for(std::map<std::string, std::string>::const_iterator it = d.screens.begin(); it != d.screens.end(); ++it)
		{
			bool bChanged = screensBefore && (!screensBefore->count(it->first) || screensBefore->find(it->first)->second != it->second);
			std::string& list = bChanged ? changedScreens : sameScreens;
			list += (list.empty() ? "" : ", ") + it->first;
		}
		out += "\nOTHER SCREENS (each is a tool)\n";
		if(!screensBefore) out += "  " + sameScreens + "\n";
		else
		{
			out += std::string("  changed since ") + since + ": " + (changedScreens.empty() ? std::string("none") : changedScreens) + "\n";
			if(!sameScreens.empty()) out += "  unchanged: " + sameScreens + "\n";
		}
		(void)plannedUnits; (void)plannedCities;
		out += "\n" + McpTurnFramingBottom() + "\n";
		return out;
	}
}

void McpAddPlanToTurnStart(CvPlayer* me, picojson::object& o)
{
	McpGamePlan& game = GamePlan(me);
	int iTurn = GC.getGame().getGameTurn();
	int seat = (int)me->GetID();
	std::map<int, SeatPlan>::iterator due = s_plans.find(seat);
	if(due != s_plans.end() && !Due(due->second)) due = s_plans.end();
	// A view before the seat's turn and one with the turn open are two views: the second is gathered again and marked
	// against the first.
	bool bMine = McpIsPlayerTurn(*me);
	std::map<int, int>& shownIn = bMine ? s_viewOnTurn : s_roundViewTurn;
	std::map<int, int>::iterator viewed = shownIn.find(seat);
	if(viewed != shownIn.end() && viewed->second == iTurn) o["turn_view"] = J(s_viewText[seat]);
	else
	{
		bool bSinceRoundView = bMine && s_roundViewTurn.count(seat) && s_roundViewTurn[seat] == iTurn;
		shownIn[seat] = iTurn;
		s_viewShown[seat] = iTurn;
		McpTrail("turn view: gather begins", seat, iTurn);
		McpTurnViewData d = McpGatherTurnView(me);
		McpGatherTurnNews(d);
		McpTrail("turn view: gather done", seat, iTurn);
		std::set<int> plannedUnits, plannedCities;
		if(due != s_plans.end()) PlannedIds(due->second.orders, plannedUnits, plannedCities);
		const std::map<int, std::vector<McpViewItem> >& itemsBefore = bSinceRoundView ? s_viewAtRound : s_viewAtEnd;
		const std::map<int, std::map<std::string, std::string> >& screens = bSinceRoundView ? s_screensAtRound : s_screensAtEnd;
		std::map<int, std::vector<McpViewItem> >::const_iterator before = itemsBefore.find(seat);
		int added = 0, changed = 0, gone = 0;
		std::map<int, std::map<std::string, std::string> >::const_iterator screensBefore = screens.find(seat);
		std::string text = TurnViewText(me, d, game, before == itemsBefore.end() ? NULL : &before->second,
			screensBefore == screens.end() ? NULL : &screensBefore->second, plannedUnits, plannedCities, added, changed, gone, !bMine, bSinceRoundView);
		if(!bMine)
		{
			s_viewAtRound[seat] = d.items;
			s_screensAtRound[seat] = d.screens;
		}
		s_viewText[seat] = text;
		o["turn_view"] = J(text);
		if(McpPlanJournalOn())
		{
			JObj journal;
			journal["event"] = J("turn_view");
			journal["game"] = J(McpGameId());
			journal["turn"] = J(iTurn);
			journal["civ"] = J(me->getCivilizationShortDescription());
			journal["chars"] = J((int)text.size());
			journal["items"] = J((int)d.items.size());
			journal["new"] = J(added);
			journal["changed"] = J(changed);
			journal["gone"] = J(gone);
			McpPlanJournalAppend(JVal(journal).serialize());
		}
	}
	std::map<int, int>::iterator shown = s_landShown.find(seat);
	if((shown == s_landShown.end() || shown->second != iTurn) && !McpLogsFlag("MCPServer-turn-picture.off"))
	{
		s_landShown[seat] = iTurn;
		McpAddLandToTurnStart(me->GetID(), o);
	}
	if(due == s_plans.end() || !bMine) return;
	SeatPlan& p = due->second;
	ShowChanges(me, p);
	if(!p.held.empty())
	{
		o["your_plan_was_held"] = J(p.held);
		o["since_you_planned"] = JVal(Lines(p.changes));
	}
	o["your_aim"] = J(p.aim);
	o["your_plan"] = JVal(OrdersJson(p.orders));
	if(!p.note.empty()) o["your_plan_note"] = J(p.note);
	o["decide"] = J("It is your turn. run_plan carries out the planned orders: drop lists order numbers to leave out, add lists orders to "
		"add (they go before a final end_turn), and unread game messages are acknowledged first. set_aside lets you give orders one at a time "
		"with the other tools.");
	if(McpPlanJournalOn())
		o["explain_changes"] = J("Debug journal on: when you drop, add or set aside, give run_plan why = {saw: what made you change it, in a few words; "
			"kind: one of " + McpWhyKinds() + "; orders: the order numbers it touched}.");
}

picojson::value McpTakePlan(const picojson::object& args, std::string& err, bool bAuto)
{
	CvPlayer* me = Me(err);
	if(!me || !RequireIdentity(err)) return JVal();
	std::map<int, SeatPlan>::iterator it = s_plans.find((int)me->GetID());
	bool bDue = it != s_plans.end() && Due(it->second);
	if(bAuto)
	{
		// The seat's own wait asks, each time its turn looks open: nothing to run is no error.
		if(!McpIsPlayerTurn(*me) || !bDue || !Major(me))
		{
			JObj idle;
			idle["idle"] = J(true);
			return JVal(idle);
		}
		SeatPlan& planned = it->second;
		int iTurn = GC.getGame().getGameTurn();
		std::string hold = McpAutoRunHold(McpLogsFlag("MCPServer-auto-run.off"), planned.viewTurn == iTurn,
			McpWakingChanges(planned.view, Capture(me), MCP_CHANGES_SHOWN), McpCountDiploRequestsFor(me->GetID()));
		if(!hold.empty())
		{
			if(planned.held.empty() && McpPlanJournalOn())
			{
				JObj journal;
				journal["event"] = J("held");
				journal["game"] = J(McpGameId());
				journal["turn"] = J(iTurn);
				journal["civ"] = J(me->getCivilizationShortDescription());
				journal["why"] = J(hold);
				McpPlanJournalAppend(JVal(journal).serialize());
			}
			planned.held = hold;
			JObj held;
			held["held"] = J(hold);
			return JVal(held);
		}
	}
	else
	{
		if(!McpIsPlayerTurn(*me)) { err = "it is not your turn right now; call wait_for_my_turn"; return JVal(); }
		std::map<int, int>::const_iterator seen = s_viewShown.find((int)me->GetID());
		if(seen == s_viewShown.end() || seen->second != GC.getGame().getGameTurn())
		{
			err = "call turn_view first: your planned orders run against this turn as it stands";
			return JVal();
		}
		if(!bDue)
		{
			err = Major(me) ? "you have no plan for this turn: play it with the other tools"
				: "you have no plan for this turn: send one with plan_turn after you end a turn, or play this turn with the other tools";
			return JVal();
		}
	}
	SeatPlan p = it->second;
	if(!p.changesShown) ShowChanges(me, p);
	bool bSetAside = !bAuto && ArgBool(args, "set_aside", false);
	std::vector<int> drop = bAuto ? std::vector<int>() : Ints(args, "drop");
	std::vector<McpPlannedOrder> add;
	JObj::const_iterator ai = args.find("add");
	if(!bAuto && ai != args.end() && ai->second.is<JArr>() && !ParseOrders(ai->second.get<JArr>(), add, err)) { err = "add: " + err; return JVal(); }
	std::vector<McpPlannedOrder> run;
	if(!bSetAside)
	{
		run = McpOrdersToRun(p.orders, drop, add, err);
		if(!err.empty()) return JVal();
	}
	// A plan that runs by itself always tries to end the turn: the game refuses when something is left to decide.
	if(bAuto && (run.empty() || run.back().tool != "end_turn"))
	{
		McpPlannedOrder end;
		end.tool = "end_turn";
		end.args = "{}";
		run.push_back(end);
	}
	s_plans.erase(it);

	JObj journal;
	journal["game"] = J(McpGameId());
	journal["turn"] = J(GC.getGame().getGameTurn());
	journal["player"] = J((int)me->GetID());
	journal["civ"] = J(me->getCivilizationShortDescription());
	journal["planned_on_turn"] = J(p.plannedTurn);
	journal["plan"] = JVal(OrdersJson(p.orders));
	journal["since_you_planned"] = JVal(Lines(p.changes));
	if(bAuto) journal["auto"] = J(true);
	if(bSetAside) journal["set_aside"] = J(true);
	if(!drop.empty())
	{
		JArr d;
		for(size_t i = 0; i < drop.size(); ++i) d.push_back(J(drop[i]));
		journal["drop"] = JVal(d);
	}
	if(!add.empty()) journal["add"] = JVal(OrdersJson(add));
	JObj::const_iterator wi = args.find("why");
	if(wi != args.end()) journal["why"] = wi->second;

	bool bChanged = bSetAside || !drop.empty() || !add.empty();
	JObj o;
	o["orders"] = JVal(OrdersJson(run));
	o["acknowledge_first"] = J(McpBridge::Get().CountUnread(me->GetID()) > 0);
	o["journal"] = JVal(journal);
	o["journal_on"] = J(McpPlanJournalOn());
	o["changed"] = J(bChanged);
	o["why_missing"] = J(bChanged && wi == args.end());
	return JVal(o);
}

void McpNoteTurnEnded(CvPlayer* me)
{
	if(!Major(me)) McpOwePlan(me->GetID());
	s_lastEnded[(int)me->GetID()] = GC.getGame().getGameTurn();
	McpTrail("turn view: end-of-turn snapshot begins", me->GetID(), GC.getGame().getGameTurn());
	McpTurnViewData atEnd = McpGatherTurnView(me);
	s_viewAtEnd[(int)me->GetID()] = atEnd.items;
	s_screensAtEnd[(int)me->GetID()] = atEnd.screens;
	McpTrail("turn view: end-of-turn snapshot done", me->GetID(), GC.getGame().getGameTurn());
	std::map<int, SeatPlan>::iterator it = s_plans.find((int)me->GetID());
	if(it == s_plans.end() || !Due(it->second)) return;
	if(McpPlanJournalOn())
	{
		JObj journal;
		journal["game"] = J(McpGameId());
		journal["turn"] = J(GC.getGame().getGameTurn());
		journal["player"] = J((int)me->GetID());
		journal["civ"] = J(me->getCivilizationShortDescription());
		journal["planned_on_turn"] = J(it->second.plannedTurn);
		journal["plan"] = JVal(OrdersJson(it->second.orders));
		journal["since_you_planned"] = JVal(Lines(it->second.changes));
		journal["set_aside"] = J(true);
		journal["how"] = J("ended the turn without run_plan");
		McpPlanJournalAppend(JVal(journal).serialize());
	}
	s_plans.erase(it);
}

void McpTurnPlanReset()
{
	s_plans.clear();
	s_gamePlans.clear();
	for(int i = 0; i < MAX_PLAYERS; ++i) InterlockedExchange(&s_owesPlan[i], 0);
	s_lastEnded.clear();
	s_landShown.clear();
	s_viewShown.clear();
	s_viewAtEnd.clear();
	s_screensAtEnd.clear();
	s_viewText.clear();
	s_viewOnTurn.clear();
	s_roundAsked.clear();
	s_roundViewTurn.clear();
	s_viewAtRound.clear();
	s_screensAtRound.clear();
}

bool McpRoundAwaitsPlan(CvPlayer* me)
{
	if(!BeforeItsTurn(me) || HasDuePlan((int)me->GetID())) return false;
	std::map<int, int>::const_iterator asked = s_roundAsked.find((int)me->GetID());
	return asked == s_roundAsked.end() || asked->second != GC.getGame().getGameTurn();
}

bool McpPlanIsDue(CvPlayer* me) { return Major(me) && HasDuePlan((int)me->GetID()); }

void McpOwePlan(int seat)
{
	if(seat >= 0 && seat < MAX_PLAYERS) InterlockedExchange(&s_owesPlan[seat], 1);
}

bool McpTakePlanReminder(int seat, bool bMyTurn)
{
	if(seat < 0 || seat >= MAX_PLAYERS) return false;
	return InterlockedExchange(&s_owesPlan[seat], 0) != 0 && !bMyTurn;
}
