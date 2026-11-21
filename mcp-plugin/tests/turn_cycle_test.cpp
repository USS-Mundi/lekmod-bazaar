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
		for(int i = 0; i < 1210; ++i) longText += "word ";
		err.clear();
		CHECK(McpGamePlanApply(plan, Fields("direction", longText.c_str(), "policy", "t"), "", 1, err, warn));
		CHECK(warn.size() == 1 && warn[0].find("words") != std::string::npos);
	}

	void TestHeadingsInOrder()
	{
		// Where you stand, then how you win, then what has to be true over the next turns, then each area's part.
		const std::vector<std::string>& h = McpGamePlanHeadings();
		const char* const want[] = { "position", "direction", "next_turns", "cities", "military", "civilians", "research", "policy", "relations" };
		CHECK(h.size() == 9);
		for(size_t i = 0; i < h.size() && i < 9; ++i) CHECK(h[i] == want[i]);
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


	void TestPlanBlockShowsEveryPartWithItsAge()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		McpGamePlanApply(plan, Fields("direction", "science", "cities", "Roma: library"), "", 42, err, warn);
		McpGamePlanApply(plan, Fields("research", "Philosophy"), "Writing landed", 47, err, warn);
		std::string block = McpGamePlanBlock(plan, 50);
		CHECK(block.find("  direction (T42, 8 turns ago): science\n") != std::string::npos);
		CHECK(block.find("  research (T47, 3 turns ago): Philosophy\n") != std::string::npos);
		CHECK(block.find("  military: none written\n") != std::string::npos);
		CHECK(McpLastPlanTurn(plan) == 47);
		McpGamePlanApply(plan, Fields("policy", "Piety"), "religion founded", 50, err, warn);
		CHECK(McpGamePlanBlock(plan, 50).find("  policy (T50, this turn): Piety\n") != std::string::npos);
		CHECK(McpLastPlanTurn(McpGamePlan()) == -1);
	}

	void TestFramingAsksWithoutPickingASide()
	{
		std::string top = McpTurnFramingTop(47);
		CHECK(top.find("turn 47") != std::string::npos);
		CHECK(top.find("hold") != std::string::npos && top.find("adjust") != std::string::npos && top.find("drop") != std::string::npos);
		CHECK(top.find("not everything") != std::string::npos && top.find("nothing in it has to be done") != std::string::npos);
		CHECK(top.find("top to bottom") != std::string::npos && top.find("follow") != std::string::npos);   // the layers agree
		CHECK(McpTurnFramingTop(-1).find("no plan yet") != std::string::npos);
		CHECK(!McpTurnFramingBottom().empty());
	}

	void TestOnlyRealBlockersBlock()
	{
		CHECK(McpDecisionKind("end_turn is blocked - production: a city needs a production choice") == MCP_DECISION_BLOCKS);
		CHECK(McpDecisionKind("production: Pliska (id 8192) is building nothing (city_options, set_production)") == MCP_DECISION_BLOCKS);
		CHECK(McpDecisionKind("research: nothing being researched (research_options, set_research)") == MCP_DECISION_BLOCKS);
		CHECK(McpDecisionKind("free_tech: 1 free technology to claim (research_options, set_research)") == MCP_DECISION_BLOCKS);
		CHECK(McpDecisionKind("captured city: decide the fate of Cordoba (id 3) with city_task: annex") == MCP_DECISION_BLOCKS);
		CHECK(McpDecisionKind("diplomacy: 1 request(s) from other leaders await your answer") == MCP_DECISION_FROM_OTHERS);
		CHECK(McpDecisionKind("promotion: Warrior (id 1) can be promoted (unit_options, promote_unit)") == MCP_DECISION_AVAILABLE);
		CHECK(McpDecisionKind("upgrade: Archer (id 2) can upgrade to Composite Bowman for 40 gold (upgrade_unit)") == MCP_DECISION_AVAILABLE);
		CHECK(McpDecisionKind("city_attack: Preslav can bombard an enemy this turn") == MCP_DECISION_AVAILABLE);
		CHECK(McpDecisionKind("policy: you can adopt a social policy now (policy_options, adopt_policy)") == MCP_DECISION_AVAILABLE);
	}

	void TestNewsDropsWhatTheyAlreadyKnow()
	{
		CHECK(McpTrimNews("id 48 · text Researched Construction: Allows you to build the Composite Bowman, a powerful ranged unit. · turn 49")
			== "id 48 · text Researched Construction · turn 49");
		CHECK(McpTrimNews("at (38,19) · summary An Enemy is Near! · text An enemy unit has been spotted in our territory! · turn 49")
			== "at (38,19) · summary An Enemy is Near! · text An enemy unit has been spotted in our territory! · turn 49");
		CHECK(McpTrimNews("text Researched Writing: Allows the Library.") == "text Researched Writing");
	}


	void TestCivCodesAreUniqueAndStable()
	{
		std::vector<std::string> names;
		names.push_back("Indonesia"); names.push_back("India"); names.push_back("Hong Kong"); names.push_back("Inca"); names.push_back("Ind");
		std::vector<std::string> codes = McpCivCodes(names);
		CHECK(codes.size() == 5);
		CHECK(codes[0] == "IND");
		CHECK(codes[1] == "INI");
		CHECK(codes[2] == "HON");
		CHECK(codes[3] == "INC");
		CHECK(codes[4] == "IN2");
		std::set<std::string> unique(codes.begin(), codes.end());
		CHECK(unique.size() == codes.size());
		CHECK(McpCivCodes(names) == codes);
	}

	void TestUnitStatsInBrackets()
	{
		CHECK(McpUnitStats(100, 15, 0, 0, "2", -1) == "[100 15 2]");
		CHECK(McpUnitStats(7, 4, 7, 2, "2", -1) == "[7 4 r7/2 2]");
		CHECK(McpUnitStats(79, 8, 0, 0, "2/2", 1) == "[79 8 2/2 L1]");
		CHECK(McpUnitStats(100, 0, 0, 0, "2", -1) == "[100 2]");
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
		CHECK(Has(cities, "~ Roma · population 4 (was 3)"));
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
		CHECK(Has(Section(view, "cities"), "~ Caen · worked [(12,33) wheat 2f 1p; (13,34) forest 1f 1p] (was: removed (14,33) deer 1f 2p; added (13,34) forest 1f 1p)"));
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
		CHECK(Has(Section(McpMarkView(before, now, ticking, true), "cities"), "  Caen: Swordsman · 27/37 at 12/t: 1t · lands T49"));
		now.back().fields.back().second = "T51";
		CHECK(Has(Section(McpMarkView(before, now, ticking, true), "cities"), "~ Caen: Swordsman · 27/37 at 12/t: 1t · lands T51 (was T49)"));
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

	void TestARoundOpensOnceTheGameTurnPassesTheSeatsLastEnd()
	{
		// Ended turn 12 and the others still play turn 12: nothing to plan from yet.
		CHECK(!McpRoundOpen(12, 12));
		// The barbarians have moved and the game is on turn 13.
		CHECK(McpRoundOpen(13, 12));
		// No turn ended since the game loaded.
		CHECK(McpRoundOpen(13, -1));
	}

	void TestOnlyTheTurnsOwnDriftDoesNotWake()
	{
		McpView now = View();
		now.gold = 70; now.happiness = 4;
		CHECK(McpViewChanges(View(), now, 25).size() == 2);
		CHECK(McpWakingChanges(View(), now, 25).empty());
	}

	void TestWhatAnotherPlayerDidInSightWakes()
	{
		// A neighbour at peace walks up, the treasury runs out, happiness falls below zero.
		McpView now = View();
		now.theirs[0].x = 39; now.theirs[0].nearDist = 1;
		now.gold = 0; now.happiness = -2;
		std::vector<std::string> c = McpWakingChanges(View(), now, 25);
		CHECK(Has(c, "Georgia Warrior moved (38,23) to (39,23), 1 tile from your Worker"));
		CHECK(Has(c, "gold 0 (was 130)"));
		CHECK(Has(c, "happiness -2 (was 1)"));
		CHECK(c.size() == 3);
	}

	void TestThreatsWake()
	{
		McpView before = View(), now = View();
		before.atWarWith.push_back("Rome");
		now.atWarWith.push_back("Turkey");
		now.mine.erase(now.mine.begin());
		now.mine[0].hp = 70;
		now.cities[0].hp = 150;
		now.theirs.push_back(Unit(60, "Barbarians", "Brute", 50, 10, 100, true, 18, "Edinburgh"));
		std::vector<std::string> c = McpWakingChanges(before, now, 25);
		CHECK(Has(c, "now at war with Turkey"));
		CHECK(Has(c, "now at peace with Rome"));
		CHECK(Has(c, "lost: your Worker (unit 10), last at (33,30)"));
		CHECK(Has(c, "your Archer at (34,30) took 30 damage (now 70)"));
		CHECK(Has(c, "Edinburgh took 50 damage (now 150 of 200)"));
		CHECK(Has(c, "new in sight: enemy Barbarians Brute at (50,10), 18 tiles from Edinburgh"));
		CHECK(c.size() == 6);
	}

	void TestAPlannedTurnRunsUnlessSomethingHoldsIt()
	{
		std::vector<std::string> none, seen;
		seen.push_back("your Archer at (34,30) took 30 damage (now 70)");
		seen.push_back("now at war with Turkey");
		CHECK(McpAutoRunHold(false, true, none, 0).empty());
		CHECK(McpAutoRunHold(true, true, none, 0) == "you check it against what changed, then run_plan");
		CHECK(McpAutoRunHold(false, false, none, 0) == "the plan was not made from this round's turn view");
		CHECK(McpAutoRunHold(false, true, none, 1) == "another leader is waiting for your answer");
		CHECK(McpAutoRunHold(false, true, seen, 0) == "since you planned: your Archer at (34,30) took 30 damage (now 70), and 1 more");
		seen.resize(1);
		CHECK(McpAutoRunHold(false, true, seen, 0) == "since you planned: your Archer at (34,30) took 30 damage (now 70)");
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

	void TestAHandedBackTurnIsEndedAgainOnceWhenNothingAsksForTheSeat()
	{
		CHECK(McpEndTurnAgain(false, 0, 0));
		// A unit that needs orders, a choice to make or a message to read is the seat's to see.
		CHECK(!McpEndTurnAgain(true, 0, 0));
		CHECK(!McpEndTurnAgain(false, 1, 0));
		// Once: a turn that comes back a second time goes to the seat.
		CHECK(!McpEndTurnAgain(false, 0, 1));
	}

	void TestAPlansEndOfTurnWaitsOnAFailedOrderOrANewMessage()
	{
		CHECK(McpPlanEndHeld(false, 0).empty());
		CHECK(McpPlanEndHeld(true, 0) == "not run: an order before it failed, so the turn is still yours");
		CHECK(McpPlanEndHeld(false, 2) == "not run: 2 game message(s) came while your orders ran (messages_while_running), so the turn is still yours");
		// The failed order is the first thing to deal with.
		CHECK(McpPlanEndHeld(true, 2) == McpPlanEndHeld(true, 0));
	}

	void TestAnOrderForWhatIsAlreadySoCountsAsDone()
	{
		CHECK(McpOrderAlreadyDone("unit_action", MCP_ALREADY_RESTING));
		CHECK(McpOrderAlreadyDone("set_production", std::string("this city cannot build BUILDING_MONUMENT: ") + MCP_CITY_HAS_IT));
		CHECK(McpOrderAlreadyDone("buy", std::string("cannot buy that here now: the city cannot build it: ") + MCP_CITY_HAS_IT));
		CHECK(McpOrderAlreadyDone("set_production", std::string("this city cannot build BUILDING_GREAT_LIBRARY: ") + MCP_TEAM_HAS_IT));
		CHECK(McpOrderAlreadyDone("set_research", std::string(MCP_KNOWN_TECH) + "Bronze Working"));
		CHECK(McpOrderAlreadyDone("adopt_policy", MCP_HAVE_POLICY));
		// A unit with no moves left can do nothing more this turn, whatever is decided; it is skipped, not "already so".
		// (move_unit queues its route instead of failing, so a move is never skipped.)
		CHECK(McpOrderAlreadyDone("unit_action", MCP_NO_MOVES_LEFT));
		CHECK(!McpOrderAlreadyDone("move_unit", MCP_NO_MOVES_LEFT));
		CHECK(McpOrderDoneNote(MCP_NO_MOVES_LEFT) == "skipped, no moves left this turn");
		CHECK(McpOrderDoneNote(MCP_ALREADY_RESTING) == "already so");
		// An attack that cannot be made is a choice still open: another unit may make it.
		CHECK(!McpOrderAlreadyDone("attack", MCP_NO_MOVES_LEFT));
		CHECK(!McpOrderAlreadyDone("city_attack", MCP_NO_MOVES_LEFT));
		// Anything else that fails is the seat's to look at.
		CHECK(!McpOrderAlreadyDone("move_unit", "path step 2 (4,5) cannot be entered: a unit of yours stands there; the rest of the path is dropped"));
		CHECK(!McpOrderAlreadyDone("set_production", "this city cannot build BUILDING_TEMPLE: needs Shrine in this city first"));
		CHECK(!McpOrderAlreadyDone("unit_action", "cannot do that build on this tile (wrong unit, tech missing, or terrain)"));
		CHECK(!McpOrderAlreadyDone("end_turn", MCP_ALREADY_RESTING));
	}

	void TestABuildTheUnitCannotStartHereWaitsForItsQueuedMove()
	{
		CHECK(McpChooseBuildStep(true, false, false) == MCP_BUILD_HERE);
		// Where it stands wins, as the order says; its queued move is replaced.
		CHECK(McpChooseBuildStep(true, true, true) == MCP_BUILD_HERE);
		// A worker that fell short of the tile builds there when it arrives.
		CHECK(McpChooseBuildStep(false, true, true) == MCP_BUILD_ON_ARRIVAL);
		CHECK(McpChooseBuildStep(false, true, false) == MCP_BUILD_REFUSED);
		CHECK(McpChooseBuildStep(false, false, false) == MCP_BUILD_REFUSED);
	}

	void TestAQueueItemMovesByNeighbourSwaps()
	{
		CHECK(McpQueueMoveSwaps(2, 2).empty());
		// Item 3 (index 2) to the front: swap 1-2, then 0-1.
		std::vector<int> up = McpQueueMoveSwaps(2, 0);
		CHECK(up.size() == 2 && up[0] == 1 && up[1] == 0);
		std::vector<int> down = McpQueueMoveSwaps(0, 2);
		CHECK(down.size() == 2 && down[0] == 0 && down[1] == 1);
	}

	void TestQueuePositionsCountFromOne()
	{
		CHECK(McpQueuePositionFault(1, 3, false).empty());
		CHECK(McpQueuePositionFault(3, 3, false).empty());
		CHECK(McpQueuePositionFault(4, 3, false) == "the queue has 3 items: give a position from 1 to 3");
		CHECK(McpQueuePositionFault(0, 3, false) == "the queue has 3 items: give a position from 1 to 3");
		// An item can go in after the last one.
		CHECK(McpQueuePositionFault(4, 3, true).empty());
		CHECK(McpQueuePositionFault(5, 3, true) == "the queue has 3 items: give a position from 1 to 4");
		CHECK(McpQueuePositionFault(1, 0, false) == "the queue is empty");
		CHECK(McpQueuePositionFault(1, 0, true).empty());
	}

	McpOpeningCity OpeningCity(int id, const char* name, bool finishes, bool unit, const char* item, bool after)
	{
		McpOpeningCity c;
		c.id = id; c.name = name; c.finishes = finishes; c.unit = unit; c.item = item; c.queuedAfter = after;
		return c;
	}

	void TestAPlanAnswersWhatTheTurnOpeningBrings()
	{
		McpOpening o;
		o.idle.push_back(std::make_pair(7, std::string("Warrior 7 at (3,4) has no order")));
		o.cities.push_back(OpeningCity(1, "Rome", true, true, "Settler", false));
		o.cities.push_back(OpeningCity(2, "Antium", true, false, "Library", true));
		o.cities.push_back(OpeningCity(3, "Cumae", false, false, "Granary", false));
		McpOpeningCity idle = OpeningCity(4, "Neapolis", false, false, "", false);
		idle.nothing = true;
		o.cities.push_back(idle);
		o.researchFinishes = true;
		o.research = "Writing";
		McpPlanCovers none;
		std::vector<std::string> owed = McpOpeningOwed(o, none, false);
		CHECK(owed.size() == 5);
		CHECK(Has(owed, "Warrior 7 at (3,4) has no order"));
		CHECK(Has(owed, "Rome finishes Settler as the turn opens: give the new unit its orders in this plan, with from_city 1 in place of unit_id"));
		CHECK(Has(owed, "Rome finishes Settler as the turn opens, with nothing queued after it: queue what comes next"));
		CHECK(Has(owed, "Neapolis has nothing to build"));
		CHECK(Has(owed, "research: Writing finishes as the turn opens, with nothing queued after it: set what comes next"));
		// What the plan orders is answered; a queued item after the one finishing already answers the city.
		McpPlanCovers all;
		all.units.insert(7);
		all.newUnits.insert(1);
		all.cities.insert(1);
		all.cities.insert(4);
		all.research = true;
		CHECK(McpOpeningOwed(o, all, false).empty());
		// With the turn open only what stops it from ending is owed: the opening has already happened.
		std::vector<std::string> open = McpOpeningOwed(o, none, true);
		CHECK(open.size() == 2 && Has(open, "Warrior 7 at (3,4) has no order") && Has(open, "Neapolis has nothing to build"));
		McpOpening r;
		r.researchNone = true;
		CHECK(McpOpeningOwed(r, none, true).size() == 1 && McpOpeningOwed(r, none, true)[0] == "research: nothing chosen");
		// A city converting its production (a process) never finishes.
		McpOpening w;
		McpOpeningCity wealth = OpeningCity(5, "Ostia", true, false, "Wealth", false);
		wealth.process = true;
		w.cities.push_back(wealth);
		CHECK(McpOpeningOwed(w, none, false).empty());
	}

	void TestAnOrderForANewUnitNamesWhichOneWhenACityMakesSeveral()
	{
		int type = -1;
		std::vector<int> made;
		CHECK(McpNewUnitType(made, -1, type) == MCP_NEW_UNIT_NONE);
		made.push_back(7);
		CHECK(McpNewUnitType(made, -1, type) == MCP_NEW_UNIT_OK && type == 7);
		CHECK(McpNewUnitType(made, 9, type) == MCP_NEW_UNIT_NOT_MADE);
		// A Warrior finished and a Settler bought: the order says which.
		made.push_back(9);
		CHECK(McpNewUnitType(made, -1, type) == MCP_NEW_UNIT_WHICH);
		CHECK(McpNewUnitType(made, 9, type) == MCP_NEW_UNIT_OK && type == 9);
		// Two of one kind are one kind: nth tells them apart.
		std::vector<int> two(2, 7);
		CHECK(McpNewUnitType(two, -1, type) == MCP_NEW_UNIT_OK && type == 7);
	}

	void TestTheNthNewUnitIsInTheCityFirstThenNearestThenLowestId()
	{
		std::vector<std::pair<int, int> > c;
		CHECK(McpPickNewUnit(c, 1) == -1);
		c.push_back(std::make_pair(1, 40));
		c.push_back(std::make_pair(0, 55));
		c.push_back(std::make_pair(0, 50));
		CHECK(McpPickNewUnit(c, 1) == 2);   // in the city, lowest id
		CHECK(McpPickNewUnit(c, 2) == 1);
		CHECK(McpPickNewUnit(c, 3) == 0);
		CHECK(McpPickNewUnit(c, 4) == -1);
		CHECK(McpPickNewUnit(c, 0) == -1);
	}

	void TestAnOrderForAUnitNotYetBuiltWaitsIfNoneCame()
	{
		CHECK(McpOrderAlreadyDone("move_unit", std::string("Rome ") + MCP_NO_NEW_UNIT));
	}

	void TestOnlyAPathThatCanNeverBeWalkedIsRefused()
	{
		CHECK(!McpPathFaultRefuses(MCP_PATH_OK));
		CHECK(McpPathFaultRefuses(MCP_PATH_MISSING));
		CHECK(McpPathFaultRefuses(MCP_PATH_OFF_MAP));
		CHECK(McpPathFaultRefuses(MCP_PATH_NOT_ADJACENT));
		CHECK(McpPathFaultRefuses(MCP_PATH_MOUNTAIN));
		// An earlier order may reveal the tile before the move runs.
		CHECK(!McpPathFaultRefuses(MCP_PATH_UNEXPLORED));
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
		before.push_back(HpItem("their cities", "Pataliputra", "300"));
		before.push_back(HpItem("cities", "Caen", "225"));
		before.push_back(HpItem("military units", "Warrior", "100"));
		before.push_back(HpItem("their cities", "Sfax", "200"));
		now.push_back(HpItem("cities", "Rouen", "107"));
		now.push_back(HpItem("their cities", "Pataliputra", "40"));
		now.push_back(HpItem("cities", "Caen", "225"));
		now.push_back(HpItem("military units", "Warrior", "30"));
		now.push_back(HpItem("their cities", "Sfax", NULL));
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

namespace
{
	void TestUnlabeledFieldsAndNewAndGoneFields()
	{
		std::vector<McpViewItem> before, now;
		before.push_back(Item("units", "unit:7", "name", "Warrior 7", "_at", "(5,5)"));
		before.back().fields.push_back(std::make_pair(std::string("embarked"), std::string("yes")));
		now.push_back(Item("units", "unit:7", "name", "Warrior 7", "_at", "(6,5)"));
		now.back().fields.push_back(std::make_pair(std::string("pact"), std::string("yes")));
		std::vector<McpViewSection> view = McpMarkView(before, now, std::set<std::string>(), true);
		CHECK(Has(Section(view, "units"), "~ Warrior 7 · (6,5) (was (5,5)) · pact yes (new)  (gone: embarked yes)"));
		CHECK(Has(Section(McpMarkView(before, before, std::set<std::string>(), true), "units"), "  Warrior 7 · (5,5) · embarked yes"));
	}
}

namespace
{
	// move_unit's and unit_action's schemas as McpTools.cpp declares them; a tool without unit_id beside them.
	const char* MOVE_UNIT = "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"path\":{\"type\":\"array\",\"items\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}},\"preview_only\":{\"type\":\"boolean\"}},\"required\":[\"unit_id\",\"path\"]}";
	const char* UNIT_ACTION = "{\"type\":\"object\",\"properties\":{\"unit_id\":{\"type\":\"integer\"},\"action\":{\"type\":\"string\"},\"build\":{\"type\":\"string\"},\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"}},\"required\":[\"unit_id\",\"action\"]}";
	const char* NO_UNIT = "{\"type\":\"object\",\"properties\":{\"tech\":{\"type\":\"string\"}},\"required\":[\"tech\"]}";

	picojson::object Args(const char* json)
	{
		picojson::value v;
		picojson::parse(v, std::string(json));
		return v.get<picojson::object>();
	}
}

void TestAUnitToolTakesFromCityInPlaceOfUnitId()
{
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"from_city\":8192,\"path\":[[20,29],[20,30]]}")) == "");
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"from_city\":8192,\"unit\":\"UNIT_WARRIOR\",\"nth\":2,\"_unit_type\":7,\"path\":[[20,29]]}")) == "");
	CHECK(McpArgsFault("unit_action", UNIT_ACTION, Args("{\"from_city\":8192,\"action\":\"fortify\"}")) == "");
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"from_city\":\"Moscow\",\"path\":[[20,29]]}")) == "argument 'from_city' for move_unit must be a whole number");
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"from_city\":8192,\"nth\":\"second\",\"path\":[[20,29]]}")) == "argument 'nth' for move_unit must be a whole number");
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"from_city\":8192}")) == "missing required argument 'path' for move_unit");
}

