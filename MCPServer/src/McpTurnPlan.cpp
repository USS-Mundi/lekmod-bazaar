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
		bool changesShown;
		std::vector<std::string> changes; // what changed, as shown at the turn's start
		SeatPlan() : plannedTurn(0), changesShown(false) {}
	};

	std::map<int, SeatPlan> s_plans;
	volatile LONG s_owesPlan[MAX_PLAYERS] = { 0 };

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
			if(!list[i].is<JObj>()) { err = McpFormat("order %d: each order is {tool, args}", (int)i + 1); return false; }
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

	bool Due(const SeatPlan& p)
	{
		return GC.getGame().getGameTurn() > p.plannedTurn;
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
			plan.turn = ArgInt(o, "turn", plan.turn);
			std::string entry = ArgStr(o, "log");
			if(!entry.empty()) plan.log.push_back(entry);
		}
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
				CvPlot* dest = GC.getMap().plot(ArgInt(a, "x", -1), ArgInt(a, "y", -1));
				if(!dest) { out.push_back(head + "that tile is not on the map"); continue; }
				std::string why = MoveRefusal(u, dest);
				if(!why.empty()) out.push_back(head + "as things stand: " + why);
			}
			else if(tool == "unit_action")
				moved.insert(ArgInt(a, "unit_id", -1));
		}
		return out;
	}
}

void McpAddPlanningView(CvPlayer* me, picojson::object& o)
{
	o["coming"] = JVal(Coming(me));
	McpGamePlan& plan = GamePlan(me);
	if(plan.turn >= 0) o["your_game_plan"] = J(McpGamePlanText(plan, true));
	else o["your_game_plan"] = J("none yet: give plan_turn a plan (arc and thrust at least)");
}

MCP_TOOL(ToolPlanTurn)
{
	CvPlayer* me = Me(err);
	if(!me || !RequireIdentity(err)) return JVal();
	JObj::const_iterator it = args.find("orders");
	if(it == args.end() || !it->second.is<JArr>() || it->second.get<JArr>().empty())
	{
		err = "orders is required: the orders you will give next turn, in order, each {tool, args}";
		return JVal();
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
		if(!pi->second.is<JObj>()) { err = "plan must be an object of headings: read, arc, thrust, rivals, tripwires"; return JVal(); }
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
	else if(game.turn < 0)
		warnings.push_back("you have no game plan yet: give plan_turn a plan with at least arc (how you mean to win from here) and thrust (what you are doing about it now)");
	std::vector<std::string> checks = CheckOrders(me, plan.orders);
	warnings.insert(warnings.end(), checks.begin(), checks.end());
	plan.plannedTurn = iTurn;
	plan.note = ArgStr(args, "note");
	plan.view = Capture(me);
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
	if(bPlanChanged) o["game_plan"] = J("saved: it heads every turn from now on");
	if(!warnings.empty()) o["warnings"] = JVal(Lines(warnings));
	o["next"] = J(warnings.empty() ? "call wait_for_my_turn; when your turn comes it shows this plan and what changed since you made it"
		: "fix what the warnings show with another plan_turn if it matters, then call wait_for_my_turn");
	return JVal(o);
}

MCP_TOOL(ToolWaitForMyTurn)
{
	JVal v = ToolStatus(args, err);
	if(!err.empty() || !v.is<JObj>()) return v;
	std::string why;
	CvPlayer* me = Me(why);
	if(me && McpIsPlayerTurn(*me)) McpAddPlanToTurnStart(me, v.get<JObj>());
	return v;
}

void McpAddPlanToTurnStart(CvPlayer* me, picojson::object& o)
{
	std::string err;
	JObj none;
	JVal briefing = ToolBriefing(none, err);
	if(err.empty()) o["briefing"] = briefing;
	McpGamePlan& game = GamePlan(me);
	if(game.turn >= 0) o["your_game_plan"] = J(McpGamePlanText(game, false));
	std::map<int, SeatPlan>::iterator it = s_plans.find((int)me->GetID());
	if(it == s_plans.end() || !Due(it->second)) return;
	SeatPlan& p = it->second;
	ShowChanges(me, p);
	o["your_aim"] = J(p.aim);
	o["your_plan"] = JVal(OrdersJson(p.orders));
	if(!p.note.empty()) o["your_plan_note"] = J(p.note);
	o["since_you_planned"] = JVal(Lines(p.changes));
	o["decide"] = J("Check your plan against what changed (since_you_planned, and the briefing). run_plan carries it out in one call: "
		"drop lists order numbers to leave out, add lists orders to add (they go before a final end_turn), and unread game messages "
		"are acknowledged first. Or play the turn yourself with the other tools after run_plan with set_aside.");
	if(McpPlanJournalOn())
		o["explain_changes"] = J("Debug journal on: when you drop, add or set aside, give run_plan why = {saw: what made you change it, in a few words; "
			"kind: one of " + McpWhyKinds() + "; orders: the order numbers it touched}.");
}

picojson::value McpTakePlan(const picojson::object& args, std::string& err)
{
	CvPlayer* me = Me(err);
	if(!me || !RequireIdentity(err)) return JVal();
	if(!McpIsPlayerTurn(*me)) { err = "it is not your turn right now; call wait_for_my_turn"; return JVal(); }
	std::map<int, SeatPlan>::iterator it = s_plans.find((int)me->GetID());
	if(it == s_plans.end() || !Due(it->second))
	{
		err = "you have no plan for this turn: send one with plan_turn after you end a turn, or play this turn with the other tools";
		return JVal();
	}
	SeatPlan p = it->second;
	if(!p.changesShown) ShowChanges(me, p);
	bool bSetAside = ArgBool(args, "set_aside", false);
	std::vector<int> drop = Ints(args, "drop");
	std::vector<McpPlannedOrder> add;
	JObj::const_iterator ai = args.find("add");
	if(ai != args.end() && ai->second.is<JArr>() && !ParseOrders(ai->second.get<JArr>(), add, err)) { err = "add: " + err; return JVal(); }
	std::vector<McpPlannedOrder> run;
	if(!bSetAside)
	{
		run = McpOrdersToRun(p.orders, drop, add, err);
		if(!err.empty()) return JVal();
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
	McpOwePlan(me->GetID());
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
}

void McpOwePlan(int seat)
{
	if(seat >= 0 && seat < MAX_PLAYERS) InterlockedExchange(&s_owesPlan[seat], 1);
}

bool McpTakePlanReminder(int seat, bool bMyTurn)
{
	if(seat < 0 || seat >= MAX_PLAYERS) return false;
	return InterlockedExchange(&s_owesPlan[seat], 0) != 0 && !bMyTurn;
}
