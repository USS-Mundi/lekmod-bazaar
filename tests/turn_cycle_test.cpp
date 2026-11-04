// Native tests for the turn cycle's game-free parts (no game needed): tests/run_turn_cycle_test.sh
#include "McpTurnCycle.h"
#include "picojson.h"
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

static int s_failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

namespace
{
	McpSeenUnit Unit(int id, const char* who, const char* type, int x, int y, int hp, bool enemy, int nearDist, const char* nearWhat)
	{
		McpSeenUnit u;
		u.id = id; u.who = who; u.type = type; u.x = x; u.y = y; u.hp = hp; u.enemy = enemy; u.nearDist = nearDist; u.nearWhat = nearWhat;
		return u;
	}

	McpSeenCity City(int id, const char* name, int hp, int maxHp)
	{
		McpSeenCity c;
		c.id = id; c.name = name; c.hp = hp; c.maxHp = maxHp;
		return c;
	}

	McpView View()
	{
		McpView v;
		v.gold = 130;
		v.happiness = 1;
		v.mine.push_back(Unit(10, "you", "Worker", 33, 30, 100, false, -1, ""));
		v.mine.push_back(Unit(11, "you", "Archer", 34, 30, 100, false, -1, ""));
		v.cities.push_back(City(1, "Edinburgh", 200, 200));
		v.theirs.push_back(Unit(50, "Georgia", "Warrior", 38, 23, 100, false, 6, "your Worker"));
		return v;
	}

	bool Has(const std::vector<std::string>& lines, const std::string& s)
	{
		for(size_t i = 0; i < lines.size(); ++i) if(lines[i] == s) return true;
		return false;
	}

	McpPlannedOrder Order(const char* tool, const char* args)
	{
		McpPlannedOrder o;
		o.tool = tool;
		o.args = args;
		return o;
	}

	void TestNothingChanged()
	{
		CHECK(McpViewChanges(View(), View(), 25).empty());
	}

	void TestNewEnemyInSightIsNamedWithItsDistance()
	{
		McpView now = View();
		now.theirs.push_back(Unit(60, "Barbarians", "Archer", 35, 30, 100, true, 2, "your Worker"));
		std::vector<std::string> c = McpViewChanges(View(), now, 25);
		CHECK(Has(c, "new in sight: enemy Barbarians Archer at (35,30), 2 tiles from your Worker"));
	}

	void TestNewEnemyAnywhereInSightIsShownEvenFarAway()
	{
		McpView now = View();
		now.theirs.push_back(Unit(61, "Barbarians", "Brute", 50, 10, 100, true, 18, "Edinburgh"));
		CHECK(Has(McpViewChanges(View(), now, 25), "new in sight: enemy Barbarians Brute at (50,10), 18 tiles from Edinburgh"));
	}

	void TestOthersMovingNearbyAreShownFarAwayAreNot()
	{
		McpView now = View();
		now.theirs[0].x = 39; now.theirs[0].nearDist = 1;
		CHECK(Has(McpViewChanges(View(), now, 25), "Georgia Warrior moved (38,23) to (39,23), 1 tile from your Worker"));
		McpView far = View(), farNow = View();
		far.theirs[0].nearDist = 12; farNow.theirs[0].nearDist = 11; farNow.theirs[0].x = 37;
		CHECK(McpViewChanges(far, farNow, 25).empty());
	}

	void TestEnemyMovingIsAlwaysShown()
	{
		McpView before = View(), now = View();
		before.theirs[0].enemy = now.theirs[0].enemy = true;
		before.theirs[0].nearDist = 12; now.theirs[0].nearDist = 11; now.theirs[0].x = 37;
		CHECK(Has(McpViewChanges(before, now, 25), "enemy Georgia Warrior moved (38,23) to (37,23), 11 tiles from your Worker"));
	}

	void TestYourLossesAndDamage()
	{
		McpView now = View();
		now.mine.erase(now.mine.begin());
		now.mine[0].hp = 70;
		now.cities[0].hp = 150;
		std::vector<std::string> c = McpViewChanges(View(), now, 25);
		CHECK(Has(c, "lost: your Worker (unit 10), last at (33,30)"));
		CHECK(Has(c, "your Archer at (34,30) took 30 damage (now 70)"));
		CHECK(Has(c, "Edinburgh took 50 damage (now 150 of 200)"));
	}