void TestArgumentsOutsideTheSchemaAreStillRefused()
{
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"unit_id\":5,\"path\":[[20,29]]}")) == "");
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"path\":[[20,29]]}")) == "missing required argument 'unit_id' for move_unit");
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"unit_id\":5,\"nth\":2,\"path\":[[20,29]]}")) == "unknown argument 'nth' for move_unit; it accepts path, preview_only, unit_id");
	CHECK(McpArgsFault("set_research", NO_UNIT, Args("{\"from_city\":8192,\"tech\":\"TECH_POTTERY\"}")) == "unknown argument 'from_city' for set_research; it accepts tech");
	CHECK(McpArgsFault("move_unit", MOVE_UNIT, Args("{\"unit_id\":5.5,\"path\":[[20,29]]}")) == "argument 'unit_id' for move_unit must be a whole number");
	CHECK(McpArgsFault("end_turn", "{\"type\":\"object\",\"properties\":{}}", Args("{\"x\":1}")) == "unknown argument 'x' for end_turn; it accepts no arguments");
	CHECK(McpArgsFault("move_unit", "{broken", Args("{}")) == "the argument schema for move_unit is broken; this is a bug in the mod");
}

McpNewUnitBinding Binding(int order, int unit, const char* text, const char* city)
{
	McpNewUnitBinding b;
	b.order = order; b.unit = unit; b.unitText = text; b.city = city;
	return b;
}

