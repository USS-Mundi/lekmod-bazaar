#include "CvGameCoreDLLPCH.h"
#include "McpCityStates.h"
#include "McpBridge.h"
#include "McpLogText.h"
#include "McpSeat.h"
#include "McpToolSupport.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvUnit.h"
#include "CvMinorCivAI.h"
#include "CvGlobals.h"
#include "CvPreGame.h"
#include <map>

using namespace McpTool;

namespace
{
	const size_t MCP_CS_HISTORY = 10; // turns of the patron's influence kept for the rising and falling notes

	struct CsState
	{
		bool on;
		PlayerTypes patron;
		bool releaseAfterTurn; // switched off during its own turn: its seat is handed back when that turn ends
		std::vector<int> history;
		CsState() : on(false), patron(NO_PLAYER), releaseAfterTurn(false) {}
	};

	std::map<int, CsState> s_states;
	std::vector<McpCsTier> s_tiers = McpCsDefaultTiers();
	std::vector<McpCsDisposition> s_dispositions = McpCsDefaultDispositions();
	McpCsSettings s_settings = McpCsDefaultSettings();

	bool IsLiveMinor(PlayerTypes e)
	{
		return e >= MAX_MAJOR_CIVS && e < MAX_CIV_PLAYERS && GET_PLAYER(e).isAlive() && GET_PLAYER(e).isMinorCiv();
	}

	CsState* Find(PlayerTypes e)
	{
		std::map<int, CsState>::iterator it = s_states.find((int)e);
		return it == s_states.end() ? NULL : &it->second;
	}

	// Its slot goes back to the computer, as release_seat does for a major; the session on it loses the seat.
	void HandBack(PlayerTypes e)
	{
		if(CvPreGame::slotStatus(e) != SS_TAKEN) return;
		CvPreGame::setSlotStatus(e, SS_COMPUTER);
		CvPreGame::setNickname(e, CvString(""));
		CvPreGame::setLeaderName(e, CvString(""));
		CvPreGame::setHandicap(e, (HandicapTypes)GC.getAI_HANDICAP());
	}

	void TellPatron(PlayerTypes ePatron, const std::string& text)
	{
		if(ePatron != NO_PLAYER) McpBridge::Get().LogLineFor((int)ePatron, MCP_LOG_DIPLOMACY, text);
	}

	int MilitaryUnits(CvPlayer& p, bool bOutsideOnly)
	{
		int n = 0, iLoop;
		for(CvUnit* u = p.firstUnit(&iLoop); u != NULL; u = p.nextUnit(&iLoop))
		{
			if(!u->IsCombatUnit()) continue;
			if(bOutsideOnly && u->plot() && u->plot()->getOwner() == p.GetID()) continue;
			++n;
		}
		return n;
	}

	bool ReadTiers(std::vector<McpCsTier>& out)
	{
		Database::Results r;
		if(!DB.Execute(r, "SELECT MinInfluence, SendPercent, SendMin, RevealSight, EnterAllyLand FROM McpCityStateTiers ORDER BY MinInfluence")) return false;
		while(r.Step())
		{
			McpCsTier t = { r.GetInt(0), r.GetInt(1), r.GetInt(2), r.GetInt(3) != 0, r.GetInt(4) != 0 };
			out.push_back(t);
		}
		return !out.empty();
	}

	bool ReadDispositions(std::vector<McpCsDisposition>& out)
	{
		Database::Results r;
		if(!DB.Execute(r, "SELECT MinInfluence, Name, Text FROM McpCityStateDispositions ORDER BY MinInfluence")) return false;
		while(r.Step())
		{
			McpCsDisposition d;
			d.minInfluence = r.GetInt(0);
			d.name = r.GetText(1) ? r.GetText(1) : "";
			d.text = r.GetText(2) ? r.GetText(2) : "";
			out.push_back(d);
		}
		return !out.empty();
	}

