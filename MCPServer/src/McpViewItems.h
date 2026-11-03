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
// A field that moves every turn on its own: stocks (gold, culture, faith), the turn and year, counters (progress,
// turns left, turns to grow, food stored, score, influence) and a unit's moves, activity and need for orders.
bool McpTickingField(const std::string& name);
std::set<std::string> McpTickingFields(const std::vector<McpViewItem>& items);