void TestAtTurnOpenAFromCityOrderCoversTheUnitItWasBoundTo()
{
	McpOpening o;
	o.idle.push_back(std::make_pair(81924, std::string("Settler 81924 at (47,34) has no order")));
	o.idle.push_back(std::make_pair(7, std::string("Warrior 7 at (3,4) has no order")));
	McpPlanCovers c;
	c.newUnits.insert(8192);
	std::vector<McpNewUnitBinding> bound(1, Binding(2, 81924, "Settler 81924", "Stockholm"));
	McpCoverBoundUnits(bound, c);
	std::vector<std::string> owed = McpOpeningOwed(o, c, true);
	CHECK(owed.size() == 1);
	CHECK(Has(owed, "Warrior 7 at (3,4) has no order"));
	CHECK(McpBindingLine(bound[0]) == "order 2 is for Settler 81924, which Stockholm finished as the turn opened: it runs as planned; give it no other order");
}

void TestAnOrderAddedWhenTheTurnOpensNeedsNoServes()
{
	std::string serves;
	CHECK(McpPlainToolName("mcp__civ5__unit_action") == "unit_action");      // a client's own prefix on a planned order
	CHECK(McpPlainToolName("mcp__my_server__move_unit") == "move_unit");
	CHECK(McpPlainToolName("unit_action") == "unit_action");
	CHECK(McpPlainToolName("mcp__") == "mcp__");
	CHECK(!McpOrderServes("move_unit", "", false, serves));
	CHECK(McpOrderServes("move_unit", "", true, serves) && serves == MCP_SERVES_ADDED);
	CHECK(McpOrderServes("move_unit", "military: hold the pass", true, serves) && serves == "military: hold the pass");
	CHECK(McpOrderServes("end_turn", "", false, serves) && serves.empty());
	CHECK(std::string(MCP_SERVES_ADDED) == "added when the turn opened");
}

