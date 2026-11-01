#include "McpCityStateRules.h"
#include <cstddef>

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
	McpCsSettings s = { 1, 20, 60, 5, 15 };
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
