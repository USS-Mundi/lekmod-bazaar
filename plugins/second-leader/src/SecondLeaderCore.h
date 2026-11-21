// Second Leader's decisions, with no game headers so they can be tested natively (tests/run_core.sh). The plugin
// reads the game's database, hands the facts here, and applies the answers. See ../SPEC.md.
#pragma once
#include <set>
#include <string>
#include <vector>

// The same order for the same seed, on every machine and every load of the game.
std::vector<int> SlShuffle(const std::vector<int>& items, unsigned int seed);

// Each player's second civilization. players[i] is player i's civilization, -1 where no one plays; pool is every
// civilization that may be a second. A second is unique in the game: never a civilization a player plays, never another
// player's second. -1 for a player with no civilization, or once the pool runs out.
std::vector<int> SlPair(const std::vector<int>& players, const std::vector<int>& pool, unsigned int seed);

// One player slot as the setup holds it. An empty slot can hold a default civilization and leader too.
struct SlSlot { int civ; int leader; };

// Each slot's civilization if it takes part in the pairing, else -1. A new game takes the slots in play (active).
// A saved game takes the slots recorded when it began, so a player killed since keeps its place and the survivors
// keep their seconds; the record counts only when every slot in it still holds what it recorded and every slot in
// play is in it (fromRecord says which was used).
std::vector<int> SlTakingPart(const std::vector<SlSlot>& slots, const std::vector<bool>& active, const std::vector<SlSlot>& recorded, bool& fromRecord);

// The record of the slots in play: one "player civ leader" line each.
std::string SlRecordText(const std::vector<SlSlot>& slots, const std::vector<bool>& active);
// The record read back as n slots (-1 where none was in play); empty when it is empty or any line does not parse.
std::vector<SlSlot> SlRecordParse(const std::string& text, size_t n);

// Whether a database value counts as set: anything but NULL, empty, 0, false or NONE.
bool SlIsSet(const char* value);

// What a second leader's trait leaves out when it merges row by row (everything else combines with the first's): each
// slot both fill, where the first's stays ("Traits.FreeBuilding", or a table row's key such as
// "Trait_BuildImprovementBuildTimeOverride:BUILD_FARM"), then each of the second's bans, which never pass on. Slots in
// sorted order, then bans in sorted order.
std::vector<std::string> SlLeftOut(const std::set<std::string>& firstSlots, const std::set<std::string>& secondSlots, const std::set<std::string>& secondBans);

// One civilization's free starting units of a class: how many, and the unit AI they start with.
struct SlFreeUnit { int cls; int count; int ai; };
// The second civilization's free starting units a merged civilization gains (Nubia's free Scout, which its unique unit
// makes an Apedemak's Bow): for each class the second starts with more of, its count, with the first's unit AI where
// the first starts with that class too, else the second's. In the second's order.
std::vector<SlFreeUnit> SlMergeFreeUnits(const std::vector<SlFreeUnit>& first, const std::vector<SlFreeUnit>& second);

// One civilization's override of a unit or building class; item -1 forbids the class (as Venice has no Settler).
struct SlOverride { int cls; int item; };
// A second civilization's unique: merged false where the first civilization overrides that class itself.
struct SlMerged { int cls; int item; bool merged; };
// The second's uniques (never its forbidden classes), each merged unless the first overrides the same class.
std::vector<SlMerged> SlMergeOverrides(const std::vector<SlOverride>& first, const std::vector<SlOverride>& second);

// Game text as plain words for an agent: [NEWLINE] becomes a space, other [MARKUP] goes, spaces collapse.
std::string SlPlain(const std::string& text);

// Whether a string is a database type name (letters, digits, underscores), safe to put in a query.
bool SlIsTypeName(const std::string& s);

// A string as a JSON string literal, quotes included (the agents' machine-readable file).
std::string SlJsonString(const std::string& s);
