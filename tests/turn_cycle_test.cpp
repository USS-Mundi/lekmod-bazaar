// Native tests for the turn cycle's game-free parts (no game needed): tests/run_turn_cycle_test.sh
#include "McpTurnCycle.h"
#include <cstdio>
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
		now.gold = 95;
		now.happiness = -2;
		std::vector<std::string> c = McpViewChanges(View(), now, 25);
		CHECK(Has(c, "gold 95 (was 130)"));
		CHECK(Has(c, "happiness -2 (was 1)"));
	}

	void TestMostUrgentFirstAndCapped()
	{
		McpView now = View();
		now.gold = 95;
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
			CHECK(c[3] == "gold 95 (was 130)");
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
	TestMostUrgentFirstAndCapped();
	TestOrdersToRun();
	TestWhyKinds();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all turn cycle tests passed\n");
	return 0;
}
