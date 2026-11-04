// Native tests for turning the screens' JSON into turn-view items (no game needed): tests/run_view_items_test.sh
#include "McpViewItems.h"
#include <cstdio>
#include <string>
#include <vector>

static int s_failures = 0;
#define CHECK_EQ(a, b) do { std::string _a = (a), _b = (b); if(_a != _b) { std::printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); ++s_failures; } } while(0)
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

namespace
{
	picojson::value Parse(const char* json)
	{
		picojson::value v;
		std::string err = picojson::parse(v, json);
		if(!err.empty()) std::printf("bad fixture: %s\n", err.c_str());
		return v;
	}

	std::string Join(const std::vector<std::pair<std::string, std::string> >& f)
	{
		std::string out;
		for(size_t i = 0; i < f.size(); ++i) out += (i ? " · " : "") + f[i].first + " " + f[i].second;
		return out;
	}

	const McpViewItem* Find(const std::vector<McpViewItem>& items, const std::string& key)
	{
		for(size_t i = 0; i < items.size(); ++i) if(items[i].key == key) return &items[i];
		return NULL;
	}

	std::string Field(const McpViewItem* it, const char* name)
	{
		if(!it) return "<no item>";
		for(size_t i = 0; i < it->fields.size(); ++i) if(it->fields[i].first == name) return it->fields[i].second;
		return "<none>";
	}

	void TestAListGoingFromEmptyToOneKeepsTheOtherListsNames()
	{
		std::vector<McpViewItem> before, now;
		McpScreenItems("religion", Parse("{\"faith\":4,\"religions\":[],\"cities\":[{\"name\":\"Caen\",\"religion\":\"none\"}]}"), before);
		McpScreenItems("religion", Parse("{\"faith\":9,\"religions\":[{\"name\":\"Buddhism\",\"founder\":\"Maurya\"}],\"cities\":[{\"name\":\"Caen\",\"religion\":\"none\"}]}"), now);
		const McpViewItem* caenBefore = Find(before, "religion: cities:Caen");
		const McpViewItem* caenNow = Find(now, "religion: cities:Caen");
		CHECK(caenBefore && caenNow);
		CHECK(Find(now, "religion: religions:Buddhism") != NULL);
		// The empty list is not a field of the summary line.
		CHECK(Field(Find(before, "religion:summary"), "religions") == "<none>");
	}

	void TestDuplicateKeysAreMadeUnique()
	{
		std::vector<McpViewItem> items;
		McpScreenItems("trade routes", Parse("{\"routes\":[{\"type\":\"land\",\"to\":\"Rouen\"},{\"type\":\"land\",\"to\":\"Bayeux\"}]}"), items);
		McpUniqueKeys(items);
		CHECK(items.size() == 2 && items[0].key != items[1].key);
		CHECK(items[1].key == items[0].key + "#2");
	}

	void TestCivIsAKeyForRankings()
	{
		std::vector<McpViewItem> items;
		McpScreenItems("rankings", Parse("{\"civs_met\":[{\"civ\":\"Maurya\",\"score\":225},{\"civ\":\"Tunisia\",\"score\":102}]}"), items);
		CHECK(Find(items, "rankings: civs met:Maurya") != NULL && Find(items, "rankings: civs met:Tunisia") != NULL);
	}

	void TestFieldsAreTidied()
	{
		std::vector<std::pair<std::string, std::string> > f;
		McpObjectFields(Parse("{\"name\":\"Worker\",\"type\":\"UNIT_WORKER\",\"x\":23,\"y\":17,\"is_combat\":false,\"is_worker\":true,"
			"\"help\":\"Allows you to build roads\",\"how\":\"city_work_tile locks a tile\",\"hp\":100,\"id\":147456}").get<picojson::object>(), f);
		CHECK(!f.empty() && f[0].first == "name" && f[0].second == "Worker");
		bool type = false, x = false, help = false, how = false, combat = false, at = false, id = false;
		for(size_t i = 0; i < f.size(); ++i)
		{
			if(f[i].first == "type") type = true;
			if(f[i].first == "x" || f[i].first == "y") x = true;
			if(f[i].first == "help") help = true;
			if(f[i].first == "how") how = true;
			if(f[i].first == "is combat" || f[i].first == "is worker") combat = true;
			if(f[i].first == "at" && f[i].second == "(23,17)") at = true;
			if(f[i].first == "id" && f[i].second == "147456") id = true;
		}
		CHECK(!type && !x && !help && !how && !combat && at && id);
	}

