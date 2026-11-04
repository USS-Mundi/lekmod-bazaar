#include "CvGameCoreDLLPCH.h"
#include "McpMapImage.h"
#include "McpMapDraw.h"
#include "McpToolSupport.h"
#include "McpTurnCycle.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvTeam.h"
#include "CvCity.h"
#include "CvUnit.h"
#include "CvPlot.h"
#include "CvMap.h"
#include "CvGlobals.h"
#include "CvInfos.h"
#include "CvImprovementClasses.h"
#include "CvGameCoreUtils.h"
#include <set>
#include <map>
#include <climits>

using namespace McpTool;

namespace
{
	unsigned Rgb(int r, int g, int b) { return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b; }

	unsigned Darker(unsigned c, int by)
	{
		int r = std::max(0, (int)((c >> 16) & 255) - by), g = std::max(0, (int)((c >> 8) & 255) - by), b = std::max(0, (int)(c & 255) - by);
		return Rgb(r, g, b);
	}

	unsigned TerrainColor(const CvPlot* p)
	{
		if(p->isMountain()) return Rgb(96, 90, 86);
		CvTerrainInfo* info = GC.getTerrainInfo(p->getTerrainType());
		std::string type = info ? info->GetType() : "";
		unsigned c = Rgb(120, 165, 90);
		if(type == "TERRAIN_PLAINS") c = Rgb(188, 156, 88);
		else if(type == "TERRAIN_DESERT") c = Rgb(240, 226, 120);
		else if(type == "TERRAIN_TUNDRA") c = Rgb(152, 170, 168);
		else if(type == "TERRAIN_SNOW") c = Rgb(236, 236, 242);
		else if(type == "TERRAIN_COAST") c = Rgb(120, 178, 228);
		else if(type == "TERRAIN_OCEAN") c = Rgb(52, 92, 176);
		if(p->isHills()) c = Darker(c, 32);
		return c;
	}

	std::string FeatureGlyph(const CvPlot* p)
	{
		if(p->IsNaturalWonder()) return "NW";
		if(p->getFeatureType() == NO_FEATURE) return "";
		CvFeatureInfo* info = GC.getFeatureInfo(p->getFeatureType());
		std::string type = info ? info->GetType() : "";
		if(type == "FEATURE_FOREST") return "F";
		if(type == "FEATURE_JUNGLE") return "J";
		if(type == "FEATURE_MARSH") return "M";
		if(type == "FEATURE_OASIS") return "O";
		if(type == "FEATURE_FLOOD_PLAINS") return "fp";
		if(type == "FEATURE_ICE") return "ice";
		if(type == "FEATURE_FALLOUT") return "X";
		if(type == "FEATURE_ATOLL") return "atl";
		return "?";
	}

	unsigned OwnerColor(PlayerTypes e)
	{
		CvPlayerColorInfo* pc = GC.GetPlayerColorInfo(GET_PLAYER(e).getPlayerColor());
		CvColorInfo* ci = pc ? GC.GetColorInfo((ColorTypes)pc->GetColorTypePrimary()) : NULL;
		if(!ci) return Rgb(200, 200, 200);
		const CvColorA& c = ci->GetColor();
		return Rgb((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255));
	}

	// Cuts to at most n characters without splitting a UTF-8 sequence.
	std::string Cut(const std::string& s, size_t n)
	{
		size_t i = 0, chars = 0;
		while(i < s.size() && chars < n)
		{
			unsigned char c = (unsigned char)s[i];
			i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
			++chars;
		}
		return s.substr(0, std::min(i, s.size()));
	}

	std::string Abbrev(const std::string& name)
	{
		if(name.find(' ') == std::string::npos) return Cut(name, 3);
		std::string out;
		bool bStart = true;
		for(size_t i = 0; i < name.size() && out.size() < 3; ++i)
		{
			if(name[i] == ' ') { bStart = true; continue; }
			if(bStart) out += name[i];
			bStart = false;
		}
		return out;
	}

	int EdgeOf(DirectionTypes d)
	{
		switch(d)
		{
		case DIRECTION_NORTHEAST: return 0;
		case DIRECTION_EAST: return 1;
		case DIRECTION_SOUTHEAST: return 2;
		case DIRECTION_SOUTHWEST: return 3;
		case DIRECTION_WEST: return 4;
		default: return 5;
		}
	}
}

