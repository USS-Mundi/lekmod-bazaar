#include "McpCityStateRules.h"
#include <cctype>
#include <cstdlib>
#include <cstddef>
#include <set>
#include <sstream>

namespace
{
	McpCsTier Tier(int minInfluence, int sendPercent, int sendMin, bool revealSight, bool enterAllyLand)
	{
		McpCsTier t = { minInfluence, sendPercent, sendMin, revealSight, enterAllyLand };
		return t;
	}

	McpCsDisposition Disposition(int minInfluence, const char* name, const char* text)
	{
		McpCsDisposition d;
		d.minInfluence = minInfluence;
		d.name = name;
		d.text = text;
		return d;
	}
}

std::vector<McpCsTier> McpCsDefaultTiers()
{
	std::vector<McpCsTier> t;
	t.push_back(Tier(60, 0, 0, false, false));
	t.push_back(Tier(70, 10, 1, false, false));
	t.push_back(Tier(80, 20, 1, true, false));
	t.push_back(Tier(90, 30, 2, true, false));
	t.push_back(Tier(100, 40, 2, true, true));
	t.push_back(Tier(110, 50, 3, true, true));
	t.push_back(Tier(120, 60, 3, true, true));
	t.push_back(Tier(130, 70, 4, true, true));
	t.push_back(Tier(140, 80, 4, true, true));
	t.push_back(Tier(150, 90, 5, true, true));
	return t;
}

std::vector<McpCsDisposition> McpCsDefaultDispositions()
{
	std::vector<McpCsDisposition> d;
	d.push_back(Disposition(60, "Ally", "They lead your court, but only just. Talk freely, tell them what you see near your land, and keep your army home; what you send out is a courtesy, and costly orders deserve something back."));
	d.push_back(Disposition(90, "Trusted ally", "They have proved themselves. Take risks for them within what you can send out, and tell them who else is courting you."));
	d.push_back(Disposition(120, "Devoted ally", "Their cause is yours. Act for them without waiting to be asked, and turn rival envoys away."));
	d.push_back(Disposition(150, "Sworn", "You would fall for them. Their enemies are yours, your secrets are theirs, and no rival's gold moves you."));
	return d;
}

McpCsSettings McpCsDefaultSettings()
{
	McpCsSettings s = { 1, 20, 60, 5, 15, 0 };
	return s;
}

const McpCsTier* McpCsTierFor(const std::vector<McpCsTier>& tiers, int influence)
{
	const McpCsTier* best = NULL;
	for(size_t i = 0; i < tiers.size(); ++i)
		if(tiers[i].minInfluence <= influence && (!best || tiers[i].minInfluence > best->minInfluence)) best = &tiers[i];
	return best;
}

const McpCsDisposition* McpCsDispositionFor(const std::vector<McpCsDisposition>& dispositions, int influence)
{
	const McpCsDisposition* best = NULL;
	for(size_t i = 0; i < dispositions.size(); ++i)
		if(dispositions[i].minInfluence <= influence && (!best || dispositions[i].minInfluence > best->minInfluence)) best = &dispositions[i];
	return best;
}

int McpCsSendCap(const McpCsTier* tier, int militaryUnits)
{
	if(!tier || militaryUnits <= 0) return 0;
	int cap = tier->sendPercent * militaryUnits / 100;
	if(cap < tier->sendMin) cap = tier->sendMin;
	if(cap > militaryUnits - 1) cap = militaryUnits - 1;
	return cap < 0 ? 0 : cap;
}

McpCsNextSteps McpCsNextStepsFor(const std::vector<McpCsTier>& tiers, int influence, int militaryUnits)
{
	const McpCsTier* now = McpCsTierFor(tiers, influence);
	int cap = McpCsSendCap(now, militaryUnits);
	McpCsNextSteps next = { -1, -1, -1 };
	for(size_t i = 0; i < tiers.size(); ++i)
	{
		const McpCsTier& t = tiers[i];
		if(t.minInfluence <= influence) continue;
		if(McpCsSendCap(&t, militaryUnits) > cap && (next.moreUnitsOut < 0 || t.minInfluence < next.moreUnitsOut)) next.moreUnitsOut = t.minInfluence;
		if(t.revealSight && !(now && now->revealSight) && (next.sight < 0 || t.minInfluence < next.sight)) next.sight = t.minInfluence;
		if(t.enterAllyLand && !(now && now->enterAllyLand) && (next.enterAllyLand < 0 || t.minInfluence < next.enterAllyLand)) next.enterAllyLand = t.minInfluence;
	}
	return next;
}