	void TestLostCity()
	{
		McpView now = View();
		now.cities.clear();
		CHECK(Has(McpViewChanges(View(), now, 25), "lost: Edinburgh"));
	}

	void TestWarAndPeace()
	{
		McpView before = View(), now = View();
		before.atWarWith.push_back("Rome");
		now.atWarWith.push_back("Turkey");
		std::vector<std::string> c = McpViewChanges(before, now, 25);
		CHECK(Has(c, "now at war with Turkey"));
		CHECK(Has(c, "now at peace with Rome"));
	}

	void TestNumbers()
	{
		McpView now = View();
		now.gold = 70;
		now.happiness = -2;
		std::vector<std::string> c = McpViewChanges(View(), now, 25);
		CHECK(Has(c, "gold 70 (was 130)"));
		CHECK(Has(c, "happiness -2 (was 1)"));
	}

	void TestSmallTicksAreLeftOut()
	{
		McpView now = View();
		now.gold = 118;
		now.happiness = 3;
		CHECK(McpViewChanges(View(), now, 25).empty());
		McpView empty = View();
		empty.gold = 0;
		CHECK(Has(McpViewChanges(View(), empty, 25), "gold 0 (was 130)"));
		McpView lower = View();
		lower.happiness = 5;
		McpView dropped = View();
		dropped.happiness = 1;
		CHECK(Has(McpViewChanges(lower, dropped, 25), "happiness 1 (was 5)"));
	}

	void TestCityStateUnitsPassingAreLeftOut()
	{
		McpView now = View();
		McpSeenUnit passing = Unit(60, "Zurich", "Pikeman", 34, 31, 100, false, 1, "your Archer");
		passing.cityState = true;
		now.theirs.push_back(passing);
		CHECK(McpViewChanges(View(), now, 25).empty());
		McpSeenUnit hostile = Unit(61, "Zurich", "Archer", 35, 31, 100, true, 1, "your Archer");
		hostile.cityState = true;
		now.theirs.push_back(hostile);
		CHECK(Has(McpViewChanges(View(), now, 25), "new in sight: enemy Zurich Archer at (35,31), 1 tile from your Archer"));
	}

	void TestMostUrgentFirstAndCapped()
	{
		McpView now = View();
		now.gold = 70;
		now.theirs.push_back(Unit(70, "Barbarians", "Spearman", 40, 40, 100, true, 9, "Edinburgh"));
		now.theirs.push_back(Unit(71, "Barbarians", "Archer", 35, 31, 100, true, 1, "your Archer"));
		now.atWarWith.push_back("Rome");
		std::vector<std::string> c = McpViewChanges(View(), now, 25);
		CHECK(c.size() == 4);
		if(c.size() == 4)
		{
			CHECK(c[0] == "now at war with Rome");
			CHECK(c[1] == "new in sight: enemy Barbarians Archer at (35,31), 1 tile from your Archer");
			CHECK(c[2] == "new in sight: enemy Barbarians Spearman at (40,40), 9 tiles from Edinburgh");
			CHECK(c[3] == "gold 70 (was 130)");
		}
		std::vector<std::string> two = McpViewChanges(View(), now, 2);
		CHECK(two.size() == 3 && two[2] == "...and 2 more");
	}

	void TestOrdersToRun()
	{
		std::vector<McpPlannedOrder> plan;
		plan.push_back(Order("move_unit", "{\"unit_id\":1,\"x\":2,\"y\":3}"));
		plan.push_back(Order("set_production", "{\"city_id\":8192,\"item\":\"UNIT_SCOUT\"}"));
		plan.push_back(Order("end_turn", "{}"));
		std::vector<int> drop;
		std::vector<McpPlannedOrder> add;
		std::string err;
		std::vector<McpPlannedOrder> run = McpOrdersToRun(plan, drop, add, err);
		CHECK(err.empty() && run.size() == 3);

		drop.push_back(1);
		add.push_back(Order("unit_action", "{\"unit_id\":1,\"action\":\"fortify\"}"));
		run = McpOrdersToRun(plan, drop, add, err);
		CHECK(err.empty() && run.size() == 3);
		if(run.size() == 3)
		{
			CHECK(run[0].tool == "set_production");
			CHECK(run[1].tool == "unit_action");
			CHECK(run[2].tool == "end_turn");
		}

		drop.clear();
		drop.push_back(4);
		run = McpOrdersToRun(plan, drop, add, err);
		CHECK(!err.empty() && run.empty());
	}