void TestAPlannedBuildMustBePossibleWhereTheUnitWillStand()
{
	CHECK(McpPlanBuildFault(true, true, false, "The Wheel") == "");
	CHECK(McpPlanBuildFault(false, true, true, "The Wheel") == "");
	CHECK(McpPlanBuildFault(false, true, false, "The Wheel") == "it needs The Wheel first, and that is not due as the turn opens");
	CHECK(McpPlanBuildFault(false, false, true, "The Wheel") == "it cannot make that build there (wrong unit, or the tile does not take it)");
}

void TestAPlannedRangedAttackMustReachFromWhereTheUnitStands()
{
	CHECK(McpPlanStrikeFault(false, 2, 2, true) == "");
	CHECK(McpPlanStrikeFault(true, 5, 2, false) == "");
	CHECK(McpPlanStrikeFault(false, 3, 2, true) == "out of range: the target is 3 tiles away and this unit's range is 2; move it first in this plan, or plan another attacker");
	CHECK(McpPlanStrikeFault(false, 2, 2, false) == "no line of sight from where it stands: hills, mountains, forest or jungle block the view; move it first in this plan, or plan another attacker");
}

void TestWhileTheRoundIsOpenATurnToolPointsToThePlan()
{
	CHECK(McpNotYourTurn(false) == "it is not your turn right now (the turn is ending or other civilizations are moving); call wait_for_my_turn");
	CHECK(McpNotYourTurn(true) == "it is not your turn yet: put this in plan_turn's orders and it runs when your turn opens; with preview_only it answers now");
}

