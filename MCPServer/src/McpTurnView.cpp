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

namespace
{
	typedef std::vector<std::pair<std::string, std::string> > Fields;

	// Fields that move every turn on their own: shown, never counted as a change. Matched on the field's last part.
	const char* const TICKING[] = { "progress", "turns left", "turns to grow", "moves left", "food stored", "stored",
		"gold", "gold per turn", "culture", "culture per turn", "faith", "faith per turn", "science per turn",
		"influence", "score", "turns", "golden age turns left", "experience", "xp" };

	// Shown first, in this order, when an item has them.
	const char* const FIRST[] = { "name", "type", "civilization", "leader", "owner" };

	std::string Spaced(const std::string& key)
	{
		std::string s = key;
		for(size_t i = 0; i < s.size(); ++i) if(s[i] == '_') s[i] = ' ';
		return s;
	}

	std::string Scalar(const JVal& v)
	{
		if(v.is<std::string>()) return v.get<std::string>();
		if(v.is<bool>()) return v.get<bool>() ? "yes" : "no";
		if(v.is<double>())
		{
			int i;
			if(IsInt(v, i)) return McpFormat("%d", i);
			return McpFormat("%.1f", v.get<double>());
		}
		if(v.is<picojson::null>()) return "none";
		return v.serialize();
	}

	// A nested value on one line: objects as {field value, ...}, lists as [a; b].
	std::string Inline(const JVal& v)
	{
		if(v.is<JObj>())
		{
			const JObj& o = v.get<JObj>();
			std::string out;
			for(JObj::const_iterator it = o.begin(); it != o.end(); ++it)
			{
				if(!out.empty()) out += ", ";
				out += Spaced(it->first) + " " + Inline(it->second);
			}
			return "{" + out + "}";
		}
		if(v.is<JArr>())
		{
			const JArr& a = v.get<JArr>();
			bool bScalars = true;
			for(size_t i = 0; i < a.size(); ++i) if(a[i].is<JObj>() || a[i].is<JArr>()) bScalars = false;
			std::string out;
			for(size_t i = 0; i < a.size(); ++i) out += (i ? (bScalars ? ", " : "; ") : "") + Inline(a[i]);
			return bScalars ? out : "[" + out + "]";
		}
		return Scalar(v);
	}

	bool IsFirst(const std::string& k)
	{
		for(size_t i = 0; i < sizeof(FIRST) / sizeof(FIRST[0]); ++i) if(k == FIRST[i]) return true;
		return false;
	}

	// An object's fields, nested objects flattened as "outer.inner", lists inline.
	void AddFields(const std::string& prefix, const JObj& o, Fields& out)
	{
		std::string pre = prefix.empty() ? std::string() : prefix + ".";
		for(size_t i = 0; i < sizeof(FIRST) / sizeof(FIRST[0]); ++i)
		{
			JObj::const_iterator it = o.find(FIRST[i]);
			if(it != o.end() && !it->second.is<JObj>() && !it->second.is<JArr>()) out.push_back(std::make_pair(pre + FIRST[i], Scalar(it->second)));
		}
		for(JObj::const_iterator it = o.begin(); it != o.end(); ++it)
		{
			bool bNested = it->second.is<JObj>() || it->second.is<JArr>();
			if(IsFirst(it->first) && !bNested) continue;
			std::string name = pre + Spaced(it->first);
			if(it->second.is<JObj>()) AddFields(name, it->second.get<JObj>(), out);
			else out.push_back(std::make_pair(name, Inline(it->second)));
		}
	}

	std::string KeyOf(const std::string& section, const JObj& o, size_t index)
	{
		const char* const ids[] = { "id", "city_id", "unit_id", "player_id" };
		for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
		{
			JObj::const_iterator it = o.find(ids[i]);
			if(it != o.end() && !it->second.is<JObj>() && !it->second.is<JArr>()) return section + ":" + ids[i] + ":" + Scalar(it->second);
		}
		JObj::const_iterator x = o.find("x"), y = o.find("y");
		if(x != o.end() && y != o.end()) return section + ":" + Scalar(x->second) + "," + Scalar(y->second);
		const char* const names[] = { "name", "civilization", "type", "tech", "policy", "branch", "resource" };
		for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
		{
			JObj::const_iterator it = o.find(names[i]);
			if(it != o.end() && it->second.is<std::string>()) return section + ":" + it->second.get<std::string>();
		}
		return McpFormat("%s:#%d", section.c_str(), (int)index);
	}