	void TestWhyKinds()
	{
		CHECK(McpIsWhyKind("enemy"));
		CHECK(McpIsWhyKind("mistake"));
		CHECK(!McpIsWhyKind("vibes"));
		CHECK(McpWhyKinds().find("opportunity") != std::string::npos);
	}
	std::map<std::string, std::string> Fields(const char* k1, const char* v1, const char* k2 = NULL, const char* v2 = NULL)
	{
		std::map<std::string, std::string> m;
		m[k1] = v1;
		if(k2) m[k2] = v2;
		return m;
	}

	void TestFirstPlanNeedsDirection()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		CHECK(!McpGamePlanApply(plan, Fields("cities", "Roma: settler"), "", 5, err, warn) && err.find("direction") != std::string::npos);
		err.clear();
		CHECK(McpGamePlanApply(plan, Fields("direction", "land first", "cities", "Roma: settler"), "", 5, err, warn));
		CHECK(plan.turn == 5 && plan.sections["direction"] == "land first" && plan.log.size() == 1 && plan.log[0] == "T5: first plan");
		CHECK(plan.written["direction"] == 5 && plan.written["cities"] == 5);
	}

	void TestChangesNeedWhyAndKeepTheRest()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		McpGamePlanApply(plan, Fields("direction", "land first", "research", "Pottery"), "", 5, err, warn);
		CHECK(!McpGamePlanApply(plan, Fields("research", "Bronze Working"), "", 9, err, warn) && err.find("why") != std::string::npos);
		err.clear();
		CHECK(McpGamePlanApply(plan, Fields("research", "Bronze Working"), "barbarians next door", 9, err, warn));
		CHECK(plan.sections["direction"] == "land first" && plan.sections["research"] == "Bronze Working");
		CHECK(plan.turn == 9 && plan.log.size() == 2 && plan.log[1] == "T9: barbarians next door");
		// Each heading keeps the turn it was last written; the one left alone keeps its own.
		CHECK(plan.written["research"] == 9 && plan.written["direction"] == 5);
		CHECK(McpGamePlanApply(plan, Fields("research", "Bronze Working"), "", 10, err, warn) && plan.log.size() == 2 && plan.turn == 9);
	}

	void TestUnknownHeadingAndLongPlan()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		CHECK(!McpGamePlanApply(plan, Fields("direction", "x", "arc", "y"), "", 1, err, warn) && err.find("arc") != std::string::npos);
		CHECK(err.find("relations") != std::string::npos);
		std::string longText;
		for(int i = 0; i < 710; ++i) longText += "word ";
		err.clear();
		CHECK(McpGamePlanApply(plan, Fields("direction", longText.c_str(), "policy", "t"), "", 1, err, warn));
		CHECK(warn.size() == 1 && warn[0].find("words") != std::string::npos);
	}

	void TestSevenHeadingsInOrder()
	{
		const std::vector<std::string>& h = McpGamePlanHeadings();
		const char* const want[] = { "direction", "cities", "military", "civilians", "research", "policy", "relations" };
		CHECK(h.size() == 7);
		for(size_t i = 0; i < h.size() && i < 7; ++i) CHECK(h[i] == want[i]);
	}

	void TestPlanTextShowsEveryHeadingWithItsTurn()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		McpGamePlanApply(plan, Fields("direction", "science", "cities", "Roma: library"), "", 40, err, warn);
		McpGamePlanApply(plan, Fields("research", "Philosophy"), "Writing landed", 44, err, warn);
		std::string text = McpGamePlanText(plan);
		CHECK(text.find("Direction (T40): science") != std::string::npos);
		CHECK(text.find("Research (T44): Philosophy") != std::string::npos);
		CHECK(text.find("Military: none written") != std::string::npos);
		CHECK(text.find("T44: Writing landed") != std::string::npos);
		CHECK(McpGamePlanLine(plan, "cities") == "plan (T40): Roma: library");
		CHECK(McpGamePlanLine(plan, "policy") == "plan: none written");
	}

	void TestWithoutAPlan()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		McpGamePlanApply(plan, Fields("direction", "wide", "cities", "Roma: settler. Milano: monument"), "", 3, err, warn);
		std::vector<std::string> cities;
		cities.push_back("Roma"); cities.push_back("Milano"); cities.push_back("Napoli");
		std::vector<std::string> gaps = McpWithoutAPlan(plan, cities);
		CHECK(Has(gaps, "Napoli in the cities plan"));
		CHECK(!Has(gaps, "Roma in the cities plan") && !Has(gaps, "Milano in the cities plan"));
		CHECK(Has(gaps, "military") && Has(gaps, "civilians") && Has(gaps, "research") && Has(gaps, "policy") && Has(gaps, "relations"));
		CHECK(!Has(gaps, "direction") && !Has(gaps, "cities"));
	}

	McpViewItem Item(const char* section, const char* key, const char* f1, const char* v1, const char* f2 = NULL, const char* v2 = NULL)
	{
		McpViewItem it;
		it.section = section;
		it.key = key;
		it.fields.push_back(std::make_pair(std::string(f1), std::string(v1)));
		if(f2) it.fields.push_back(std::make_pair(std::string(f2), std::string(v2)));
		return it;
	}

	const std::vector<std::string>& Section(const std::vector<McpViewSection>& view, const char* name)
	{
		static std::vector<std::string> none;
		for(size_t i = 0; i < view.size(); ++i) if(view[i].name == name) return view[i].lines;
		return none;
	}

	void TestFirstViewHasNoMarks()
	{
		std::vector<McpViewItem> before, now;
		now.push_back(Item("cities", "city:1", "name", "Roma", "population", "3"));
		std::vector<McpViewSection> view = McpMarkView(before, now, std::set<std::string>(), false);
		CHECK(view.size() == 1 && view[0].name == "cities");
		CHECK(Section(view, "cities").size() == 1 && Section(view, "cities")[0] == "  Roma · population 3");
	}

	void TestNewChangedSameAndGone()
	{
		std::vector<McpViewItem> before, now;
		before.push_back(Item("cities", "city:1", "name", "Roma", "population", "3"));
		before.push_back(Item("units", "unit:7", "type", "Warrior", "at", "(5,5)"));
		before.push_back(Item("units", "unit:8", "type", "Scout", "at", "(9,9)"));
		now.push_back(Item("cities", "city:1", "name", "Roma", "population", "4"));
		now.push_back(Item("cities", "city:2", "name", "Milano", "population", "1"));
		now.push_back(Item("units", "unit:7", "type", "Warrior", "at", "(5,5)"));
		std::vector<McpViewSection> view = McpMarkView(before, now, std::set<std::string>(), true);
		const std::vector<std::string>& cities = Section(view, "cities");
		CHECK(Has(cities, "~ Roma · population 4  (was: population 3)"));
		CHECK(Has(cities, "+ Milano · population 1"));
		const std::vector<std::string>& units = Section(view, "units");
		CHECK(Has(units, "  Warrior · at (5,5)"));
		CHECK(Has(units, "- Scout · at (9,9)"));
	}

	void TestTickingFieldsShowButDoNotMark()
	{
		std::vector<McpViewItem> before, now;
		before.push_back(Item("cities", "city:1", "name", "Roma", "food stored", "12/30"));
		now.push_back(Item("cities", "city:1", "name", "Roma", "food stored", "15/30"));
		std::set<std::string> ticking;
		ticking.insert("food stored");
		std::vector<McpViewSection> view = McpMarkView(before, now, ticking, true);
		CHECK(Has(Section(view, "cities"), "  Roma · food stored 15/30"));
	}

	void TestSectionsKeepTheirOrderAndGoneSectionsStay()
	{
		std::vector<McpViewItem> before, now;
		before.push_back(Item("seen", "seen:1", "type", "Brute", "at", "(3,3)"));
		now.push_back(Item("empire", "empire", "gold", "18"));
		now.push_back(Item("cities", "city:1", "name", "Roma"));
		std::vector<McpViewSection> view = McpMarkView(before, now, std::set<std::string>(), true);
		CHECK(view.size() == 3 && view[0].name == "empire" && view[1].name == "cities" && view[2].name == "seen");
		CHECK(Has(Section(view, "seen"), "- Brute · at (3,3)"));
	}

	void TestChangedListsShowOnlyWhatChanged()
	{
		std::vector<McpViewItem> before, now;
		before.push_back(Item("cities", "city:1", "name", "Caen", "worked", "[(12,33) wheat 2f 1p; (14,33) deer 1f 2p]"));
		now.push_back(Item("cities", "city:1", "name", "Caen", "worked", "[(12,33) wheat 2f 1p; (13,34) forest 1f 1p]"));
		std::vector<McpViewSection> view = McpMarkView(before, now, std::set<std::string>(), true);
		CHECK(Has(Section(view, "cities"), "~ Caen · worked [(12,33) wheat 2f 1p; (13,34) forest 1f 1p]  (was: worked: removed (14,33) deer 1f 2p; added (13,34) forest 1f 1p)"));
	}

	void TestOldPlanHeadingsMoveToTheNewOnes()
	{
		McpGamePlan plan;
		plan.turn = 12;
		plan.sections["read"] = "2 cities";
		plan.sections["arc"] = "science";
		plan.sections["thrust"] = "libraries";
		plan.sections["rivals"] = "Maurya: ahead";
		plan.sections["tripwires"] = "war";
		McpMigratePlan(plan);
		CHECK(plan.sections.count("arc") == 0 && plan.sections.count("rivals") == 0 && plan.sections.count("read") == 0);
		CHECK(plan.sections["direction"] == "Read: 2 cities. Arc: science. Thrust: libraries. Tripwires: war.");
		CHECK(plan.sections["relations"] == "Maurya: ahead");
		CHECK(plan.written["direction"] == 12 && plan.written["relations"] == 12);
		McpGamePlan fresh;
		fresh.sections["direction"] = "wide";
		McpMigratePlan(fresh);
		CHECK(fresh.sections.size() == 1 && fresh.sections["direction"] == "wide");
	}

	void TestTargetsAreReadFromPlanTurn()
	{
		picojson::value v;
		picojson::parse(v, "[{\"tech\":\"Iron Working\",\"by\":31},{\"city\":\"Bayeux\",\"size\":4,\"by\":32},"
			"{\"city\":\"Caen\",\"build\":\"Library\",\"by\":36},{\"gold\":150,\"by\":35},{\"policy\":\"next\",\"by\":25}]");
		std::vector<McpTarget> t;
		std::string err;
		CHECK(McpParseTargets(v, 23, t, err) && err.empty() && t.size() == 5);
		CHECK(t[0].kind == "tech" && t[0].subject == "Iron Working" && t[0].by == 31);
		CHECK(t[1].kind == "size" && t[1].city == "Bayeux" && t[1].amount == 4);
		CHECK(t[2].kind == "build" && t[2].city == "Caen" && t[2].subject == "Library");
		CHECK(t[3].kind == "gold" && t[3].amount == 150);
		CHECK(t[4].kind == "policy" && t[4].by == 25);
		CHECK(McpTargetLabel(t[0]) == "Iron Working by T31");
		CHECK(McpTargetLabel(t[1]) == "Bayeux size 4 by T32");
		CHECK(McpTargetLabel(t[2]) == "Caen Library by T36");
		CHECK(McpTargetLabel(t[3]) == "150 gold by T35");
		CHECK(McpTargetLabel(t[4]) == "next policy by T25");
	}

	void TestBadTargetsAreRefusedWithTheReason()
	{
		std::vector<McpTarget> t;
		std::string err;
		picojson::value v;
		picojson::parse(v, "[{\"tech\":\"Iron Working\"}]");
		CHECK(!McpParseTargets(v, 23, t, err) && err.find("by") != std::string::npos);
		picojson::parse(v, "[{\"tech\":\"Iron Working\",\"by\":20}]");
		err.clear();
		CHECK(!McpParseTargets(v, 23, t, err) && err.find("after") != std::string::npos);
		picojson::parse(v, "[{\"wonder\":\"Pyramids\",\"by\":40}]");
		err.clear();
		CHECK(!McpParseTargets(v, 23, t, err) && err.find("tech, build, size, gold or policy") != std::string::npos);
		picojson::parse(v, "[{\"size\":4,\"by\":40}]");
		err.clear();
		CHECK(!McpParseTargets(v, 23, t, err) && err.find("city") != std::string::npos);
		picojson::parse(v, "[]");
		err.clear();
		CHECK(McpParseTargets(v, 23, t, err) && t.empty());
	}

	void TestAClockIsMarkedOnlyWhenItsLandingMoves()
	{
		std::vector<McpViewItem> before, now;
		std::set<std::string> ticking;
		ticking.insert("clock");
		before.push_back(Item("cities", "city:1:clock:Swordsman", "name", "Caen: Swordsman", "clock", "15/37 at 12/t: 2t"));
		before.back().fields.push_back(std::make_pair(std::string("lands"), std::string("T49")));
		now.push_back(Item("cities", "city:1:clock:Swordsman", "name", "Caen: Swordsman", "clock", "27/37 at 12/t: 1t"));
		now.back().fields.push_back(std::make_pair(std::string("lands"), std::string("T49")));
		CHECK(Has(Section(McpMarkView(before, now, ticking, true), "cities"), "  Caen: Swordsman · clock 27/37 at 12/t: 1t · lands T49"));
		now.back().fields.back().second = "T51";
		CHECK(Has(Section(McpMarkView(before, now, ticking, true), "cities"), "~ Caen: Swordsman · clock 27/37 at 12/t: 1t · lands T51  (was: lands T49)"));
	}

	void TestTurnsToAfford()
	{
		CHECK(McpTurnsToAfford(300, 320, 10) == 0);
		CHECK(McpTurnsToAfford(390, 302, 49) == 2);
		CHECK(McpTurnsToAfford(390, 351, 49) == 1);
		CHECK(McpTurnsToAfford(390, 302, 0) == -1);
		CHECK(McpTurnsToAfford(390, 302, -5) == -1);
	}

	void TestPlanCountsFromTheTurnTheSeatEnded()
	{
		// Planning during its own turn 12: the plan is for turn 13.
		CHECK(McpPlanMadeOnTurn(12, true, 11) == 12);
		// Ended turn 12 and planning while the others still play turn 12.
		CHECK(McpPlanMadeOnTurn(12, false, 12) == 12);
		// Last in the turn order: the game moved on to turn 13 the moment it ended turn 12, before it planned.
		CHECK(McpPlanMadeOnTurn(13, false, 12) == 12);
		// No turn ended since the game loaded: the game turn is all there is.
		CHECK(McpPlanMadeOnTurn(13, false, -1) == 13);
	}

	void TestOnePictureWhenTheCitiesFit()
	{
		// Two candidate centers, three cities: the second center has every city within reach 4.
		std::vector<std::vector<int> > dist(2);
		int a[] = { 0, 5, 7 }, b[] = { 3, 2, 4 };
		dist[0].assign(a, a + 3);
		dist[1].assign(b, b + 3);
		std::vector<McpPicture> pics = McpPlanPictures(dist, 4, 3);
		CHECK(pics.size() == 1);
		CHECK(pics[0].center == 1 && pics[0].cities.size() == 3 && pics[0].farthest == 4);
	}

	void TestSpreadCitiesTakeMorePicturesUpToTheLimit()
	{
		// Each center reaches only its own city: three cities need three pictures, a limit of two leaves one out.
		std::vector<std::vector<int> > dist(3);
		int a[] = { 0, 9, 9 }, b[] = { 9, 1, 9 }, c[] = { 9, 9, 2 };
		dist[0].assign(a, a + 3);
		dist[1].assign(b, b + 3);
		dist[2].assign(c, c + 3);
		CHECK(McpPlanPictures(dist, 4, 3).size() == 3);
		std::vector<McpPicture> two = McpPlanPictures(dist, 4, 2);
		CHECK(two.size() == 2 && two[0].center == 0 && two[1].center == 1);
	}

	void TestTiesGoToTheTighterPicture()
	{
		// Both centers reach both cities; the one whose farthest city is nearer wins.
		std::vector<std::vector<int> > dist(2);
		int a[] = { 4, 4 }, b[] = { 2, 1 };
		dist[0].assign(a, a + 2);
		dist[1].assign(b, b + 2);
		std::vector<McpPicture> pics = McpPlanPictures(dist, 4, 3);
		CHECK(pics.size() == 1 && pics[0].center == 1 && pics[0].farthest == 2);
	}

	void TestNoCitiesNoPictures()
	{
		std::vector<std::vector<int> > dist(1);
		CHECK(McpPlanPictures(dist, 4, 3).empty());
	}
}

