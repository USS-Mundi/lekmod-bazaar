#include "CvGameCoreDLLPCH.h"
#include "McpTurnPlan.h"
#include "McpTurnCycle.h"
#include "McpPlanJournal.h"
#include "McpToolSupport.h"
#include "McpToolHandlers.h"
#include "McpTools.h"
#include "McpBridge.h"
#include "McpSeat.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvTeam.h"
#include "CvUnit.h"
#include "CvCity.h"
#include "CvPlot.h"
#include "CvGlobals.h"
#include "CvGameCoreUtils.h"
#include <map>

using namespace McpTool;

namespace
{
	const size_t MCP_CHANGES_SHOWN = 30;

	struct SeatPlan
	{
		int plannedTurn;       // due from the next game turn, so a turn that reopens this round never runs it early
		std::vector<McpPlannedOrder> orders;
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
	plan.plannedTurn = GC.getGame().getGameTurn();
	plan.note = ArgStr(args, "note");
	plan.view = Capture(me);
	bool bReplaced = s_plans.count((int)me->GetID()) > 0;
	s_plans[(int)me->GetID()] = plan;
	InterlockedExchange(&s_owesPlan[me->GetID()], 0);
	JObj o;
	o["ok"] = J(true);
	o["orders"] = J((int)plan.orders.size());
	if(bReplaced) o["replaced"] = J("your earlier plan");
	o["next"] = J("call wait_for_my_turn; when your turn comes it shows this plan and what changed since you made it");
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
	std::map<int, SeatPlan>::iterator it = s_plans.find((int)me->GetID());
	if(it == s_plans.end() || !Due(it->second)) return;
	SeatPlan& p = it->second;
	ShowChanges(me, p);
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