	McpViewItem Item(const std::string& section, const std::string& key, const Fields& fields)
	{
		McpViewItem it;
		it.section = section;
		it.key = key;
		it.fields = fields;
		return it;
	}

	// A screen's result as items: its plain fields as one line, and each list of things one line per thing. A screen
	// with one list and nothing else keeps the section's name; otherwise each list is "section: list".
	void AddScreen(std::vector<McpViewItem>& items, const std::string& section, const JVal& result)
	{
		if(!result.is<JObj>()) return;
		const JObj& o = result.get<JObj>();
		Fields plain;
		std::vector<std::string> lists;
		JObj rest;
		for(JObj::const_iterator it = o.begin(); it != o.end(); ++it)
		{
			bool bList = false;
			if(it->second.is<JArr>())
			{
				const JArr& a = it->second.get<JArr>();
				bList = !a.empty() && a[0].is<JObj>();
			}
			if(bList) lists.push_back(it->first);
			else rest[it->first] = it->second;
		}
		AddFields("", rest, plain);
		if(!plain.empty()) items.push_back(Item(section, section + ":summary", plain));
		for(size_t i = 0; i < lists.size(); ++i)
		{
			std::string name = (lists.size() == 1 && plain.empty()) ? section : section + ": " + Spaced(lists[i]);
			const JArr& a = o.find(lists[i])->second.get<JArr>();
			for(size_t k = 0; k < a.size(); ++k)
			{
				if(!a[k].is<JObj>()) continue;
				Fields f;
				AddFields("", a[k].get<JObj>(), f);
				items.push_back(Item(name, KeyOf(name, a[k].get<JObj>(), k), f));
			}
		}
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

	bool EnemyOf(CvPlayer* me, CvUnit* u)
	{
		return u->getOwner() != me->GetID() && IsEnemyOf(me, u->getOwner());
	}

	// Enemy units in sight, for the reach facts on each of the seat's units.
	std::vector<CvUnit*> EnemiesInSight(CvPlayer* me)
	{
		std::vector<CvUnit*> out;
		TeamTypes eTeam = me->getTeam();
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || i == (int)me->GetID() || !IsEnemyOf(me, (PlayerTypes)i)) continue;
			int iLoop;
			for(CvUnit* u = p.firstUnit(&iLoop); u; u = p.nextUnit(&iLoop))
			{
				CvPlot* plot = u->plot();
				if(plot && plot->isVisible(eTeam) && !u->isInvisible(eTeam, false)) out.push_back(u);
			}
		}
		return out;
	}

	std::string UnitAt(CvUnit* u)
	{
		return McpFormat("%s (%d,%d) %d hp", UnitTypeName(u->getUnitType()), u->getX(), u->getY(), u->GetCurrHitPoints());
	}

	// The seat's units, military and civilian apart, each with the enemies within its range and the enemies within
	// their own move count of it (distance in tiles; terrain and line of sight are not counted).
	void AddUnits(CvPlayer* me, std::vector<McpViewItem>& items, std::vector<std::string>& withoutOrder)
	{
		JVal result = Call(ToolUnits);
		if(!result.is<JObj>()) return;
		JObj::const_iterator li = result.get<JObj>().find("units");
		if(li == result.get<JObj>().end() || !li->second.is<JArr>()) return;
		std::vector<CvUnit*> enemies = EnemiesInSight(me);
		const JArr& list = li->second.get<JArr>();
		for(size_t k = 0; k < list.size(); ++k)
		{
			if(!list[k].is<JObj>()) continue;
			const JObj& o = list[k].get<JObj>();
			Fields f;
			AddFields("", o, f);
			CvUnit* u = me->getUnit(ArgInt(o, "id", -1));
			bool bMilitary = u && u->IsCombatUnit();
			if(u)
			{
				std::string inRange, reachIt;
				int range = u->isRanged() ? u->GetRange() : 1;
				for(size_t e = 0; e < enemies.size(); ++e)
				{
					int d = plotDistance(u->getX(), u->getY(), enemies[e]->getX(), enemies[e]->getY());
					if(bMilitary && d <= range) inRange += (inRange.empty() ? "" : "; ") + UnitAt(enemies[e]);
					if(d <= (int)Moves(enemies[e]->maxMoves())) reachIt += (reachIt.empty() ? "" : "; ") + UnitAt(enemies[e]) + McpFormat(", %d tiles", d);
				}
				if(!inRange.empty()) f.push_back(std::make_pair(std::string("enemies within its range"), inRange));
				if(!reachIt.empty()) f.push_back(std::make_pair(std::string("enemies within their move count of it"), reachIt));
				if(ArgBool(o, "needs_orders", false)) withoutOrder.push_back(McpFormat("%s %d", UnitTypeName(u->getUnitType()), u->GetID()));
			}
			std::string section = bMilitary ? "military units" : "civilian units";
			items.push_back(Item(section, KeyOf("unit", o, k), f));
		}
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

	void AddCities(CvPlayer* me, std::vector<McpViewItem>& items, std::vector<std::string>& withoutOrder, std::vector<std::string>& names)
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
			AddFields("", o, f);
			std::string key = KeyOf("city", o, k);
			items.push_back(Item("cities", key, f));
			CvCity* c = me->getCity(ArgInt(o, "id", -1));
			if(!c) continue;
			names.push_back(c->getName().c_str());
			if(c->getOrderQueueLength() == 0) withoutOrder.push_back(std::string(c->getName().c_str()) + "'s production");
			JObj args;
			args["city_id"] = J(c->GetID());
			JVal citizens = Call(ToolCityCitizens, args);
			if(citizens.is<JObj>())
			{
				Fields cf;
				cf.push_back(std::make_pair(std::string("name"), std::string(c->getName().c_str()) + " citizens"));
				AddFields("", citizens.get<JObj>(), cf);
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
		AddFields("", plain, f);
		items.push_back(Item("empire", "empire", f));
	}

	std::string LastPart(const std::string& name)
	{
		size_t dot = name.rfind('.');
		return dot == std::string::npos ? name : name.substr(dot + 1);
	}

	void AddLines(const JObj& o, const char* key, std::vector<std::string>& out)
	{
		JObj::const_iterator it = o.find(key);
		if(it == o.end()) return;
		if(it->second.is<JArr>())
		{
			const JArr& a = it->second.get<JArr>();
			for(size_t i = 0; i < a.size(); ++i) out.push_back(Inline(a[i]));
		}
		else if(it->second.is<std::string>() && !it->second.get<std::string>().empty()) out.push_back(it->second.get<std::string>());
	}
}