namespace
{
	void TestEndTurnWaitsUntilTheGameHasRunTheQueuedMoves()
	{
		CHECK(McpEndTurnOutcome(false, false) == MCP_TURN_ENDING);
		CHECK(McpEndTurnOutcome(true, false) == MCP_TURN_ENDED);
		CHECK(McpEndTurnOutcome(false, true) == MCP_TURN_HANDED_BACK);
		// A turn that passed counts as ended even if the next one has already come round.
		CHECK(McpEndTurnOutcome(true, true) == MCP_TURN_ENDED);
	}
}

namespace
{
	McpViewItem HpItem(const char* section, const char* key, const char* hp)
	{
		McpViewItem it;
		it.section = section;
		it.key = key;
		it.fields.push_back(std::make_pair(std::string("name"), std::string(key)));
		if(hp) it.fields.push_back(std::make_pair(std::string("hp"), std::string(hp)));
		return it;
	}

	std::string FieldOf(const McpViewItem& it, const char* name)
	{
		for(size_t i = 0; i < it.fields.size(); ++i) if(it.fields[i].first == name) return it.fields[i].second;
		return "";
	}

	void TestAFallingCityGetsAClock()
	{
		std::vector<McpViewItem> before, now;
		before.push_back(HpItem("cities", "Rouen", "182"));
		before.push_back(HpItem("known cities: cities", "Pataliputra", "300"));
		before.push_back(HpItem("cities", "Caen", "225"));
		before.push_back(HpItem("military units", "Warrior", "100"));
		before.push_back(HpItem("known cities: cities", "Sfax", "200"));
		now.push_back(HpItem("cities", "Rouen", "107"));
		now.push_back(HpItem("known cities: cities", "Pataliputra", "40"));
		now.push_back(HpItem("cities", "Caen", "225"));
		now.push_back(HpItem("military units", "Warrior", "30"));
		now.push_back(HpItem("known cities: cities", "Sfax", NULL));
		now.push_back(HpItem("cities", "Bayeux", "150"));
		McpAddFallClocks(before, now);
		CHECK(FieldOf(now[0], "falling") == "-75 since your last turn: 0 in 2t at this rate; at 0 a melee unit can take it");
		CHECK(FieldOf(now[1], "falling") == "-260 since your last turn: 0 in 1t at this rate; at 0 a melee unit can take it");
		CHECK(FieldOf(now[2], "falling") == "");
		CHECK(FieldOf(now[3], "falling") == "");
		CHECK(FieldOf(now[4], "falling") == "");
		CHECK(FieldOf(now[5], "falling") == "");

		std::vector<McpViewItem> b2, n2;
		b2.push_back(HpItem("cities", "Rouen", "40"));
		n2.push_back(HpItem("cities", "Rouen", "0"));
		McpAddFallClocks(b2, n2);
		CHECK(FieldOf(n2[0], "falling") == "-40 since your last turn: at 0, a melee unit can take it now");
	}
}

