#pragma once

// The screens' JSON as turn-view items, game-free so it can be tested natively (tests/run_view_items_test.sh).
// McpTurnView.cpp calls the screen tools and hands their results here.

#include "McpTurnCycle.h"
#include "picojson.h"
#include <set>
#include <string>
#include <vector>

// A screen's result as items. Its plain fields make one line, keyed "<section>:summary"; each list of things makes one
// line per thing under the section "<section>: <list>", whether or not the screen's other lists are empty. Empty lists
// are left out.
void McpScreenItems(const std::string& section, const picojson::value& result, std::vector<McpViewItem>& items);
// An object's fields in the order they are shown: name, civ, civilization, leader and owner first, then x and y as
// "at (x,y)", then the rest; nested objects flattened as "outer.inner", lists as "[a, b]" (objects "[{...}; {...}]").
// Left out: help and how texts, is combat and is worker, empty lists, and type when the object has a name.
void McpObjectFields(const picojson::object& o, std::vector<std::pair<std::string, std::string> >& out);
// The key that names the same thing turn after turn: its id (with its owner, when it has one), else its position,
// else its name, civ, civilization, type, tech, policy, branch or resource, else its place in the list.
std::string McpObjectKey(const std::string& section, const picojson::object& o, size_t index);
// A key already used gets "#2", "#3" in the order the items come.
void McpUniqueKeys(std::vector<McpViewItem>& items);
// One tile as a player names it: what stands out on it (resource, improvement, feature, hill; the terrain only when
// nothing else does), its position and its yields ("deer forest hill (25,22) 2f 1p"; f food, p production, g gold,
// s science, c culture, fa faith; a yield of 0 is left out), and "locked" when a citizen is locked on it.
std::string McpTileShort(const picojson::object& tile);
// A city's citizens screen as fields: works and free, each a list of short tiles, then its other fields (focus,
// specialists, unemployed...), without the city's name and the how text.
void McpCitizensFields(const picojson::object& citizens, std::vector<std::pair<std::string, std::string> >& out);
// A field that moves every turn on its own: stocks (gold, culture, faith), the turn and year, counters (progress,
// turns left, turns, turns to grow, food stored, score, influence), a unit's moves, activity, need for orders and
// healing, a city's fall, the demographics, and the clocks, their notes and their sources, which are recomputed every
// turn (fields named "clock ...", "note" and "sources ...").
bool McpTickingField(const std::string& name);
std::set<std::string> McpTickingFields(const std::vector<McpViewItem>& items);
// A unit's own line without what does not apply to it: can found city here, embarked and needs orders when "no", and
// max hp when it is the usual 100.
void McpTrimUnitFields(std::vector<std::pair<std::string, std::string> >& fields);
