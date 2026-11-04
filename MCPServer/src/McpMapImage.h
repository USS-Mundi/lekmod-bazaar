#pragma once

#include <string>
#include "picojson.h"

// Game thread. Draws the tiles within radius of (cx, cy) as the model's civilization knows
// them, with the map tool's fog rules, and returns the picture as PNG bytes; summary gets a
// line of text about what is in it, and cities and units the roster of what the picture shows,
// with exact coordinates. Empty on failure, with the reason in summary.
std::string McpRenderMapPng(PlayerTypes eMe, int cx, int cy, int radius, std::string& summary, picojson::array& cities, picojson::array& units);

std::string McpBase64(const std::string& bytes);

// Game thread. The strategic picture: one map over every city and unit of the seat's and every unit and city it can see
// now, units marked by owner, relation and health and named by short codes, with a key on the picture saying what each
// mark means. Empty on failure, with the reason in summary.
std::string McpRenderStrategicPng(PlayerTypes eMe, std::string& summary);

// Game thread. A seat's land for the start of its turn: the strategic picture under image_png_base64 (a list of one),
// and under your_land a line about it and one per city with the tiles it can buy and their prices.
void McpAddLandToTurnStart(PlayerTypes eMe, picojson::object& o);