	void TestListsAreBracketed()
	{
		std::vector<std::pair<std::string, std::string> > f;
		McpObjectFields(Parse("{\"name\":\"Caen\",\"buildings\":[\"Monument\",\"Palace\"]}").get<picojson::object>(), f);
		CHECK(f.size() == 2 && f[1].first == "buildings" && f[1].second == "[Monument, Palace]");
	}

	void TestTickingFields()
	{
		CHECK(McpTickingField("moves left"));
		CHECK(McpTickingField("activity"));
		CHECK(McpTickingField("needs orders"));
		CHECK(McpTickingField("production.progress"));
		CHECK(McpTickingField("research.turns left"));
		CHECK(McpTickingField("turn") && McpTickingField("year") && McpTickingField("gold"));
		// Rates and a city's own yields are real changes.
		CHECK(!McpTickingField("gold per turn"));
		CHECK(!McpTickingField("yields per turn.gold"));
		CHECK(!McpTickingField("hp"));
	}
	void TestTilesAreShort()
	{
		CHECK_EQ(McpTileShort(Parse("{\"feature\":\"Forest\",\"hills\":true,\"resource\":\"Deer\",\"terrain\":\"Grassland\",\"x\":25,\"y\":22,"
			"\"yields\":{\"food\":2,\"gold\":0,\"production\":1}}").get<picojson::object>()), "deer forest hill (25,22) 2f 1p");
		CHECK_EQ(McpTileShort(Parse("{\"improvement\":\"Plantation\",\"resource\":\"Incense\",\"terrain\":\"Grassland\",\"x\":27,\"y\":21,\"locked\":true,"
			"\"yields\":{\"food\":2,\"gold\":2,\"production\":0,\"faith\":1}}").get<picojson::object>()), "incense plantation (27,21) 2f 2g 1fa locked");
		CHECK_EQ(McpTileShort(Parse("{\"lake\":true,\"terrain\":\"Coast\",\"water\":true,\"x\":26,\"y\":21,"
			"\"yields\":{\"food\":3,\"gold\":0,\"production\":0}}").get<picojson::object>()), "lake (26,21) 3f");
		CHECK_EQ(McpTileShort(Parse("{\"terrain\":\"Plains\",\"x\":1,\"y\":2,\"yields\":{\"food\":1,\"production\":1,\"science\":2,\"culture\":1}}").get<picojson::object>()),
			"plains (1,2) 1f 1p 2s 1c");
	}

	void TestCitizensBecomeWorksAndFree()
	{
		std::vector<std::pair<std::string, std::string> > f;
		McpCitizensFields(Parse("{\"city\":\"Caen\",\"focus\":\"default\",\"how\":\"x\",\"population\":3,\"unemployed\":0,\"avoid_growth\":false,"
			"\"specialists\":[{\"building\":\"Market\",\"can_add\":true,\"count\":0,\"max\":1,\"specialist\":\"Merchant\",\"type\":\"BUILDING_MARKET\"}],"
			"\"worked_tiles\":[{\"terrain\":\"Plains\",\"x\":1,\"y\":1,\"yields\":{\"food\":2,\"production\":1}}],"
			"\"workable_tiles\":[{\"terrain\":\"Coast\",\"lake\":true,\"x\":2,\"y\":2,\"yields\":{\"food\":3}},{\"terrain\":\"Grassland\",\"x\":3,\"y\":3,\"yields\":{\"food\":2}}]}").get<picojson::object>(), f);
		std::string works, free, focus;
		bool city = false, how = false;
		for(size_t i = 0; i < f.size(); ++i)
		{
			if(f[i].first == "works") works = f[i].second;
			if(f[i].first == "free") free = f[i].second;
			if(f[i].first == "focus") focus = f[i].second;
			if(f[i].first == "city") city = true;
			if(f[i].first == "how") how = true;
		}
		CHECK_EQ(works, "[plains (1,1) 2f 1p]");
		CHECK_EQ(free, "[lake (2,2) 3f; grassland (3,3) 2f]");
		CHECK_EQ(focus, "");
		CHECK(!city && !how);
		CHECK_EQ(Join(f), "works [plains (1,1) 2f 1p] · free [lake (2,2) 3f; grassland (3,3) 2f] · specialists Merchant 0/1 (Market)");
	}

