#include "CvGameCoreDLLPCH.h"
#include "McpHistory.h"
#include "McpHistoryFormat.h"
#include "McpPlanJournal.h"
#include "McpToolSupport.h"
#include "McpToolHandlers.h"
#include "McpTools.h"
#include "McpBridge.h"
#include "McpSeat.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvCity.h"
#include "CvBuildingClasses.h"
#include "CvReplayMessage.h"
#include "CvInfos.h"
#include "CvGameCoreUtils.h"
#include "CvInternalGameCoreUtils.h"

using namespace McpTool;

namespace
{
	const char* PlayerKind(CvPlayer& p)
	{
		if(p.isMinorCiv()) return "city_state";
		return p.isHuman() ? "human" : "ai";
	}

	const char* EventKind(ReplayMessageTypes e)
	{
		switch(e)
		{
		case REPLAY_MESSAGE_MAJOR_EVENT: return "major_event";
		case REPLAY_MESSAGE_CITY_FOUNDED: return "city_founded";
		case REPLAY_MESSAGE_PLOT_OWNER_CHANGE: return "border_change";
		case REPLAY_MESSAGE_CITY_CAPTURED: return "city_captured";
		case REPLAY_MESSAGE_CITY_DESTROYED: return "city_destroyed";
		case REPLAY_MESSAGE_RELIGION_FOUNDED: return "religion_founded";
		case REPLAY_MESSAGE_PANTHEON_FOUNDED: return "pantheon_founded";
		default: return "other";
		}
	}

	bool Counted(PlayerTypes e) { return e >= 0 && e < MAX_CIV_PLAYERS && GET_PLAYER(e).isEverAlive(); }

	// eSelf == NO_PLAYER: every player (the on-screen session, and the file); otherwise that player alone.
	bool Shown(PlayerTypes e, PlayerTypes eSelf) { return McpHistoryShows(eSelf == NO_PLAYER, e == eSelf); }

	JVal PlayerJson(PlayerTypes e)
	{
		CvPlayer& p = GET_PLAYER(e);
		JObj o;
		o["id"] = J((int)e);
		o["civ"] = J(CivName(e));
		o["code"] = J(CivCode(e));
		o["leader"] = J(p.getName());
		o["kind"] = J(PlayerKind(p));
		o["team"] = J((int)p.getTeam());
		o["alive"] = J(p.isAlive());
		o["cities"] = J(p.getNumCities());
		o["era"] = J(EraName(p.GetCurrentEra()));
		o["score"] = J(p.GetScore());
		CvCity* pCapital = p.getCapitalCity();
		if(pCapital) o["capital"] = J(pCapital->getName().c_str());
		return JVal(o);
	}

	// One array per figure the game records, iLo to iHi; metrics, when not empty, keeps only the named ones.
	JVal SeriesJson(CvPlayer& p, int iLo, int iHi, const std::set<std::string>& metrics)
	{
		JObj o;
		for(unsigned int i = 0; i < p.getNumReplayDataSets(); ++i)
		{
			std::string key = McpReplaySetKey(p.getReplayDataSetName(i));
			if(!metrics.empty() && metrics.find(key) == metrics.end()) continue;
			o[key] = McpHistorySeries(p.getReplayDataHistory(i), iLo, iHi);
		}
		return JVal(o);
	}

	JVal EventsJson(PlayerTypes eSelf, int iLo, int iHi)
	{
		JArr a;
		CvGame& g = GC.getGame();
		for(uint i = 0; i < g.getNumReplayMessages(); ++i)
		{
			const CvReplayMessage* m = g.getReplayMessage(i);
			if(!m || m->getTurn() < iLo || m->getTurn() > iHi) continue;
			PlayerTypes e = m->getPlayer();
			if(eSelf != NO_PLAYER && e != eSelf) continue;
			JObj o;
			o["turn"] = J(m->getTurn());
			o["kind"] = J(EventKind(m->getType()));
			if(Counted(e)) o["civ"] = J(CivName(e));
			o["text"] = J(StripMarkup(m->getText().c_str()));
			int x, y;
			if(m->getNumPlots() > 0 && m->getPlot(0, x, y))
			{
				JArr at;
				at.push_back(J(x));
				at.push_back(J(y));
				o["at"] = JVal(at);
			}
			a.push_back(JVal(o));
		}
		return JVal(a);
	}