std::string McpBase64(const std::string& bytes)
{
	static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((bytes.size() + 2) / 3 * 4);
	size_t i = 0;
	for(; i + 2 < bytes.size(); i += 3)
	{
		unsigned v = ((unsigned char)bytes[i] << 16) | ((unsigned char)bytes[i + 1] << 8) | (unsigned char)bytes[i + 2];
		out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63]; out += tbl[v & 63];
	}
	if(i < bytes.size())
	{
		unsigned v = (unsigned char)bytes[i] << 16;
		if(i + 1 < bytes.size()) v |= (unsigned char)bytes[i + 1] << 8;
		out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
		out += (i + 1 < bytes.size()) ? tbl[(v >> 6) & 63] : '=';
		out += '=';
	}
	return out;
}

namespace
{
	// Short codes for the unit types in one picture, each unique: a one-word type by its first two letters ("Wa"
	// Warrior), a longer one by its initials ("CB" Composite Bowman), longer letters when two would clash.
	struct UnitCodes
	{
		std::map<std::string, std::string> byName, byCode;
		std::string For(const std::string& name)
		{
			std::map<std::string, std::string>::iterator it = byName.find(name);
			if(it != byName.end()) return it->second;
			std::string compact, initials;
			bool bStart = true;
			for(size_t i = 0; i < name.size(); ++i)
			{
				if(name[i] == ' ') { bStart = true; continue; }
				compact += name[i];
				if(bStart) initials += (char)toupper((unsigned char)name[i]);
				bStart = false;
			}
			std::string code = initials.size() > 1 ? initials.substr(0, 3) : Cut(compact, 2);
			for(size_t n = 3; byCode.count(code) && n <= compact.size() + 1; ++n)
				code = n <= compact.size() ? Cut(compact, n) : compact + "2";
			byName[name] = code;
			byCode[code] = name;
			return code;
		}
		std::string Key() const
		{
			std::string out = "units:";
			for(std::map<std::string, std::string>::const_iterator it = byCode.begin(); it != byCode.end(); ++it)
				out += (it == byCode.begin() ? " " : " · ") + it->first + " " + it->second;
			return out;
		}
	};

	struct HexCounts
	{
		int unexplored, fogged;
		bool anyWorked;
		HexCounts() : unexplored(0), fogged(0), anyWorked(false) {}
	};