	bool ReadSettings(McpCsSettings& s)
	{
		Database::Results r;
		if(!DB.Execute(r, "SELECT Name, Value FROM McpCityStateSettings")) return false;
		int n = 0;
		while(r.Step())
		{
			std::string name = r.GetText(0) ? r.GetText(0) : "";
			int v = r.GetInt(1);
			if(name == "MCP_CITY_STATE_SWITCHED_ON_INFLUENCE_COST") s.influenceCost = v;
			else if(name == "MCP_CITY_STATE_CONTESTED_LEAD") s.contestedLead = v;
			else if(name == "MCP_CITY_STATE_SECURE_LEAD") s.secureLead = v;
			else if(name == "MCP_CITY_STATE_TREND_TURNS") s.trendTurns = v;
			else if(name == "MCP_CITY_STATE_TREND_AMOUNT") s.trendAmount = v;
			else continue;
			++n;
		}
		return n > 0;
	}
}

void McpCsLoadSettings()
{
	std::vector<std::string> missing;
	std::vector<McpCsTier> tiers;
	if(ReadTiers(tiers)) s_tiers = tiers; else { s_tiers = McpCsDefaultTiers(); missing.push_back("McpCityStateTiers"); }
	std::vector<McpCsDisposition> dispositions;
	if(ReadDispositions(dispositions)) s_dispositions = dispositions; else { s_dispositions = McpCsDefaultDispositions(); missing.push_back("McpCityStateDispositions"); }
	McpCsSettings settings = McpCsDefaultSettings();
	if(!ReadSettings(settings)) missing.push_back("McpCityStateSettings");
	s_settings = settings;
	if(missing.empty()) return;
	std::string list;
	for(size_t i = 0; i < missing.size(); ++i) list += (i ? ", " : "") + missing[i];
	McpBridge::Get().LogLineFor(-1, MCP_LOG_ALWAYS, "City-state settings missing from the mod's database (" + list + "); the built-in defaults apply");
}

const McpCsSettings& McpCsGetSettings() { return s_settings; }

bool McpCsSwitchOn(PlayerTypes eMinor, PlayerTypes ePatron, std::string& err)
{
	if(!IsLiveMinor(eMinor)) { err = "that is not a living city-state"; return false; }
	if(ePatron == NO_PLAYER || GET_PLAYER(eMinor).GetMinorCivAI()->GetAlly() != ePatron) { err = "only their ally can ask for their active help"; return false; }
	CsState& s = s_states[(int)eMinor];
	if(s.on && s.patron == ePatron) return true;
	s.on = true;
	s.patron = ePatron;
	s.releaseAfterTurn = false;
	s.history.clear();
	s.history.push_back(GET_PLAYER(eMinor).GetMinorCivAI()->GetEffectiveFriendshipWithMajor(ePatron));
	TellPatron(ePatron, CivName(eMinor) + " answers your call for its active help. A model may now play it; until one does, the game's AI keeps playing it.");
	return true;
}

void McpCsSwitchOff(PlayerTypes eMinor, const std::string& why)
{
	CsState* s = Find(eMinor);
	if(!s || !s->on) return;
	PlayerTypes ePatron = s->patron;
	s->on = false;
	s->patron = NO_PLAYER;
	s->history.clear();
	if(CvPreGame::slotStatus(eMinor) == SS_TAKEN)
	{
		if(GET_PLAYER(eMinor).isAlive() && GET_PLAYER(eMinor).isTurnActive()) s->releaseAfterTurn = true;
		else HandBack(eMinor);
	}
	TellPatron(ePatron, CivName(eMinor) + " goes back to the game's AI: " + why + ".");
}

bool McpCsIsSwitchedOn(PlayerTypes eMinor)
{
	CsState* s = Find(eMinor);
	return s && s->on;
}

PlayerTypes McpCsPatron(PlayerTypes eMinor)
{
	CsState* s = Find(eMinor);
	return (s && s->on) ? s->patron : NO_PLAYER;
}

