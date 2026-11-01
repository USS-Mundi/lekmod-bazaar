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
#include "CvTeam.h"
#include "CvGameCoreUtils.h"
#include "CvGlobals.h"
#include "CvPreGame.h"
#include "CvCity.h"
#include "CvTypes.h"
#include <map>

using namespace McpTool;

namespace
{
	const size_t MCP_CS_HISTORY = 10; // turns of the patron's influence kept for the rising and falling notes
	const size_t MCP_CS_TALK_KEPT = 40; // lines of each city-state's conversation with each civ
	const size_t MCP_CS_TALK_SHOWN = 20; // ...of which its player sees the latest in status
	const size_t MCP_CS_DEEDS_KEPT = 30; // deeds of each civ toward each city-state
	const size_t MCP_CS_DEEDS_SHOWN = 10;

	// on, patron and releaseAfterTurn are read from any thread: the engine asks who is human from its own threads, and
	// a seated city-state's answer comes from here. The rest is the game thread's alone.
	struct CsState
	{
		volatile bool on;
		volatile PlayerTypes patron;
		volatile bool releaseAfterTurn; // switched off during its own turn: its seat is handed back when that turn ends
		bool turnEnded;                 // its model was gone, so the plugin is ending this turn for it
		std::vector<int> history;
		CsState() : on(false), patron(NO_PLAYER), releaseAfterTurn(false), turnEnded(false) {}
	};

	// A fixed array, so a reader on another thread never meets an allocation or a rebalance.
	CsState s_states[MAX_CIV_PLAYERS];
	volatile long s_switchedOn = 0;        // how many are on: the hot hooks do nothing more while it is 0
	volatile bool s_teamOn[MAX_CIV_TEAMS]; // the teams of the switched-on city-states
	std::map<std::pair<int, int>, std::vector<std::string> > s_talk; // (city-state, civ) -> lines, oldest first
	std::map<std::pair<int, int>, std::vector<std::string> > s_deeds; // (city-state, civ) -> deeds, oldest first

	JArr Latest(const std::vector<std::string>& lines, size_t n)
	{
		JArr out;
		for(size_t i = lines.size() > n ? lines.size() - n : 0; i < lines.size(); ++i) out.push_back(J(lines[i]));
		return out;
	}
	std::vector<McpCsTier> s_tiers = McpCsDefaultTiers();
	std::vector<McpCsDisposition> s_dispositions = McpCsDefaultDispositions();
	McpCsSettings s_settings = McpCsDefaultSettings();

	bool IsLiveMinor(PlayerTypes e)
	{
		return e >= MAX_MAJOR_CIVS && e < MAX_CIV_PLAYERS && GET_PLAYER(e).isAlive() && GET_PLAYER(e).isMinorCiv();
	}

	CsState* Find(PlayerTypes e)
	{
		return (e >= MAX_MAJOR_CIVS && e < MAX_CIV_PLAYERS) ? &s_states[e] : NULL;
	}

	void SetTeamOn(PlayerTypes eMinor, bool bOn)
	{
		TeamTypes eTeam = GET_PLAYER(eMinor).getTeam();
		if(eTeam >= 0 && eTeam < MAX_CIV_TEAMS) s_teamOn[eTeam] = bOn;
	}

	// Out of its own land, or ordered out: a unit whose move ends outside counts as out already.
	bool IsOut(CvUnit* u, PlayerTypes eOwner)
	{
		if(!u->plot()) return false;
		if(u->plot()->getOwner() != eOwner) return true;
		const MissionData* m = u->GetHeadMissionData();
		if(!m || m->eMissionType != CvTypes::getMISSION_MOVE_TO()) return false;
		CvPlot* to = GC.getMap().plot(m->iData1, m->iData2);
		return to && to->getOwner() != eOwner;
	}