	// One tile as a picture shows it, at grid position (x, y). The strategic picture leaves resources and improvements
	// to the map tool and names units by their codes; both give a city's and a unit's health.
	McpMapHex HexOf(PlayerTypes eMe, int x, int y, CvPlot* p, bool bStrategic, UnitCodes& codes, std::set<PlayerTypes>& owners,
		JArr& cities, JArr& units, HexCounts& counts)
	{
		TeamTypes eTeam = GET_PLAYER(eMe).getTeam();
		CvTeam& myTeam = GET_TEAM(eTeam);
		McpMapHex h;
		h.x = x;
		h.y = y;
		if(!p->isRevealed(eTeam))
		{
			++counts.unexplored;
			h.unexplored = true;
			return h;
		}
		h.fill = TerrainColor(p);
		h.fogged = !p->isVisible(eTeam);
		if(h.fogged) ++counts.fogged;
		h.riverE = p->isWOfRiver();
		h.riverSE = p->isNWOfRiver();
		h.riverSW = p->isNEOfRiver();
		h.coords = McpFormat("%d,%d", p->getX(), p->getY());

		RouteTypes eRoute = p->getRevealedRouteType(eTeam);
		h.hasRoad = eRoute != NO_ROUTE;
		PlayerTypes eOwner = p->getRevealedOwner(eTeam);
		if(eOwner != NO_PLAYER) { h.owned = true; h.borderColor = OwnerColor(eOwner); owners.insert(eOwner); }
		CvCity* pWorker = p->isBeingWorked() ? p->getWorkingCity() : NULL;
		h.worked = pWorker && pWorker->getOwner() == eMe && !p->isCity();
		if(h.worked) counts.anyWorked = true;
		for(int d = 0; d < NUM_DIRECTION_TYPES; ++d)
		{
			CvPlot* n = plotDirection(p->getX(), p->getY(), (DirectionTypes)d);
			bool bKnown = n && n->isRevealed(eTeam);
			int k = EdgeOf((DirectionTypes)d);
			h.road[k] = eRoute != NO_ROUTE && bKnown && n->getRevealedRouteType(eTeam) != NO_ROUTE;
			h.border[k] = eOwner != NO_PLAYER && !(bKnown && n->getRevealedOwner(eTeam) == eOwner);
		}

		ResourceTypes eRes = p->getResourceType(eTeam);
		if(eRes != NO_RESOURCE && !bStrategic) h.topLeft = Cut(ResourceName(eRes), 6);
		h.topRight = FeatureGlyph(p);
		if(p->isMountain()) h.centerLabel = "MTN";
		else if(p->isHills()) h.topRight += "h";
		CvCity* city = p->getPlotCity();
		if(city && !city->isRevealed(eTeam, false)) city = NULL;
		ImprovementTypes eImp = p->getRevealedImprovementType(eTeam);
		if(eImp != NO_IMPROVEMENT && !city && !bStrategic)
		{
			CvImprovementEntry* pImp = GC.getImprovementInfo(eImp);
			std::string name = (pImp && pImp->IsGoody()) ? "ruins" : ImprovementName(eImp);
			for(size_t k = 0; k < name.size(); ++k) name[k] = (char)tolower((unsigned char)name[k]);
			h.bottomLeft = Cut(name, 5);
		}

		if(city)
		{
			// Under fog the city is shown as last seen: its owner then, grey if it has changed hands since.
			PlayerTypes eCityOwner = h.fogged ? eOwner : city->getOwner();
			h.city = true;
			h.cityColor = eCityOwner != NO_PLAYER ? OwnerColor(eCityOwner) : Rgb(120, 120, 120);
			h.cityLabel = Cut(city->getName(), 9);
			if(h.cityLabel.size() < city->getName().size()) h.cityLabel = Cut(city->getName(), 8) + ".";
			if(bStrategic && !h.fogged) h.cityLabel += McpFormat(" %d", city->getPopulation());
			if(!h.fogged) h.cityHp = 100 - city->getDamage() * 100 / std::max(1, city->GetMaxHitPoints());
			h.cityAtWar = eCityOwner != NO_PLAYER && eCityOwner != eMe && myTeam.isAtWar(GET_PLAYER(eCityOwner).getTeam());
			if(eCityOwner != NO_PLAYER) owners.insert(eCityOwner);
			JObj co;
			co["name"] = J(city->getName().c_str());
			co["x"] = J(p->getX());
			co["y"] = J(p->getY());
			co["owner"] = J(eCityOwner != NO_PLAYER ? CivName(eCityOwner) : "unknown: it changed hands while out of sight");
			if(h.fogged) co["out_of_sight"] = J(true);
			cities.push_back(JVal(co));
		}
		else if(!h.fogged)
		{
			for(int k = 0; k < p->getNumUnits(); ++k)
			{
				CvUnit* u = p->getUnitByIndex(k);
				if(!u || u->isInvisible(eTeam, false)) continue;
				owners.insert(u->getOwner());
				const char* relation = u->getOwner() == eMe ? "yours" : u->getTeam() == eTeam ? "teammate" : myTeam.isAtWar(u->getTeam()) ? "enemy" : "other";
				JObj uo;
				uo["x"] = J(p->getX());
				uo["y"] = J(p->getY());
				uo["type"] = J(UnitTypeName(u->getUnitType()));
				uo["owner"] = J(CivName(u->getOwner()));
				uo["relation"] = J(relation);
				units.push_back(JVal(uo));
				if(h.units.size() >= 3) { ++h.extraUnits; continue; }
				McpMapUnit mu;
				mu.label = bStrategic ? codes.For(UnitTypeName(u->getUnitType())) : Abbrev(UnitTypeName(u->getUnitType()));
				mu.fill = OwnerColor(u->getOwner());
				mu.ring = u->getOwner() == eMe ? Rgb(255, 255, 255)
				        : u->getTeam() == eTeam ? Rgb(60, 230, 90)
				        : myTeam.isAtWar(u->getTeam()) ? Rgb(240, 40, 40)
				        : Rgb(150, 150, 150);
				mu.hp = u->GetCurrHitPoints() * 100 / std::max(1, u->GetMaxHitPoints());
				h.units.push_back(mu);
			}
		}
		return h;
	}

