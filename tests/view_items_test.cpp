// Native tests for turning the screens' JSON into turn-view items (no game needed): tests/run_view_items_test.sh
#include "McpViewItems.h"
#include <cstdio>
#include <string>
#include <vector>

static int s_failures = 0;
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
}

int main()
{
	TestAListGoingFromEmptyToOneKeepsTheOtherListsNames();
	TestDuplicateKeysAreMadeUnique();
	TestCivIsAKeyForRankings();
	TestFieldsAreTidied();
	TestListsAreBracketed();
	TestTickingFields();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all view item tests passed\n");
	return 0;
}