	int UnitsOut(CvPlayer& p, int iExceptUnit)
	{
		int n = 0, iLoop;
		for(CvUnit* u = p.firstUnit(&iLoop); u != NULL; u = p.nextUnit(&iLoop))
			if(u->IsCombatUnit() && u->GetID() != iExceptUnit && IsOut(u, p.GetID())) ++n;
		return n;
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

	const char* PersonalityName(MinorCivPersonalityTypes e)
	{
		switch(e)
		{
		case MINOR_CIV_PERSONALITY_FRIENDLY: return "friendly";
		case MINOR_CIV_PERSONALITY_NEUTRAL: return "neutral";
		case MINOR_CIV_PERSONALITY_HOSTILE: return "hostile";
		case MINOR_CIV_PERSONALITY_IRRATIONAL: return "irrational";
		default: return "unknown";
		}
	}

	const char* TraitName(MinorCivTraitTypes e)
	{
		switch(e)
		{
		case MINOR_CIV_TRAIT_CULTURED: return "cultured";
		case MINOR_CIV_TRAIT_MILITARISTIC: return "militaristic";
		case MINOR_CIV_TRAIT_MARITIME: return "maritime";
		case MINOR_CIV_TRAIT_MERCANTILE: return "mercantile";
		case MINOR_CIV_TRAIT_RELIGIOUS: return "religious";
		default: return "unknown";
		}
	}

	// The strongest influence any other major civ has with it.
	int RunnerUp(PlayerTypes eMinor, PlayerTypes ePatron)
	{
		int best = 0;
		CvMinorCivAI* ai = GET_PLAYER(eMinor).GetMinorCivAI();
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		{
			PlayerTypes e = (PlayerTypes)i;
			if(e == ePatron || !GET_PLAYER(e).isAlive()) continue;
			int v = ai->GetEffectiveFriendshipWithMajor(e);
			if(v > best) best = v;
		}
		return best;
	}

	// The city-state is the only player whose turn is left to a model that may be gone: no other seat ends a turn
	// on its own, so the game would wait. Gone is the bridge's idle rule (no request for two minutes). Ending the
	// turn of a player that is not on screen does nothing while a unit or city is busy, so it is tried again every
	// update until it holds; a city waiting for a build would block it, so the game's AI picks one.
	void EndTurnIfGone(PlayerTypes e, CsState& s)
	{
		CvPlayer& p = GET_PLAYER(e);
		if(!p.isTurnActive()) { s.turnEnded = false; return; }
		if(CvPreGame::slotStatus(e) != SS_TAKEN || p.isEndTurn()) return;
		McpSession session;
		if(McpBridge::Get().SessionForSeat((int)e, session)) return;
		int iLoop;
		for(CvCity* c = p.firstCity(&iLoop); c != NULL; c = p.nextCity(&iLoop))
			if(c->getOrderQueueLength() == 0) c->AI_chooseProduction(false);
		p.EndTurnsForReadyUnits();
		p.setEndTurn(true);
		if(s.turnEnded) return;
		s.turnEnded = true;
		if(s.on) McpCsSwitchOff(e, "its player was gone when its turn came");
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
	if(!s.on) ++s_switchedOn;
	s.patron = ePatron;
	s.releaseAfterTurn = false;
	s.history.clear();
	s.history.push_back(GET_PLAYER(eMinor).GetMinorCivAI()->GetEffectiveFriendshipWithMajor(ePatron));
	s.on = true;
	SetTeamOn(eMinor, true);
	TellPatron(ePatron, CivName(eMinor) + " answers your call for its active help. A model may now play it; until one does, the game's AI keeps playing it, and the extra influence cost starts only when a model takes its seat.");
	return true;
}

void McpCsSwitchOff(PlayerTypes eMinor, const std::string& why)
{
	CsState* s = Find(eMinor);
	if(!s || !s->on) return;
	PlayerTypes ePatron = s->patron;
	s->on = false;
	--s_switchedOn;
	SetTeamOn(eMinor, false);
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
	return UnitsOut(GET_PLAYER(eMinor), -1);
}

bool McpCsAnySwitchedOn() { return s_switchedOn > 0; }

bool McpCsTeamSwitchedOn(TeamTypes eTeam)
{
	return eTeam >= 0 && eTeam < MAX_CIV_TEAMS && s_teamOn[eTeam];
}

std::string McpCsMoveRefusal(const CvUnit* u, const CvPlot* dest)
{
	if(!u || !dest || !u->IsCombatUnit()) return "";
	PlayerTypes e = u->getOwner();
	if(!McpIsCityStateSeat(e) || !u->plot() || u->plot()->getOwner() != e || dest->getOwner() == e) return "";
	// Switched off but not yet handed back (the rest of its own turn): nothing more leaves its land.
	if(!McpCsIsSwitchedOn(e)) return "you have been switched off: your units stay in your land until the game's AI takes you back";
	int cap = McpCsSendCapFor(e), out = UnitsOut(GET_PLAYER(e), u->GetID());
	if(out < cap) return "";
	return McpFormat("at %d influence your ally can have %d of your units outside your land, and %d are out already; bring one home or wait for more influence", McpCsInfluence(e), cap, out);
}

bool McpCsMayEnter(PlayerTypes eMinor, TeamTypes eTeam)
{
	if(eTeam == NO_TEAM) return false;
	PlayerTypes ePatron = McpCsPatron(eMinor);
	if(ePatron == NO_PLAYER || GET_PLAYER(ePatron).getTeam() != eTeam) return false;
	const McpCsTier* tier = McpCsCurrentTier(eMinor);
	return tier && tier->enterAllyLand;
}

bool McpCsTeamMayEnter(TeamTypes eMinorTeam, TeamTypes eLandTeam)
{
	return McpCsMayEnter(GET_TEAM(eMinorTeam).getLeaderID(), eLandTeam);
}

void McpCsRevealAround(const CvUnit* u)
{
	PlayerTypes e = u->getOwner();
	const McpCsTier* tier = McpCsCurrentTier(e);
	if(!tier || !tier->revealSight || !u->plot()) return;
	TeamTypes eAllyTeam = GET_PLAYER(McpCsPatron(e)).getTeam();
	int range = u->visibilityRange();
	for(int dx = -range; dx <= range; ++dx)
	{
		for(int dy = -range; dy <= range; ++dy)
		{
			CvPlot* p = plotXYWithRangeCheck(u->getX(), u->getY(), dx, dy, range);
			if(p && !p->isRevealed(eAllyTeam)) p->setRevealed(eAllyTeam, true);
		}
	}
}

void McpCsTick()
{
	for(int i = MAX_MAJOR_CIVS; i < MAX_CIV_PLAYERS; ++i)
	{
		PlayerTypes e = (PlayerTypes)i;
		CsState& s = s_states[i];
		if(!s.on && !s.releaseAfterTurn) continue;
		if(!IsLiveMinor(e))
		{
			if(s.on) McpCsSwitchOff(e, "it no longer exists");
			s.releaseAfterTurn = false;
			HandBack(e);
			continue;
		}
		if(s.on && GET_PLAYER(e).GetMinorCivAI()->GetAlly() != s.patron) McpCsAfterSetAlly(e, GET_PLAYER(e).GetMinorCivAI()->GetAlly());
		if(s.on || s.releaseAfterTurn) EndTurnIfGone(e, s);
		if(s.releaseAfterTurn && !GET_PLAYER(e).isTurnActive())
		{
			s.releaseAfterTurn = false;
			HandBack(e);
		}
	}
}

void McpCsNoteLine(PlayerTypes eMinor, PlayerTypes eCiv, const std::string& line)
{
	std::vector<std::string>& lines = s_talk[std::make_pair((int)eMinor, (int)eCiv)];
	lines.push_back(line);
	if(lines.size() > MCP_CS_TALK_KEPT) lines.erase(lines.begin());
}

std::vector<std::string> McpCsConversation(PlayerTypes eMinor, PlayerTypes eCiv)
{
	std::map<std::pair<int, int>, std::vector<std::string> >::const_iterator it = s_talk.find(std::make_pair((int)eMinor, (int)eCiv));
	return it == s_talk.end() ? std::vector<std::string>() : it->second;
}

void McpCsNoteDeed(PlayerTypes eMinor, PlayerTypes eCiv, const std::string& what)
{
	if(eMinor < MAX_MAJOR_CIVS || eCiv == NO_PLAYER || eCiv >= MAX_MAJOR_CIVS) return;
	std::vector<std::string>& lines = s_deeds[std::make_pair((int)eMinor, (int)eCiv)];
	lines.push_back(McpFormat("turn %d: ", GC.getGame().getGameTurn()) + what);
	if(lines.size() > MCP_CS_DEEDS_KEPT) lines.erase(lines.begin());
}

std::vector<std::string> McpCsDeeds(PlayerTypes eMinor, PlayerTypes eCiv)
{
	std::map<std::pair<int, int>, std::vector<std::string> >::const_iterator it = s_deeds.find(std::make_pair((int)eMinor, (int)eCiv));
	return it == s_deeds.end() ? std::vector<std::string>() : it->second;
}

bool McpIsCityStateSeat(PlayerTypes e)
{
	if(e < MAX_MAJOR_CIVS || e >= MAX_CIV_PLAYERS) return false;
	const CsState& s = s_states[e];
	return (s.on || s.releaseAfterTurn) && CvPreGame::slotStatus(e) == SS_TAKEN && GET_PLAYER(e).isMinorCiv();
}

std::vector<PlayerTypes> McpCsWaitingForModel()
{
	std::vector<PlayerTypes> out;
	McpSession session;
	for(int i = MAX_MAJOR_CIVS; i < MAX_CIV_PLAYERS; ++i)
	{
		PlayerTypes e = (PlayerTypes)i;
		if(s_states[i].on && IsLiveMinor(e) && CvPreGame::slotStatus(e) != SS_TAKEN && !McpBridge::Get().SessionForSeat(i, session)) out.push_back(e);
	}
	return out;
}

std::string McpCsToolRefusal(const std::string& tool)
{
	static const char* const REFUSED[] =
	{
		"declare_war", "make_peace", "set_research", "research_options", "adopt_policy", "policy_options",
		"choose_ideology", "switch_ideology", "found_pantheon", "found_religion", "enhance_religion",
		"add_reformation_belief", "deal_options", "propose_deal", "answer_deal", "demand", "discuss",
		"respond_to_request", "congress", "propose_resolution", "vote_resolution", "abstain", "spies", "move_spy",
		"stage_coup", "steal_tech", "launch_spaceship", "establish_trade_route", "city_state_action",
		"tell_city_state", "choose_great_person"
	};
	for(size_t i = 0; i < sizeof(REFUSED) / sizeof(REFUSED[0]); ++i)
		if(tool == REFUSED[i]) return "a city-state does not do that: " + tool + " is decided by the game's rules for city-states";
	return "";
}

picojson::value McpCsSeatView(PlayerTypes eMinor)
{
	JObj o;
	if(!IsLiveMinor(eMinor)) return JVal(o);
	CvMinorCivAI* ai = GET_PLAYER(eMinor).GetMinorCivAI();
	PlayerTypes ePatron = McpCsPatron(eMinor);
	o["personality"] = J(PersonalityName(ai->GetPersonality()));
	o["kind"] = J(TraitName(ai->GetTrait()));
	if(ePatron == NO_PLAYER)
	{
		o["ally"] = J("none: you have been switched off, and the game's AI takes you back when this turn ends");
		return JVal(o);
	}
	int influence = McpCsInfluence(eMinor);
	o["ally"] = J(CivName(ePatron));
	o["ally_influence"] = J(influence);
	const McpCsDisposition* d = McpCsDispositionFor(s_dispositions, influence);
	if(d)
	{
		o["ally_standing"] = J(d->name);
		o["how_you_see_them"] = J(d->text);
	}
	CsState* s = Find(eMinor);
	JArr notes;
	std::vector<std::string> mods = McpCsModifiers(s_settings, influence, RunnerUp(eMinor, ePatron), s ? s->history : std::vector<int>());
	for(size_t i = 0; i < mods.size(); ++i) notes.push_back(J(mods[i]));
	if(!notes.empty()) o["notes"] = JVal(notes);
	const McpCsTier* tier = McpCsCurrentTier(eMinor);
	o["units_you_may_send_out_of_your_land"] = J(McpCsSendCapFor(eMinor));
	o["units_out_now"] = J(McpCsUnitsOut(eMinor));
	o["your_sight_on_their_map"] = J(tier && tier->revealSight);
	o["you_may_enter_their_land"] = J(tier && tier->enterAllyLand);
	std::vector<std::string> talk = McpCsConversation(eMinor, ePatron);
	if(!talk.empty()) o["conversation_with_your_ally"] = JVal(Latest(talk, MCP_CS_TALK_SHOWN));
	JObj deeds;
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
	{
		std::vector<std::string> d = McpCsDeeds(eMinor, (PlayerTypes)i);
		if(!d.empty()) deeds[CivName((PlayerTypes)i)] = JVal(Latest(d, MCP_CS_DEEDS_SHOWN));
	}
	if(!deeds.empty()) o["what_each_civ_did_for_you"] = JVal(deeds);
	o["what_you_play"] = J("every unit and your city's builds; the game's rules decide your wars, peace, research and policies, and your alliance follows influence as always");
	return JVal(o);
}

void McpCsResetForNewGame()
{
	for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
	{
		s_states[i].on = false;
		s_states[i].patron = NO_PLAYER;
		s_states[i].releaseAfterTurn = false;
		s_states[i].turnEnded = false;
		s_states[i].history.clear();
	}
	for(int t = 0; t < MAX_CIV_TEAMS; ++t) s_teamOn[t] = false;
	s_switchedOn = 0;
	s_talk.clear();
	s_deeds.clear();
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
	// The ally pays for help it is getting: only while a model holds the seat.
	if(s_settings.influenceCost != 0 && CvPreGame::slotStatus(eMinor) == SS_TAKEN) ai->ChangeFriendshipWithMajor(s->patron, -s_settings.influenceCost);
	s->history.push_back(ai->GetEffectiveFriendshipWithMajor(s->patron));
	if(s->history.size() > MCP_CS_HISTORY) s->history.erase(s->history.begin());
}