	std::vector<McpMapLegendEntry> LegendOf(PlayerTypes eMe, const std::set<PlayerTypes>& owners)
	{
		TeamTypes eTeam = GET_PLAYER(eMe).getTeam();
		CvTeam& myTeam = GET_TEAM(eTeam);
		std::vector<McpMapLegendEntry> legend;
		for(std::set<PlayerTypes>::const_iterator it = owners.begin(); it != owners.end(); ++it)
		{
			McpMapLegendEntry e;
			e.color = OwnerColor(*it);
			bool bKnown = GET_PLAYER(*it).getTeam() == eTeam || myTeam.isHasMet(GET_PLAYER(*it).getTeam());
			e.name = bKnown ? CivName(*it) : "unmet civilization";
			if(*it == eMe) e.name += " (you)";
			else if(GET_PLAYER(*it).getTeam() == eTeam) e.name += " (teammate)";
			else if(myTeam.isAtWar(GET_PLAYER(*it).getTeam())) e.name += " (at war)";
			legend.push_back(e);
		}
		return legend;
	}
}

std::string McpRenderMapPng(PlayerTypes eMe, int cx, int cy, int radius, std::string& summary, JArr& cities, JArr& units)
{
	CvMap& map = GC.getMap();
	int x0 = cx - radius - 1, y1 = cy + radius;
	std::vector<McpMapHex> hexes;
	std::set<PlayerTypes> owners;
	UnitCodes codes;
	HexCounts counts;
	for(int y = cy - radius; y <= cy + radius; ++y)
		for(int x = x0; x <= cx + radius + 1; ++x)
		{
			if(plotDistance(cx, cy, x, y) > radius) continue;
			CvPlot* p = map.plot(x, y);
			if(p) hexes.push_back(HexOf(eMe, x, y, p, false, codes, owners, cities, units, counts));
		}
	std::string footer = McpFormat("center %d,%d  radius %d  hatched = out of sight now  ? = unexplored", cx, cy, radius);
	if(counts.anyWorked) footer += "  white dot = worked by your city";
	std::string err;
	std::string png = McpDrawHexMapPng(hexes, LegendOf(eMe, owners), footer, x0, y1, 2 * radius + 3, 2 * radius + 1, map.isWrapX() ? map.getGridWidth() : 0, err);
	if(png.empty()) { summary = err; return ""; }
	McpSaveMapCopy(png);
	summary = McpFormat("%d tiles drawn around (%d,%d); %d of them out of sight now; %d unexplored, marked ?", (int)hexes.size() - counts.unexplored, cx, cy, counts.fogged, counts.unexplored);
	return png;
}

std::string McpRenderStrategicPng(PlayerTypes eMe, std::string& summary)
{
	CvPlayer& me = GET_PLAYER(eMe);
	TeamTypes eTeam = me.getTeam();
	CvMap& map = GC.getMap();
	int minX = INT_MAX, maxX = INT_MIN, minY = INT_MAX, maxY = INT_MIN;
	struct Grow
	{
		static void At(int x, int y, int& minX, int& maxX, int& minY, int& maxY)
		{
			minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y);
		}
	};
	int iLoop;
	for(CvCity* c = me.firstCity(&iLoop); c; c = me.nextCity(&iLoop)) Grow::At(c->getX(), c->getY(), minX, maxX, minY, maxY);
	for(CvUnit* u = me.firstUnit(&iLoop); u; u = me.nextUnit(&iLoop)) Grow::At(u->getX(), u->getY(), minX, maxX, minY, maxY);
	for(int i = 0; i < MAX_PLAYERS; ++i)
	{
		CvPlayer& other = GET_PLAYER((PlayerTypes)i);
		if(i == (int)eMe || !other.isAlive()) continue;
		for(CvUnit* u = other.firstUnit(&iLoop); u; u = other.nextUnit(&iLoop))
			if(u->plot() && u->plot()->isVisible(eTeam) && !u->isInvisible(eTeam, false)) Grow::At(u->getX(), u->getY(), minX, maxX, minY, maxY);
		for(CvCity* c = other.firstCity(&iLoop); c; c = other.nextCity(&iLoop))
			if(c->plot() && c->plot()->isVisible(eTeam)) Grow::At(c->getX(), c->getY(), minX, maxX, minY, maxY);
	}
	if(minX > maxX) { summary = "nothing of yours on the map yet"; return ""; }
	minX -= 1; maxX += 1; minY = std::max(0, minY - 1); maxY = std::min(map.getGridHeight() - 1, maxY + 1);
	if(!map.isWrapX()) { minX = std::max(0, minX); maxX = std::min(map.getGridWidth() - 1, maxX); }
	if(maxX - minX + 1 > map.getGridWidth()) maxX = minX + map.getGridWidth() - 1;

	std::vector<McpMapHex> hexes;
	std::set<PlayerTypes> owners;
	UnitCodes codes;
	HexCounts counts;
	JArr cities, units;
	for(int y = maxY; y >= minY; --y)
		for(int x = minX; x <= maxX; ++x)
		{
			CvPlot* p = map.plot(x, y);
			if(p) hexes.push_back(HexOf(eMe, x, y, p, true, codes, owners, cities, units, counts));
		}
	std::vector<std::string> key;
	key.push_back("ring: white = yours · green = teammate · red = at war with you · grey = not at war · bar under a unit or city = its health");
	if(!codes.byCode.empty()) key.push_back(codes.Key());
	key.push_back("ground: F forest · J jungle · M marsh · fp flood plains · h hills · MTN mountain · NW natural wonder · brown line = road · blue edge = river"
		" · hatched = out of sight now · ? = unexplored" + std::string(counts.anyWorked ? " · white dot = worked by your city" : ""));
	std::string footer = McpFormat("TURN %d · %s · x%d-x%d, y%d-y%d", GC.getGame().getGameTurn(), CivName(eMe).c_str(), minX, maxX, minY, maxY);
	std::string err;
	std::string png = McpDrawHexMapPng(hexes, LegendOf(eMe, owners), footer, minX, maxY, maxX - minX + 1, maxY - minY + 1, map.isWrapX() ? map.getGridWidth() : 0, err, key);
	if(png.empty()) { summary = err; return ""; }
	McpSaveMapCopy(png);
	summary = McpFormat("x%d-x%d, y%d-y%d: every city and unit of yours and every unit and city in sight; %d units, %d cities shown",
		minX, maxX, minY, maxY, (int)units.size(), (int)cities.size());
	return png;
}