bool McpCsIsDismissal(const std::string& text)
{
	size_t a = text.find_first_not_of(" \t\r\n"), b = text.find_last_not_of(" \t\r\n");
	if(a == std::string::npos) return false;
	std::string word;
	for(size_t i = a; i <= b; ++i) word += (char)std::tolower((unsigned char)text[i]);
	return word == "dismiss" || word == "/dismiss";
}

bool McpCsHumanMaySpeak(bool bAlive, bool bMinor, int ally, int human)
{
	return bAlive && bMinor && human >= 0 && ally == human;
}

int McpCsGoldMoved(int giverBefore, int giverAfter)
{
	return giverBefore > giverAfter ? giverBefore - giverAfter : 0;
}

int McpCsTributePaid(int treasury, int tribute)
{
	if(treasury <= 0 || tribute <= 0) return 0;
	return treasury < tribute ? treasury : tribute;
}

std::string McpCsGiftDeed(int gold, int treasuryBefore)
{
	std::ostringstream o;
	o << "gave them " << gold << " gold (treasury " << treasuryBefore << " to " << (treasuryBefore + gold) << ")";
	return o.str();
}

std::string McpCsTributeDeed(int tribute, int paid, int treasuryBefore)
{
	std::ostringstream o;
	o << "took " << tribute << " gold from them by threat (treasury " << treasuryBefore << " to " << (treasuryBefore - paid) << ")";
	return o.str();
}

bool McpCsSameSide(bool aMinor, bool bMinor, int allyTeamA, int allyTeamB)
{
	return aMinor && bMinor && allyTeamA >= 0 && allyTeamA == allyTeamB;
}

bool McpCsReadTalkLine(const std::string& line, const std::string& cityState, int& turn, bool& byCityState, std::string& text)
{
	if(line.compare(0, 5, "turn ") != 0) return false;
	size_t i = 5, digits = 0;
	int n = 0;
	while(i < line.size() && line[i] >= '0' && line[i] <= '9') { n = n * 10 + (line[i] - '0'); ++i; ++digits; }
	if(digits == 0 || line.compare(i, 2, ", ") != 0) return false;
	size_t colon = line.find(": ", i + 2);
	if(colon == std::string::npos) return false;
	turn = n;
	byCityState = line.substr(i + 2, colon - i - 2) == cityState;
	text = line.substr(colon + 2);
	return true;
}

namespace
{
	const char* SIGHT_TAG = "mcp-sight 1|";