std::string McpCsPlayedBy(PlayerTypes eMinor)
{
	return CvPreGame::slotStatus(eMinor) == SS_TAKEN ? McpSeatLabel(eMinor) : "";
}

int McpCsInfluence(PlayerTypes eMinor)
{
	PlayerTypes ePatron = McpCsPatron(eMinor);
	if(ePatron == NO_PLAYER || !IsLiveMinor(eMinor)) return 0;
	return GET_PLAYER(eMinor).GetMinorCivAI()->GetEffectiveFriendshipWithMajor(ePatron);
}

const McpCsTier* McpCsCurrentTier(PlayerTypes eMinor)
{
	if(!McpCsIsSwitchedOn(eMinor)) return NULL;
	return McpCsTierFor(s_tiers, McpCsInfluence(eMinor));
}

int McpCsSendCapFor(PlayerTypes eMinor)
{
	if(!IsLiveMinor(eMinor)) return 0;
	return McpCsSendCap(McpCsCurrentTier(eMinor), MilitaryUnits(GET_PLAYER(eMinor), false));
}

int McpCsUnitsOut(PlayerTypes eMinor)
{
	if(!IsLiveMinor(eMinor)) return 0;
	return MilitaryUnits(GET_PLAYER(eMinor), true);
}

void McpCsTick()
{
	for(std::map<int, CsState>::iterator it = s_states.begin(); it != s_states.end(); ++it)
	{
		PlayerTypes e = (PlayerTypes)it->first;
		CsState& s = it->second;
		if(!IsLiveMinor(e))
		{
			if(s.on) McpCsSwitchOff(e, "it no longer exists");
			s.releaseAfterTurn = false;
			HandBack(e);
			continue;
		}
		if(s.on && GET_PLAYER(e).GetMinorCivAI()->GetAlly() != s.patron) McpCsAfterSetAlly(e, GET_PLAYER(e).GetMinorCivAI()->GetAlly());
		if(s.releaseAfterTurn && !GET_PLAYER(e).isTurnActive())
		{
			s.releaseAfterTurn = false;
			HandBack(e);
		}
	}
}

void McpCsResetForNewGame()
{
	s_states.clear();
	McpCsLoadSettings();
	// Switching on is not kept in a save: a city-state seated when the game was saved goes back to the game's AI,
	// or the game would wait on a turn no model is there to play.
	for(int i = MAX_MAJOR_CIVS; i < MAX_CIV_PLAYERS; ++i)
	{
		PlayerTypes e = (PlayerTypes)i;
		if(CvPreGame::slotStatus(e) != SS_TAKEN || !GET_PLAYER(e).isMinorCiv()) continue;
		HandBack(e);
		McpBridge::Get().LogLineFor(-1, MCP_LOG_ALWAYS, CivName(e) + " goes back to the game's AI: switching a city-state on does not survive a reload yet.");
	}
}

void McpCsAfterSetAlly(PlayerTypes eMinor, PlayerTypes eNewAlly)
{
	CsState* s = Find(eMinor);
	if(!s || !s->on || eNewAlly == s->patron) return;
	McpCsSwitchOff(eMinor, eNewAlly == NO_PLAYER ? std::string("it no longer has an ally") : "its alliance passed to " + CivName(eNewAlly));
}

void McpCsAfterMinorTurn(PlayerTypes eMinor)
{
	CsState* s = Find(eMinor);
	if(!s || !s->on || !IsLiveMinor(eMinor)) return;
	CvMinorCivAI* ai = GET_PLAYER(eMinor).GetMinorCivAI();
	if(s_settings.influenceCost != 0) ai->ChangeFriendshipWithMajor(s->patron, -s_settings.influenceCost);
	s->history.push_back(ai->GetEffectiveFriendshipWithMajor(s->patron));
	if(s->history.size() > MCP_CS_HISTORY) s->history.erase(s->history.begin());
}
