// Native tests for Second Leader's decisions. Run: tests/run_core.sh
#include "../src/SecondLeaderCore.h"
#include <stdio.h>

static int g_run = 0, g_failed = 0;
#define CHECK(c) do { ++g_run; if(!(c)) { ++g_failed; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while(0)

int main()
{
	// shuffle: a permutation, the same for the same seed, different for another
	std::vector<int> items;
	for(int i = 0; i < 40; ++i) items.push_back(i);
	std::vector<int> a = SlShuffle(items, 7), b = SlShuffle(items, 7), c = SlShuffle(items, 8);
	CHECK(a == b && a != c && a != items);
	std::set<int> seen(a.begin(), a.end());
	CHECK(seen.size() == 40);

	// pairing: every player gets a second no one plays and no one else got
	std::vector<int> players;
	players.push_back(3); players.push_back(-1); players.push_back(5); players.push_back(9);
	std::vector<int> pool;
	for(int i = 0; i < 12; ++i) pool.push_back(i);
	std::vector<int> s = SlPair(players, pool, 1234);
	CHECK(s.size() == 4 && s[1] == -1);
	std::set<int> used;
	for(int i = 0; i < 4; ++i)
	{
		if(players[i] < 0) continue;
		CHECK(s[i] >= 0 && s[i] != 3 && s[i] != 5 && s[i] != 9);
		CHECK(used.insert(s[i]).second);
	}
	CHECK(SlPair(players, pool, 1234) == s);                       // a reload pairs the same
	// a pool too small: the last players go without
	std::vector<int> tiny;
	tiny.push_back(3); tiny.push_back(20);
	std::vector<int> t = SlPair(players, tiny, 1);
	CHECK(t[0] == 20 && t[2] == -1 && t[3] == -1);
	// a civilization listed twice in the pool is still given once
	std::vector<int> twice;
	twice.push_back(20); twice.push_back(20); twice.push_back(21);
	std::vector<int> u = SlPair(players, twice, 5);
	CHECK(u[0] != u[2] && u[3] == -1);

	// who takes part: at a new game, the slots in play; an empty slot holds a default civilization and leader too
	std::vector<SlSlot> slots(5);
	slots[0].civ = 3; slots[0].leader = 30;    // the human
	slots[1].civ = 7; slots[1].leader = 70;    // empty: holds defaults
	slots[2].civ = 5; slots[2].leader = 50;    // an AI
	slots[3].civ = 9; slots[3].leader = 90;    // an AI
	slots[4].civ = -1; slots[4].leader = -1;   // empty
	std::vector<bool> active(5, false);
	active[0] = active[2] = active[3] = true;
	std::vector<SlSlot> none;
	bool fromRecord = true;
	std::vector<int> part = SlTakingPart(slots, active, none, fromRecord);
	CHECK(!fromRecord && part.size() == 5 && part[0] == 3 && part[1] == -1 && part[2] == 5 && part[3] == 9 && part[4] == -1);
	// the record written at the start, read back
	std::string rec = SlRecordText(slots, active);
	std::vector<SlSlot> back = SlRecordParse(rec, 5);
	CHECK(back.size() == 5 && back[0].civ == 3 && back[0].leader == 30 && back[1].civ == -1 && back[3].civ == 9 && back[3].leader == 90);
	CHECK(SlRecordParse("0 3 30\nnonsense\n", 5).empty() && SlRecordParse("9 3 30\n", 5).empty() && SlRecordParse("", 5).empty());
	// a reload after player 2 was killed: its slot is no longer in play, and still takes part
	std::vector<bool> later = active;
	later[2] = false;
	part = SlTakingPart(slots, later, back, fromRecord);
	CHECK(fromRecord && part[0] == 3 && part[1] == -1 && part[2] == 5 && part[3] == 9);
	// so the survivors keep their seconds
	CHECK(SlPair(SlTakingPart(slots, active, none, fromRecord), pool, 99) == SlPair(part, pool, 99));
	// a record that does not match the slots is not used: another game on the same seed
	std::vector<SlSlot> other = back;
	other[3].leader = 91;
	part = SlTakingPart(slots, later, other, fromRecord);
	CHECK(!fromRecord && part[2] == -1 && part[3] == 9);
	// nor one that misses a slot in play
	std::vector<bool> more = active;
	more[1] = true;
	part = SlTakingPart(slots, more, back, fromRecord);
	CHECK(!fromRecord && part[1] == 7);
	// a killed player's slot dropped from the pairing deals the survivors different seconds
	std::vector<int> dropped = players;
	dropped[2] = -1;
	CHECK(SlPair(dropped, pool, 1234) != s);

	// text inside the agents' machine-readable file
	CHECK(SlJsonString("Lion's Gate") == "\"Lion's Gate\"");
	CHECK(SlJsonString("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"");

	// set values
	CHECK(!SlIsSet(0) && !SlIsSet("") && !SlIsSet("0") && !SlIsSet("0.0") && !SlIsSet("false") && !SlIsSet("NONE"));
	CHECK(SlIsSet("1") && SlIsSet("-25") && SlIsSet("0.5") && SlIsSet("BUILDING_X") && SlIsSet("true"));

	// row-level merge: everything combines; only a slot both fill (the first's stays) and the second's bans are left out
	std::set<std::string> s1, s2, bans;
	s1.insert("Traits.FreeBuilding"); s1.insert("Trait_BuildImprovementBuildTimeOverride:BUILD_FARM");
	s2.insert("Traits.CombatBonusImprovement"); s2.insert("Trait_BuildImprovementBuildTimeOverride:BUILD_MINE");
	CHECK(SlLeftOut(s1, s2, bans).empty());
	s2.insert("Traits.FreeBuilding");
	s2.insert("Trait_BuildImprovementBuildTimeOverride:BUILD_FARM");
	bans.insert("Trait_NoTrain:UNITCLASS_SETTLER");
	std::vector<std::string> out = SlLeftOut(s1, s2, bans);
	CHECK(out.size() == 3 && out[0] == "Trait_BuildImprovementBuildTimeOverride:BUILD_FARM" && out[1] == "Traits.FreeBuilding"
		&& out[2] == "Trait_NoTrain:UNITCLASS_SETTLER");

	// free starting units: the larger count of each class, the first's unit AI where it has the class
	std::vector<SlFreeUnit> u1, u2;
	SlFreeUnit fu;
	fu.cls = 10; fu.count = 1; fu.ai = 1; u1.push_back(fu);        // first: a settler
	fu.cls = 10; fu.count = 1; fu.ai = 1; u2.push_back(fu);        // second: a settler too, so nothing more
	fu.cls = 11; fu.count = 1; fu.ai = 7; u2.push_back(fu);        // second: a scout (Nubia's bow)
	std::vector<SlFreeUnit> gained = SlMergeFreeUnits(u1, u2);
	CHECK(gained.size() == 1 && gained[0].cls == 11 && gained[0].count == 1 && gained[0].ai == 7);
	fu.cls = 10; fu.count = 2; fu.ai = 5; u2[0] = fu;              // a second that starts with two settlers
	gained = SlMergeFreeUnits(u1, u2);
	CHECK(gained.size() == 2 && gained[0].cls == 10 && gained[0].count == 2 && gained[0].ai == 1);
	CHECK(SlMergeFreeUnits(u2, u1).empty());                        // the first starts with more of each already

	// overrides: the second's uniques merge, unless the first overrides that class; the second's bans never come
	std::vector<SlOverride> first, second;
	SlOverride o;
	o.cls = 1; o.item = 101; first.push_back(o);      // first has its own swordsman
	o.cls = 2; o.item = -1; first.push_back(o);       // first cannot build class 2
	o.cls = 1; o.item = 201; second.push_back(o);     // second's swordsman: clashes
	o.cls = 3; o.item = 203; second.push_back(o);     // second's archer: merges
	o.cls = 2; o.item = 202; second.push_back(o);     // first bans class 2: stays banned
	o.cls = 4; o.item = -1; second.push_back(o);      // second's ban: never passed on
	std::vector<SlMerged> m = SlMergeOverrides(first, second);
	CHECK(m.size() == 3);
	bool sw = false, ar = false, banned = false;
	for(size_t i = 0; i < m.size(); ++i)
	{
		if(m[i].cls == 1) sw = !m[i].merged && m[i].item == 201;
		if(m[i].cls == 3) ar = m[i].merged && m[i].item == 203;
		if(m[i].cls == 2) banned = !m[i].merged;
		CHECK(m[i].cls != 4);
	}
	CHECK(sw && ar && banned);

	// plain text
	CHECK(SlPlain("+1 [ICON_GOLD] Gold.[NEWLINE]Units [COLOR_POSITIVE_TEXT]heal[ENDCOLOR]  fast.") == "+1 Gold. Units heal fast.");
	CHECK(SlPlain("") == "");

	CHECK(SlIsTypeName("LEADER_HARUN_AL_RASHID") && !SlIsTypeName("X'; DROP") && !SlIsTypeName(""));

	printf("%d/%d passed\n", g_run - g_failed, g_run);
	return g_failed ? 1 : 0;
}