	void TestClockFieldsTick()
	{
		CHECK(McpTickingField("clock"));
		CHECK(McpTickingField("clock build"));
		CHECK(McpTickingField("sources science"));
		// Where a clock lands and whether it is at its edge are compared: they move only when something did.
		CHECK(!McpTickingField("lands"));
		CHECK(!McpTickingField("edge"));
	}
}

namespace
{
	void TestNotesHealingFallingTurnsAndDemographicsTick()
	{
		CHECK(McpTickingField("note"));
		CHECK(McpTickingField("heals"));
		CHECK(McpTickingField("falling"));
		CHECK(McpTickingField("turns"));
		CHECK(McpTickingField("research.turns"));
		CHECK(McpTickingField("demographics.approval.rank"));
		CHECK(McpTickingField("demographics.gnp.value"));
		CHECK(McpTickingField("_activity"));
		CHECK(McpTickingField("moves"));
		CHECK(McpTickingField("yields"));
		CHECK(McpTickingField("near"));
		CHECK(McpTickingField("army"));
		CHECK(!McpTickingField("_at"));
		CHECK(!McpTickingField("lands"));
		CHECK(!McpTickingField("hp"));
	}

	void TestUnitLinesLeaveOutWhatDoesNotApply()
	{
		std::vector<std::pair<std::string, std::string> > f;
		f.push_back(std::make_pair(std::string("name"), std::string("Warrior")));
		f.push_back(std::make_pair(std::string("can found city here"), std::string("no")));
		f.push_back(std::make_pair(std::string("embarked"), std::string("no")));
		f.push_back(std::make_pair(std::string("hp"), std::string("71")));
		f.push_back(std::make_pair(std::string("max hp"), std::string("100")));
		f.push_back(std::make_pair(std::string("needs orders"), std::string("no")));
		f.push_back(std::make_pair(std::string("strength"), std::string("8")));
		McpTrimUnitFields(f);
		CHECK_EQ(Join(f), "name Warrior · hp 71 · strength 8");

		std::vector<std::pair<std::string, std::string> > g;
		g.push_back(std::make_pair(std::string("name"), std::string("Settler")));
		g.push_back(std::make_pair(std::string("can found city here"), std::string("yes")));
		g.push_back(std::make_pair(std::string("embarked"), std::string("yes")));
		g.push_back(std::make_pair(std::string("max hp"), std::string("120")));
		g.push_back(std::make_pair(std::string("needs orders"), std::string("yes")));
		McpTrimUnitFields(g);
		CHECK_EQ(Join(g), "name Settler · can found city here yes · embarked yes · max hp 120 · needs orders yes");
	}
}

int main()
{
	TestAListGoingFromEmptyToOneKeepsTheOtherListsNames();
	TestDuplicateKeysAreMadeUnique();
	TestCivIsAKeyForRankings();
	TestFieldsAreTidied();
	TestListsAreBracketed();
	TestTickingFields();
	TestTilesAreShort();
	TestCitizensBecomeWorksAndFree();
	TestClockFieldsTick();
	TestNotesHealingFallingTurnsAndDemographicsTick();
	TestUnitLinesLeaveOutWhatDoesNotApply();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all view item tests passed\n");
	return 0;
}