void TestAWaitWithTheViewReadAndNoPlanAsksForThePlanOnce()
{
	CHECK(McpAskForMissingPlan(true, true, false, false));
	CHECK(!McpAskForMissingPlan(true, true, false, true));
	CHECK(!McpAskForMissingPlan(true, true, true, false));
	CHECK(!McpAskForMissingPlan(true, false, false, false));
	CHECK(!McpAskForMissingPlan(false, true, false, false));
}

void TestAPlannedEndOfTurnSkipsUnitsThatOnlyFinishedTheirRoutes()
{
	CHECK(McpAfterHandBack(false, false, false, 0, 0) == MCP_END_AGAIN);
	CHECK(McpAfterHandBack(false, false, true, 0, 1) == MCP_HAND_BACK);
	CHECK(McpAfterHandBack(true, true, true, 0, 0) == MCP_END_AGAIN_SKIPPING_UNITS);
	CHECK(McpAfterHandBack(true, true, true, 0, 1) == MCP_END_AGAIN_SKIPPING_UNITS);
	CHECK(McpAfterHandBack(true, true, true, 0, 2) == MCP_HAND_BACK);
	CHECK(McpAfterHandBack(true, true, false, 0, 0) == MCP_HAND_BACK);
	CHECK(McpAfterHandBack(true, false, true, 0, 0) == MCP_HAND_BACK);
	CHECK(McpAfterHandBack(true, true, true, 1, 0) == MCP_HAND_BACK);
}