	JVal WondersJson(PlayerTypes eSelf)
	{
		JArr a;
		for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
		{
			PlayerTypes e = (PlayerTypes)i;
			if(!Counted(e) || !Shown(e, eSelf)) continue;
			CvPlayer& p = GET_PLAYER(e);
			int iLoop;
			for(CvCity* c = p.firstCity(&iLoop); c != NULL; c = p.nextCity(&iLoop))
			{
				for(int k = 0; k < GC.getNumBuildingInfos(); ++k)
				{
					BuildingTypes eB = (BuildingTypes)k;
					CvBuildingEntry* b = GC.getBuildingInfo(eB);
					if(!b || !::isWorldWonderClass(b->GetBuildingClassInfo()) || c->GetCityBuildings()->GetNumBuilding(eB) <= 0) continue;
					JObj o;
					o["wonder"] = J(BuildingName(eB));
					o["city"] = J(c->getName().c_str());
					o["civ"] = J(CivName(e));
					JArr at;
					at.push_back(J(c->getX()));
					at.push_back(J(c->getY()));
					o["at"] = JVal(at);
					a.push_back(JVal(o));
				}
			}
		}
		return JVal(a);
	}

	JVal RecordJson(PlayerTypes eSelf, int iLo, int iHi, const std::set<std::string>& metrics,
	                const std::set<std::string>& civs, bool bEvents)
	{
		JObj o;
		o["game"] = J(McpGameId());
		o["turn"] = J(GC.getGame().getGameTurn());
		o["from_turn"] = J(iLo);
		o["through_turn"] = J(iHi);
		o["settings"] = McpGameSettingsJson(eSelf == NO_PLAYER ? -1 : (int)eSelf);
		JArr players;
		JObj series;
		for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
		{
			PlayerTypes e = (PlayerTypes)i;
			if(!Counted(e) || !Shown(e, eSelf)) continue;
			if(!civs.empty() && civs.find(CivName(e)) == civs.end()) continue;
			players.push_back(PlayerJson(e));
			series[CivName(e)] = SeriesJson(GET_PLAYER(e), iLo, iHi, metrics);
		}
		o["players"] = JVal(players);
		o["series"] = JVal(series);
		if(bEvents) o["events"] = EventsJson(eSelf, iLo, iHi);
		o["wonders"] = WondersJson(eSelf);
		return JVal(o);
	}

	std::set<std::string> NameSet(const JObj& args, const char* key, std::string& err)
	{
		std::set<std::string> s;
		JObj::const_iterator it = args.find(key);
		if(!HasArg(args, key)) return s;
		if(!it->second.is<JArr>()) { err = std::string(key) + " must be an array of names"; return s; }
		const JArr& a = it->second.get<JArr>();
		for(size_t i = 0; i < a.size(); ++i)
			if(a[i].is<std::string>()) s.insert(a[i].get<std::string>());
		return s;
	}

	int s_writtenTurn = -1;
	std::string s_writtenGame;

	std::string WriteRecord()
	{
		int iNow = GC.getGame().getGameTurn();
		std::set<std::string> none;
		JVal record = RecordJson(NO_PLAYER, 0, iNow, none, none, true);
		return McpLogsWrite(McpHistoryFileName(McpGameId()), record.serialize());
	}
}

// Once per game turn, and again when another game is loaded; only a game the plugin has named (its id is the save's
// game name), so reading the id here never names one.
void McpHistoryTick()
{
	int iTurn = GC.getGame().getGameTurn();
	std::string name = CvPreGame::gameName().c_str();
	if(iTurn == s_writtenTurn && name == s_writtenGame) return;
	s_writtenTurn = iTurn;
	s_writtenGame = name;
	if(name.compare(0, 10, "civ5-game-") != 0) return;
	WriteRecord();
}

MCP_TOOL(ToolHistory)
{
	CvPlayer* me = Me(err);
	if(!me) return JVal();
	bool bOnScreen = McpBridge::Get().Current().seat == MCP_SEAT_SCREEN;
	PlayerTypes eSelf = bOnScreen ? NO_PLAYER : me->GetID();
	if(ArgBool(args, "file", false))
	{
		if(!bOnScreen) { err = "file writes every player's record, so only the on-screen session can ask for it"; return JVal(); }
		std::string path = WriteRecord();
		if(path.empty()) { err = "could not write the record to the Logs folder"; return JVal(); }
		JObj o;
		o["written"] = J(path);
		o["turn"] = J(GC.getGame().getGameTurn());
		return JVal(o);
	}
	int iLo, iHi;
	if(!McpHistoryWindow(ArgInt(args, "from_turn", -1), ArgInt(args, "through_turn", -1), GC.getGame().getGameTurn(), iLo, iHi, err)) return JVal();
	std::set<std::string> metrics = NameSet(args, "metrics", err);
	if(!err.empty()) return JVal();
	std::set<std::string> civs = NameSet(args, "civs", err);
	if(!err.empty()) return JVal();
	return RecordJson(eSelf, iLo, iHi, metrics, civs, ArgBool(args, "events", true));
}
