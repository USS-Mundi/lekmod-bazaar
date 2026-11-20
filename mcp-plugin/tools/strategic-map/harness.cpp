// Draws a map picture from a fixture with the plugin's own drawing code, outside the game, so the picture can be
// changed and looked at without restarting Civilization V. Built and run by run.sh.
//
// A fixture is lines of tab-separated fields:
//   GRID    x0 y1 cols rows wrapWidth
//   LEGEND  color(hex) name
//   KEY     text
//   FOOTER  text
//   HEX     x y fill fogged unexplored rivers(E,SE,SW as 0/1) hasRoad roads(6 x 0/1) owned borders(6 x 0/1) borderColor
//           worked topRight centerLabel coords city cityColor cityLabel cityHp cityAtWar
//   UNIT    x y label fill ring hp [ownerTag atWar(0/1)]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "McpMapDraw.h"

namespace
{
	std::vector<std::string> Split(const std::string& line)
	{
		std::vector<std::string> out;
		std::string cur;
		for(size_t i = 0; i < line.size(); ++i)
		{
			if(line[i] == '\t') { out.push_back(cur); cur.clear(); }
			else cur += line[i];
		}
		out.push_back(cur);
		return out;
	}

	unsigned Color(const std::string& s) { return (unsigned)strtoul(s.c_str(), NULL, 16); }
	int Int(const std::string& s) { return atoi(s.c_str()); }
	bool Bit(const std::string& s, size_t i) { return i < s.size() && s[i] == '1'; }
}

int main(int argc, char** argv)
{
	if(argc < 3) { fprintf(stderr, "usage: harness <fixture> <out.png>\n"); return 2; }
	FILE* f = fopen(argv[1], "rb");
	if(!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
	int x0 = 0, y1 = 0, cols = 0, rows = 0, wrap = 0;
	std::vector<McpMapHex> hexes;
	std::vector<McpMapLegendEntry> legend;
	std::vector<std::string> key;
	std::string footer;
	std::map<std::pair<int, int>, size_t> at;
	static char buf[16384];
	while(fgets(buf, sizeof(buf), f))
	{
		std::string line(buf);
		while(!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r')) line.erase(line.size() - 1);
		std::vector<std::string> t = Split(line);
		if(t[0] == "GRID" && t.size() >= 6) { x0 = Int(t[1]); y1 = Int(t[2]); cols = Int(t[3]); rows = Int(t[4]); wrap = Int(t[5]); }
		else if(t[0] == "LEGEND" && t.size() >= 3) { McpMapLegendEntry e; e.color = Color(t[1]); e.name = t[2]; legend.push_back(e); }
		else if(t[0] == "KEY" && t.size() >= 2) key.push_back(t[1]);
		else if(t[0] == "FOOTER" && t.size() >= 2) footer = t[1];
		else if(t[0] == "HEX" && t.size() >= 21)
		{
			McpMapHex h;
			h.x = Int(t[1]); h.y = Int(t[2]); h.fill = Color(t[3]);
			h.fogged = t[4] == "1"; h.unexplored = t[5] == "1";
			h.riverE = Bit(t[6], 0); h.riverSE = Bit(t[6], 1); h.riverSW = Bit(t[6], 2);
			h.hasRoad = t[7] == "1";
			for(int k = 0; k < 6; ++k) { h.road[k] = Bit(t[8], k); h.border[k] = Bit(t[10], k); }
			h.owned = t[9] == "1"; h.borderColor = Color(t[11]); h.worked = t[12] == "1";
			h.topRight = t[13]; h.centerLabel = t[14]; h.coords = t[15];
			h.city = t[16] == "1"; h.cityColor = Color(t[17]); h.cityLabel = t[18]; h.cityHp = Int(t[19]); h.cityAtWar = t[20] == "1";
			at[std::make_pair(h.x, h.y)] = hexes.size();
			hexes.push_back(h);
		}
		else if(t[0] == "UNIT" && t.size() >= 7)
		{
			std::map<std::pair<int, int>, size_t>::iterator it = at.find(std::make_pair(Int(t[1]), Int(t[2])));
			if(it == at.end()) continue;
			McpMapUnit u;
			u.label = t[3]; u.fill = Color(t[4]); u.ring = Color(t[5]); u.hp = Int(t[6]);
			if(t.size() >= 9) { u.tag = t[7]; u.atWar = t[8] == "1"; }
			McpMapHex& h = hexes[it->second];
			if(h.units.size() >= 3) ++h.extraUnits;
			else h.units.push_back(u);
		}
	}
	fclose(f);
	std::string err;
	std::string png = McpDrawHexMapPng(hexes, legend, footer, x0, y1, cols, rows, wrap, err, key);
	if(png.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	FILE* o = fopen(argv[2], "wb");
	if(!o) { fprintf(stderr, "cannot write %s\n", argv[2]); return 2; }
	fwrite(png.data(), 1, png.size(), o);
	fclose(o);
	McpShutdownMapDraw();
	printf("wrote %s (%d hexes)\n", argv[2], (int)hexes.size());
	return 0;
}