	bool ReadInt(const std::string& s, size_t& i, int& out)
	{
		size_t start = i;
		if(i < s.size() && s[i] == '-') ++i;
		while(i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
		if(i == start || (i == start + 1 && s[start] == '-')) return false;
		out = std::atoi(s.substr(start, i - start).c_str());
		return true;
	}
}

std::string McpCsSightLedgerWrite(const McpCsSightLedger& ledger)
{
	if(ledger.team < 0 || ledger.unitPlots.empty()) return "";
	std::ostringstream out;
	out << SIGHT_TAG << ledger.team << "|";
	for(std::map<int, std::vector<int> >::const_iterator it = ledger.unitPlots.begin(); it != ledger.unitPlots.end(); ++it)
	{
		if(it != ledger.unitPlots.begin()) out << ";";
		out << it->first << ":";
		for(size_t k = 0; k < it->second.size(); ++k) out << (k ? "," : "") << it->second[k];
	}
	return out.str();
}

bool McpCsSightLedgerRead(const std::string& text, McpCsSightLedger& ledger)
{
	ledger = McpCsSightLedger();
	if(text.empty()) return true;
	std::string tag(SIGHT_TAG);
	if(text.compare(0, tag.size(), tag) != 0) return false;
	size_t i = tag.size();
	McpCsSightLedger read;
	if(!ReadInt(text, i, read.team) || i >= text.size() || text[i++] != '|') return false;
	while(i < text.size())
	{
		int unit = 0;
		if(!ReadInt(text, i, unit) || i >= text.size() || text[i++] != ':') return false;
		std::vector<int>& plots = read.unitPlots[unit];
		for(;;)
		{
			int plot = 0;
			if(!ReadInt(text, i, plot)) return false;
			plots.push_back(plot);
			if(i >= text.size() || text[i] == ';') break;
			if(text[i++] != ',') return false;
		}
		if(i < text.size()) ++i;
	}
	ledger = read;
	return true;
}

int McpCsLiveSightFrom(const std::vector<McpCsTier>& tiers, const McpCsSettings& settings)
{
	int top = -1, firstSight = -1;
	for(size_t i = 0; i < tiers.size(); ++i)
	{
		if(tiers[i].minInfluence > top) top = tiers[i].minInfluence;
		if(tiers[i].revealSight && (firstSight < 0 || tiers[i].minInfluence < firstSight)) firstSight = tiers[i].minInfluence;
	}
	if(firstSight < 0) return -1;
	int from = settings.liveSightInfluence > 0 ? settings.liveSightInfluence : top;
	return from < firstSight ? firstSight : from;
}

bool McpCsSightIsAutomatic(const std::vector<McpCsTier>& tiers, const McpCsSettings& settings, int influence)
{
	int from = McpCsLiveSightFrom(tiers, settings);
	if(from < 0 || influence < from) return false;
	const McpCsTier* tier = McpCsTierFor(tiers, influence);
	return tier && tier->revealSight;
}

McpCsSightChange McpCsAutoSightChange(bool automatic, const std::vector<int>& units, const std::vector<int>& lit, const std::vector<int>& autoLit)
{
	std::set<int> has(units.begin(), units.end()), isLit(lit.begin(), lit.end()), rule;
	McpCsSightChange c;
	// the rule's units that are still there and still lit
	for(size_t i = 0; i < autoLit.size(); ++i)
		if(has.count(autoLit[i]) && isLit.count(autoLit[i])) rule.insert(autoLit[i]);
	if(!automatic)
	{
		c.drop.assign(rule.begin(), rule.end());
		return c;
	}
	for(std::set<int>::const_iterator it = has.begin(); it != has.end(); ++it)
		if(!isLit.count(*it)) { c.light.push_back(*it); rule.insert(*it); }
	c.autoLit.assign(rule.begin(), rule.end());
	return c;
}

std::string McpCsSightOffRefusal(bool automatic)
{
	return automatic ? "at this standing every unit of yours sees for your ally: it is not yours to withhold" : "";
}

const char* McpCsModifierText(McpCsModifier modifier)
{
	switch(modifier)
	{
	case MCP_CS_CONTESTED: return "Your loyalty is in play: the runner-up could take your alliance soon. Hedge.";
	case MCP_CS_SECURE: return "No one is close to replacing them.";
	case MCP_CS_RISING: return "They have been generous lately.";
	case MCP_CS_FALLING: return "They have been neglecting you.";
	}
	return "";
}

std::vector<std::string> McpCsModifiers(const McpCsSettings& settings, int allyInfluence, int runnerUpInfluence, const std::vector<int>& allyHistory)
{
	std::vector<std::string> out;
	int lead = allyInfluence - runnerUpInfluence;
	if(lead < settings.contestedLead) out.push_back(McpCsModifierText(MCP_CS_CONTESTED));
	else if(lead >= settings.secureLead) out.push_back(McpCsModifierText(MCP_CS_SECURE));
	if(allyHistory.size() >= 2 && settings.trendTurns >= 2)
	{
		size_t back = (size_t)settings.trendTurns - 1;
		size_t from = allyHistory.size() > back ? allyHistory.size() - 1 - back : 0;
		int change = allyHistory.back() - allyHistory[from];
		if(change >= settings.trendAmount) out.push_back(McpCsModifierText(MCP_CS_RISING));
		else if(change <= -settings.trendAmount) out.push_back(McpCsModifierText(MCP_CS_FALLING));
	}
	return out;
}

std::string McpCsToolRefusal(const std::string& tool, const std::string& action, const std::string& item)
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
	if(tool == "economic_overview")
		return "a city-state's treasury is the game's to keep: it never falls below zero, so it is not yours to balance";
	if(tool == "unit_action" && action == "disband")
		return "a city-state does not disband its units: the game takes your city back with the forces it gave you, and running short of gold costs you nothing";
	if((tool == "set_production" || tool == "city_queue") && item.compare(0, 8, "PROCESS_") == 0)
		return "a city-state's city builds things rather than turning its work into gold or research: your treasury never falls below zero, so running short costs you nothing";
	return "";
}
