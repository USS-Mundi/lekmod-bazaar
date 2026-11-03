// Native tests for the turn cycle's game-free parts (no game needed): tests/run_turn_cycle_test.sh
#include "McpTurnCycle.h"
#include <cstdio>
#include <map>
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

	void TestFirstPlanNeedsArcAndThrust()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		CHECK(!McpGamePlanApply(plan, Fields("arc", "land first"), "", 5, err, warn) && err.find("thrust") != std::string::npos);
		err.clear();
		CHECK(McpGamePlanApply(plan, Fields("arc", "land first", "thrust", "settler by T14"), "", 5, err, warn));
		CHECK(plan.turn == 5 && plan.sections["arc"] == "land first" && plan.log.size() == 1 && plan.log[0] == "T5: first plan");
	}

	void TestChangesNeedWhyAndKeepTheRest()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		McpGamePlanApply(plan, Fields("arc", "land first", "thrust", "settler by T14"), "", 5, err, warn);
		CHECK(!McpGamePlanApply(plan, Fields("thrust", "walls in Antium"), "", 9, err, warn) && err.find("why") != std::string::npos);
		err.clear();
		CHECK(McpGamePlanApply(plan, Fields("thrust", "walls in Antium"), "Turkish army at the border", 9, err, warn));
		CHECK(plan.sections["arc"] == "land first" && plan.sections["thrust"] == "walls in Antium");
		CHECK(plan.turn == 9 && plan.log.size() == 2 && plan.log[1] == "T9: Turkish army at the border");
		CHECK(McpGamePlanApply(plan, Fields("thrust", "walls in Antium"), "", 10, err, warn) && plan.log.size() == 2 && plan.turn == 9);
	}

	void TestUnknownHeadingAndLongPlan()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		CHECK(!McpGamePlanApply(plan, Fields("arc", "x", "dreams", "y"), "", 1, err, warn) && err.find("dreams") != std::string::npos);
		std::string longText;
		for(int i = 0; i < 460; ++i) longText += "word ";
		err.clear();
		CHECK(McpGamePlanApply(plan, Fields("arc", longText.c_str(), "thrust", "t"), "", 1, err, warn));
		CHECK(warn.size() == 1 && warn[0].find("words") != std::string::npos);
	}

	void TestPlanText()
	{
		McpGamePlan plan;
		std::string err;
		std::vector<std::string> warn;
		std::map<std::string, std::string> f;
		f["read"] = "3rd in score"; f["arc"] = "science"; f["thrust"] = "universities"; f["rivals"] = "Canada: culture"; f["tripwires"] = "Sistine built";
		McpGamePlanApply(plan, f, "", 40, err, warn);
		std::string full = McpGamePlanText(plan, true), head = McpGamePlanText(plan, false);
		CHECK(full.find("Read: 3rd in score") != std::string::npos && full.find("Rivals: Canada: culture") != std::string::npos);
		CHECK(full.find("T40: first plan") != std::string::npos);
		CHECK(head.find("Arc: science") != std::string::npos && head.find("Thrust: universities") != std::string::npos);
		CHECK(head.find("Tripwires: Sistine built") != std::string::npos && head.find("Read:") == std::string::npos);
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
	TestFirstPlanNeedsArcAndThrust();
	TestChangesNeedWhyAndKeepTheRest();
	TestUnknownHeadingAndLongPlan();
	TestPlanText();
	TestTurnsToAfford();
	TestPlanCountsFromTheTurnTheSeatEnded();
	TestOnePictureWhenTheCitiesFit();
	TestSpreadCitiesTakeMorePicturesUpToTheLimit();
	TestTiesGoToTheTighterPicture();
	TestNoCitiesNoPictures();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all turn cycle tests passed\n");
	return 0;
}