int main()
{
	TestNothingChanged();
	TestNewEnemyInSightIsNamedWithItsDistance();
	TestNewEnemyAnywhereInSightIsShownEvenFarAway();
	TestOthersMovingNearbyAreShownFarAwayAreNot();
	TestEnemyMovingIsAlwaysShown();
	TestYourLossesAndDamage();
	TestLostCity();
	TestWarAndPeace();
	TestNumbers();
	TestSmallTicksAreLeftOut();
	TestCityStateUnitsPassingAreLeftOut();
	TestMostUrgentFirstAndCapped();
	TestOrdersToRun();
	TestWhyKinds();
	TestFirstPlanNeedsDirection();
	TestChangesNeedWhyAndKeepTheRest();
	TestUnknownHeadingAndLongPlan();
	TestSevenHeadingsInOrder();
	TestPlanTextShowsEveryHeadingWithItsTurn();
	TestWithoutAPlan();
	TestFirstViewHasNoMarks();
	TestNewChangedSameAndGone();
	TestTickingFieldsShowButDoNotMark();
	TestSectionsKeepTheirOrderAndGoneSectionsStay();
	TestChangedListsShowOnlyWhatChanged();
	TestOldPlanHeadingsMoveToTheNewOnes();
	TestTargetsAreReadFromPlanTurn();
	TestBadTargetsAreRefusedWithTheReason();
	TestAClockIsMarkedOnlyWhenItsLandingMoves();
	TestTurnsToAfford();
	TestPlanCountsFromTheTurnTheSeatEnded();
	TestOnePictureWhenTheCitiesFit();
	TestSpreadCitiesTakeMorePicturesUpToTheLimit();
	TestTiesGoToTheTighterPicture();
	TestNoCitiesNoPictures();
	TestEndTurnWaitsUntilTheGameHasRunTheQueuedMoves();
	TestAFallingCityGetsAClock();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all turn cycle tests passed\n");
	return 0;
}
