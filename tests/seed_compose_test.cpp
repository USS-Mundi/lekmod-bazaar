// Native tests for the page dealer (no game needed): tests/run_seed_compose_test.sh
#include "McpSeedCompose.h"
#include <cstdio>
#include <sstream>
#include <set>

static int s_failures = 0;
#define CHECK_EQ(a, b) do { std::string _a = (a), _b = (b); if(_a != _b) { std::printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); ++s_failures; } } while(0)
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

namespace
{
	struct Case { const char *name, *civ, *second, *a, *b, *base, *table, *key, *page, *ids, *cut; int words; const char* classes; };
	const Case kCases[] = {
#include "seed_fixtures.inc"
	};

	std::vector<int> Ints(const std::string& s)
	{
		std::vector<int> out;
		std::istringstream in(s);
		int v;
		while(in >> v) out.push_back(v);
		return out;
	}

	std::string Join(const std::vector<std::string>& v)
	{
		std::string out;
		for(size_t i = 0; i < v.size(); ++i) out += (i ? " " : "") + v[i];
		return out;
	}

	McpSeedInput Input(const Case& c)
	{
		McpSeedInput in;
		in.civ = c.civ; in.second = c.second; in.a = Ints(c.a); in.b = Ints(c.b); in.base = Ints(c.base); in.dealKey = c.key;
		std::string table = c.table;
		for(size_t from = 0; !table.empty() && from <= table.size();)
		{
			size_t at = table.find(';', from);
			if(at == std::string::npos) at = table.size();
			in.table.push_back(Ints(table.substr(from, at - from)));
			from = at + 1;
		}
		return in;
	}
}

static void TestSha256()
{
	CHECK_EQ(McpSha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(McpSha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(McpSha256Hex(std::string(200, 'a')), std::string("c2a908d98f5df987ade41b5fce213067efbcc21ef2240212a41e54b5e7c28ae5"));
}

static void TestTheLibraryTakesOnlySignedLines()
{
	McpSeedLibrary lib;
	McpSeedLibraryParse("# a comment\n\nok boldness  +  plain  Bo+1   I go first.  \n?  boldness  +  plain  Bo+2   Not signed.\nok -  -  frame  open1  I rule {civ}.\n", false, lib);
	CHECK(lib["boldness + plain"].size() == 1);
	CHECK_EQ(lib["boldness + plain"][0].id, std::string("Bo+1"));
	CHECK_EQ(lib["boldness + plain"][0].text, std::string("I go first.  "));
	CHECK(lib["- - frame"].size() == 1);
	McpSeedLibrary all;
	McpSeedLibraryParse("ok boldness  +  plain  Bo+1   I go first.\n?  boldness  +  plain  Bo+2   Not signed.\n", true, all);
	CHECK(all["boldness + plain"].size() == 2);
	size_t n = 0;
	for(McpSeedLibrary::const_iterator it = McpSeedLibraryBuiltIn().begin(); it != McpSeedLibraryBuiltIn().end(); ++it) n += it->second.size();
	CHECK(n == 195);
}

static void TestPagesMatchThePythonComposerWordForWord()
{
	const McpSeedLibrary& lib = McpSeedLibraryBuiltIn();
	for(size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		const Case& c = kCases[i];
		std::string page, err;
		McpSeedRecord rec;
		bool ok = McpComposeSeed(Input(c), lib, page, rec, err);
		if(!ok) { std::printf("FAIL case %s: %s\n", c.name, err.c_str()); ++s_failures; continue; }
		if(page != c.page) { std::printf("FAIL case %s: page\n  got  %s\n  want %s\n", c.name, page.c_str(), c.page); ++s_failures; }
		CHECK_EQ(Join(rec.ids), std::string(c.ids));
		CHECK_EQ(Join(rec.cut), std::string(c.cut));
		CHECK(rec.words == c.words);
		std::string classes;
		for(size_t k = 0; k < rec.classes.size(); ++k) classes += (k ? " " : "") + rec.classes[k].first + "=" + rec.classes[k].second;
		CHECK_EQ(classes, std::string(c.classes));
		std::set<std::string> once(rec.ids.begin(), rec.ids.end());
		CHECK(once.size() == rec.ids.size());
		CHECK(rec.words <= 230);
	}
}

static void TestAMissingLineIsAnError()
{
	McpSeedLibrary lib;
	McpSeedLibraryParse("ok -  -  frame  open1  I rule {civ}.\n", false, lib);
	McpSeedInput in;
	in.civ = "X"; in.dealKey = "k";
	in.a = Ints("5 5 5 9 5 5 5 5 5 5 5 5");
	std::string page, err;
	McpSeedRecord rec;
	CHECK(!McpComposeSeed(in, lib, page, rec, err));
	CHECK(err.find("boldness") != std::string::npos);
	in.a = Ints("5 5 5");
	CHECK(!McpComposeSeed(in, McpSeedLibraryBuiltIn(), page, rec, err));
}

static void TestTheRollStaysInRangeAndRepeats()
{
	std::set<int> seen;
	for(int i = 0; i < 60; ++i)
	{
		std::ostringstream key; key << "game:" << i;
		int v = McpSeedRoll(5, key.str(), "LEADER_X", "boldness", 1, 10);
		CHECK(v >= 3 && v <= 7);
		CHECK(v == McpSeedRoll(5, key.str(), "LEADER_X", "boldness", 1, 10));
		seen.insert(v);
	}
	CHECK(seen.size() == 5);
	CHECK(McpSeedRoll(10, "g", "L", "boldness", 1, 10) <= 10);
	CHECK(McpSeedRoll(1, "g", "L", "boldness", 1, 10) >= 1);
}

static void TestPeoplesTakeThe()
{
	CHECK_EQ(McpSeedThe("Franks"), std::string("the Franks"));
	CHECK_EQ(McpSeedThe("Maya"), std::string("the Maya"));
	CHECK_EQ(McpSeedThe("Vatican"), std::string("the Vatican"));
	CHECK_EQ(McpSeedThe("The Huns"), std::string("the Huns"));
	CHECK_EQ(McpSeedThe("Ethiopia"), std::string("Ethiopia"));
	CHECK_EQ(McpSeedThe("Honduras"), std::string("Honduras"));
	CHECK_EQ(McpSeedThe("Hittites"), std::string("the Hittites"));
	CHECK_EQ(McpSeedLineText(McpSeedLibraryBuiltIn(), "open1"), std::string("I rule {civ}."));
	CHECK_EQ(McpSeedLineText(McpSeedLibraryBuiltIn(), "nope"), std::string(""));
}

int main()
{
	TestSha256();
	TestTheLibraryTakesOnlySignedLines();
	TestPagesMatchThePythonComposerWordForWord();
	TestAMissingLineIsAnError();
	TestTheRollStaysInRangeAndRepeats();
	TestPeoplesTakeThe();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all seed compose tests passed\n");
	return 0;
}