McpTurnViewData McpGatherTurnView(CvPlayer* me)
{
	McpTurnViewData d;
	AddEmpire(d.items);
	AddCities(me, d.items, d.withoutOrder, d.cities);
	AddUnits(me, d.items, d.withoutOrder);
	AddOthersUnits(me, d.items);
	AddScreen(d.items, "research", Call(ToolResearchOptions));
	AddScreen(d.items, "policies", Call(ToolPolicyOptions));
	AddScreen(d.items, "religion", Call(ToolReligionOverview));
	AddPlayers(me, d.items);
	AddScreen(d.items, "civilizations and city-states", Call(ToolCivs));
	AddScreen(d.items, "diplomacy", Call(ToolDiplomacy));
	AddScreen(d.items, "known cities", Call(ToolKnownCities));
	AddScreen(d.items, "rankings", Call(ToolRankings));
	AddScreen(d.items, "economy", Call(ToolEconomicOverview));
	AddScreen(d.items, "military overview", Call(ToolMilitaryOverview));
	AddScreen(d.items, "trade routes", Call(ToolTradeRoutes));
	AddScreen(d.items, "victory", Call(ToolVictoryProgress));
	for(size_t i = 0; i < d.items.size(); ++i)
		for(size_t k = 0; k < d.items[i].fields.size(); ++k)
		{
			const std::string& name = d.items[i].fields[k].first;
			std::string last = LastPart(name);
			for(size_t t = 0; t < sizeof(TICKING) / sizeof(TICKING[0]); ++t)
				if(last == TICKING[t]) { d.ticking.insert(name); break; }
		}
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
