#pragma once

#include <string>
#include <vector>

// Drawing only, with no game headers: the game's precompiled header strips Windows.h of the
// drawing parts GDI+ needs and they cannot be brought back afterwards, so this file is built
// without that header. McpMapImage.cpp gathers the game data into these records.

struct McpMapUnit
{
	std::string label;
	unsigned fill, ring;   // 0xRRGGBB
	int hp;                // percent of full; under 100 a bar shows it
	McpMapUnit() : fill(0), ring(0), hp(100) {}
};

struct McpMapHex
{
	int x, y;
	unsigned fill;                      // 0xRRGGBB
	bool fogged;                        // explored but out of sight now: shaded
	bool unexplored;                    // never seen: a dark hex with a question mark and nothing else
	bool riverE, riverSE, riverSW;      // a river along that edge
	bool hasRoad;                       // a road on the tile: a dot, and lines toward neighbors with roads
	bool road[6];                       // a road toward the neighbor in each direction: NE, E, SE, SW, W, NW
	bool owned;                         // inside someone's borders: the hex is tinted with the owner's color
	bool border[6];                     // the owner changes across that edge
	unsigned borderColor;               // the owner's color
	std::string topLeft, topRight, bottomLeft, coords;
	std::string centerLabel;            // drawn in the middle when there is no city and no unit (MTN)
	bool city;
	unsigned cityColor;
	std::string cityLabel;              // the name and its size: "Tunis 7"
	int cityHp;                         // percent of full; under 100 a bar shows it
	bool cityAtWar;                     // its owner is at war with the viewer: a red outline
	std::vector<McpMapUnit> units;   // up to three; two are drawn, the rest counted in the +N
	int extraUnits;
	bool worked;                        // a citizen of the viewer's city works it: a white dot under the ground label

	McpMapHex() : x(0), y(0), fill(0), fogged(false), unexplored(false), riverE(false), riverSE(false), riverSW(false), hasRoad(false), owned(false), borderColor(0), city(false), cityColor(0), cityHp(100), cityAtWar(false), extraUnits(0), worked(false)
	{
		for(int i = 0; i < 6; ++i) road[i] = border[i] = false;
	}
};

struct McpMapLegendEntry
{
	unsigned color;
	std::string name;
};

// Draws the hexes on a grid whose left column is tile x0 and top row tile y1 (y grows
// northward, so the top row is the highest y), cols hexes wide and rows high, then the legend,
// the key (a line each) and a footer line. wrapWidth is the map's width when it wraps east-west,
// so the x labels wrap with it; 0 otherwise. Returns PNG bytes, or "" with the reason in err.
std::string McpDrawHexMapPng(const std::vector<McpMapHex>& hexes, const std::vector<McpMapLegendEntry>& legend, const std::string& footer, int x0, int y1, int cols, int rows, int wrapWidth, std::string& err,
	const std::vector<std::string>& key = std::vector<std::string>());

// Keeps a copy of the picture as Logs\map_image_last.png under the game's My Games folder,
// so the human can see what the model saw.
void McpSaveMapCopy(const std::string& png);

void McpShutdownMapDraw();