void McpAddLandToTurnStart(PlayerTypes eMe, picojson::object& o)
{
	CvPlayer& me = GET_PLAYER(eMe);
	TeamTypes eTeam = me.getTeam();
	std::vector<CvCity*> cities;
	int iLoop = 0;
	for(CvCity* c = me.firstCity(&iLoop); c; c = me.nextCity(&iLoop)) cities.push_back(c);

	JArr images, lines;
	std::string summary;
	std::string png = McpRenderStrategicPng(eMe, summary);
	if(png.empty()) lines.push_back(J("the picture could not be drawn: " + summary));
	else
	{
		images.push_back(J(McpBase64(png)));
		lines.push_back(J("picture: " + summary));
	}

#ifdef MCP_LEKMOD
	int iBuyRange = GC.getMAXIMUM_BUY_PLOT_DISTANCE();
#endif
	for(size_t k = 0; k < cities.size(); ++k)
	{
		CvCity* c = cities[k];
#ifndef MCP_LEKMOD
		int iBuyRange = c->getBuyPlotDistance();
#endif
		std::string withResource;
		int iCheapest = -1;
		for(int dy = -iBuyRange; dy <= iBuyRange; ++dy)
			for(int dx = -iBuyRange; dx <= iBuyRange; ++dx)
			{
				CvPlot* p = plotXYWithRangeCheck(c->getX(), c->getY(), dx, dy, iBuyRange);
				if(!p || !c->CanBuyPlot(p->getX(), p->getY(), true)) continue;
				int iCost = c->GetBuyPlotCost(p->getX(), p->getY());
				ResourceTypes eRes = p->getResourceType(eTeam);
				if(eRes != NO_RESOURCE)
					withResource += McpFormat("%s(%d,%d) %s %dg", withResource.empty() ? "" : ", ", p->getX(), p->getY(), ResourceName(eRes), iCost);
				else if(iCheapest < 0 || iCost < iCheapest) iCheapest = iCost;
			}
		std::string line = McpFormat("%s (%d,%d) pop %d: ", c->getName().c_str(), c->getX(), c->getY(), c->getPopulation());
		if(withResource.empty() && iCheapest < 0) line += "nothing to buy";
		else
		{
			if(!withResource.empty()) line += "buy " + withResource;
			if(iCheapest >= 0) line += McpFormat("%sother tiles from %dg", withResource.empty() ? "buy " : "; ", iCheapest);
		}
		lines.push_back(J(line));
	}
	o["your_land"] = JVal(lines);
	if(!images.empty()) o["image_png_base64"] = JVal(images);
}