void TestOnlyAMessageThatAsksSomethingHoldsAPlansEndOfTurn()
{
	CHECK(McpMessageAsksNothing("BUTTONPOPUP_GOODY_HUT_REWARD"));
	CHECK(McpMessageAsksNothing("BUTTONPOPUP_BARBARIAN_CAMP_REWARD"));
	CHECK(McpMessageAsksNothing("BUTTONPOPUP_CITY_STATE_GREETING"));
	CHECK(McpMessageAsksNothing("BUTTONPOPUP_NATURAL_WONDER_REWARD"));
	CHECK(!McpMessageAsksNothing("BUTTONPOPUP_CITY_CAPTURED"));
	CHECK(!McpMessageAsksNothing("BUTTONPOPUP_RETURN_CIVILIAN"));
	CHECK(!McpMessageAsksNothing("BUTTONPOPUP_TEXT"));
	CHECK(!McpMessageAsksNothing(""));
}

int main()
{
	TestAtTurnOpenAFromCityOrderCoversTheUnitItWasBoundTo();
	TestAnOrderAddedWhenTheTurnOpensNeedsNoServes();
	TestAPlannedBuildMustBePossibleWhereTheUnitWillStand();
	TestAPlannedRangedAttackMustReachFromWhereTheUnitStands();
	TestWhileTheRoundIsOpenATurnToolPointsToThePlan();
	TestAWaitWithTheViewReadAndNoPlanAsksForThePlanOnce();
	TestAPlannedEndOfTurnSkipsUnitsThatOnlyFinishedTheirRoutes();
	TestOnlyAMessageThatAsksSomethingHoldsAPlansEndOfTurn();
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
	TestHeadingsInOrder();
	TestPlanTextShowsEveryHeadingWithItsTurn();
	TestWithoutAPlan();
	TestPlanBlockShowsEveryPartWithItsAge();
	TestFramingAsksWithoutPickingASide();
	TestOnlyRealBlockersBlock();
	TestNewsDropsWhatTheyAlreadyKnow();
	TestCivCodesAreUniqueAndStable();
	TestUnitStatsInBrackets();
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
	TestARoundOpensOnceTheGameTurnPassesTheSeatsLastEnd();
	TestOnlyTheTurnsOwnDriftDoesNotWake();
	TestWhatAnotherPlayerDidInSightWakes();
	TestThreatsWake();
	TestAPlannedTurnRunsUnlessSomethingHoldsIt();
	TestAnOrderForWhatIsAlreadySoCountsAsDone();
	TestABuildTheUnitCannotStartHereWaitsForItsQueuedMove();
	TestOnlyAPathThatCanNeverBeWalkedIsRefused();
	TestAQueueItemMovesByNeighbourSwaps();
	TestQueuePositionsCountFromOne();
	TestAPlanAnswersWhatTheTurnOpeningBrings();
	TestAnOrderForAUnitNotYetBuiltWaitsIfNoneCame();
	TestAnOrderForANewUnitNamesWhichOneWhenACityMakesSeveral();
	TestTheNthNewUnitIsInTheCityFirstThenNearestThenLowestId();
	TestOnePictureWhenTheCitiesFit();
	TestSpreadCitiesTakeMorePicturesUpToTheLimit();
	TestTiesGoToTheTighterPicture();
	TestNoCitiesNoPictures();
	TestEndTurnWaitsUntilTheGameHasRunTheQueuedMoves();
	TestAHandedBackTurnIsEndedAgainOnceWhenNothingAsksForTheSeat();
	TestAPlansEndOfTurnWaitsOnAFailedOrderOrANewMessage();
	TestUnlabeledFieldsAndNewAndGoneFields();
	TestAFallingCityGetsAClock();
	TestAUnitToolTakesFromCityInPlaceOfUnitId();
	TestArgumentsOutsideTheSchemaAreStillRefused();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all turn cycle tests passed\n");
	return 0;
}
