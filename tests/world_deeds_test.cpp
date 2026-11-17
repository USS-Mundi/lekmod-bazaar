// Native tests for the deeds between two moments of the world (no game needed): tests/run_world_deeds_test.sh
#include "McpWorldDeeds.h"
#include <cstdio>

static int s_failures = 0;
#define CHECK_EQ(a, b) do { std::string _a = (a), _b = (b); if(_a != _b) { std::printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); ++s_failures; } } while(0)
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

namespace
{
	McpWorld World(int turn)
	{
		McpWorld w;
		w.turn = turn; w.me = "Yugoslavia";
		w.players.insert("Yugoslavia"); w.players.insert("Goths"); w.players.insert("Inca"); w.players.insert("Madagascar");
		return w;
	}

	McpWorldCity City(const char* name, const char* owner, const char* first, bool capital = false, int founded = 0, const char* near = "Belgrade", int tiles = 12)
	{
		McpWorldCity c;
		c.name = name; c.owner = owner; c.firstOwner = first; c.firstCapital = capital; c.founded = founded; c.nearCity = near; c.nearTiles = tiles;
		return c;
	}

	std::string All(const McpWorld& a, const McpWorld& b, std::vector<std::string>* goneOut = NULL)
	{
		std::vector<std::string> gone;
		std::vector<McpWorldDeed> d = McpWorldDeeds(a, b, gone);
		std::string out;
		for(size_t i = 0; i < d.size(); ++i) out += (i ? " | " : "") + d[i].with + ": " + d[i].text;
		if(goneOut) *goneOut = gone;
		return out;
	}
}

static void TestNothingLiesBeforeTheFirstMoment()
{
	McpWorld none, now = World(10);
	now.wars[McpWorldPair("Goths", "Inca")] = "Goths";
	now.cities[1] = City("Cusco", "Goths", "Inca", true);
	CHECK_EQ(All(none, now), std::string(""));
	CHECK_EQ(All(now, now), std::string(""));
}

static void TestWarsAndPeace()
{
	McpWorld a = World(10), b = World(11);
	b.wars[McpWorldPair("Goths", "Madagascar")] = "Goths";
	CHECK_EQ(All(a, b), std::string("Goths: declared war on Madagascar | Madagascar: the Goths declared war on them"));
	b = World(11);
	b.wars[McpWorldPair("Inca", "Madagascar")] = "";
	CHECK_EQ(All(a, b), std::string("Inca: went to war with Madagascar | Madagascar: went to war with the Inca"));
	b = World(11);
	b.wars[McpWorldPair("Goths", "Yugoslavia")] = "Goths";
	CHECK_EQ(All(a, b), std::string("Goths: declared war on me"));
	b.wars[McpWorldPair("Goths", "Yugoslavia")] = "Yugoslavia";
	CHECK_EQ(All(a, b), std::string("Goths: I declared war on them"));
	b.wars[McpWorldPair("Goths", "Yugoslavia")] = "";
	CHECK_EQ(All(a, b), std::string("Goths: we went to war"));
	McpWorld c = World(12);
	CHECK_EQ(All(b, c), std::string("Goths: we made peace"));
	a.wars[McpWorldPair("Goths", "Inca")] = "Goths";
	CHECK_EQ(All(a, c), std::string("Goths: made peace with the Inca | Inca: made peace with the Goths"));
}

static void TestCitiesTakenRazedAndFoundedCloseBy()
{
	McpWorld a = World(10), b = World(11);
	a.cities[1] = City("Cusco", "Inca", "Inca", true);
	a.cities[2] = City("Belgrade", "Yugoslavia", "Yugoslavia", true, 0, "", -1);
	a.cities[3] = City("Ollantaytambo", "Inca", "Inca");
	b.cities = a.cities;
	b.cities[1].owner = "Goths";
	CHECK_EQ(All(a, b), std::string("Goths: took Cusco, a first capital, from the Inca | Inca: lost Cusco, their first capital, to the Goths"));
	b.cities = a.cities; b.cities[3].owner = "Yugoslavia";
	CHECK_EQ(All(a, b), std::string("Inca: I took Ollantaytambo from them"));
	b.cities = a.cities; b.cities[2].owner = "Goths";
	CHECK_EQ(All(a, b), std::string("Goths: took Belgrade, my first capital, from me"));
	// taken back by the one who founded it
	McpWorld c = World(12), d = World(13);
	c.cities[1] = City("Cusco", "Goths", "Inca", true);
	d.cities = c.cities; d.cities[1].owner = "Inca";
	CHECK_EQ(All(c, d), std::string("Inca: took Cusco, their first capital, back from the Goths | Goths: lost Cusco back to the Inca"));
	// razed: a taken city that is gone, and a city gone with nothing known of how
	d = World(13);
	CHECK_EQ(All(c, d), std::string("Goths: razed Cusco, once the Inca's | Inca: the Goths razed Cusco, once theirs"));
	c = World(12); c.cities[3] = City("Ollantaytambo", "Inca", "Inca");
	CHECK_EQ(All(c, d), std::string("Inca: Ollantaytambo is gone"));
	// a city-state taken is filed under its taker alone
	McpWorld g = World(14), h = World(15);
	g.cities[30] = City("Zurich", "Zurich", "Zurich");
	h.cities[30] = City("Zurich", "Goths", "Zurich");
	CHECK_EQ(All(g, h), std::string("Goths: took the city-state Zurich"));
	// founded: only when new and within 6 tiles of one of the seat's cities
	McpWorld e = World(20), f = World(21);
	f.cities[7] = City("Nesa", "Goths", "Goths", false, 21, "Belgrade", 5);
	f.cities[8] = City("Farplace", "Goths", "Goths", false, 21, "Belgrade", 7);
	f.cities[9] = City("Oldtown", "Goths", "Goths", false, 3, "Belgrade", 4);       // old, only now seen: not a deed
	f.cities[10] = City("Nis", "Yugoslavia", "Yugoslavia", false, 21, "", -1);
	CHECK_EQ(All(e, f), std::string("Goths: founded Nesa 5 tiles from my city Belgrade"));
}

static void TestPlayersLeaveTheGame()
{
	McpWorld a = World(10), b = World(11);
	a.wars[McpWorldPair("Goths", "Inca")] = "Goths";
	a.cities[1] = City("Cusco", "Inca", "Inca", true);
	a.friends.insert(McpWorldPair("Inca", "Madagascar"));
	b.players.erase("Inca");
	b.cities[1] = City("Cusco", "Goths", "Inca", true);
	std::vector<std::string> gone;
	CHECK_EQ(All(a, b, &gone), std::string("Goths: took Cusco, a first capital, from the Inca | Inca: lost Cusco, their first capital, to the Goths | Inca: left the game"));
	CHECK(gone.size() == 1 && gone[0] == "Inca");
}

static void TestFriendshipsDenouncementsPactsAndDeals()
{
	McpWorld a = World(10), b = World(11);
	b.friends.insert(McpWorldPair("Goths", "Inca"));
	b.friends.insert(McpWorldPair("Madagascar", "Yugoslavia"));
	CHECK_EQ(All(a, b), std::string("Goths: declared friendship with the Inca | Inca: declared friendship with the Goths | Madagascar: we declared friendship"));
	CHECK_EQ(All(b, a), std::string("Goths: their friendship with the Inca ended | Inca: their friendship with the Goths ended | Madagascar: our friendship ended"));
	b = World(11);
	b.denounced.insert(std::make_pair(std::string("Goths"), std::string("Inca")));
	b.denounced.insert(std::make_pair(std::string("Goths"), std::string("Yugoslavia")));
	b.denounced.insert(std::make_pair(std::string("Yugoslavia"), std::string("Madagascar")));
	CHECK_EQ(All(a, b), std::string("Goths: denounced the Inca | Inca: the Goths denounced them | Goths: denounced me | Madagascar: I denounced them"));
	b = World(11);
	b.pacts.insert(McpWorldPair("Goths", "Inca"));
	b.pacts.insert(McpWorldPair("Inca", "Yugoslavia"));
	CHECK_EQ(All(a, b), std::string("Goths: signed a defensive pact with the Inca | Inca: signed a defensive pact with the Goths | Inca: we signed a defensive pact"));
	b = World(11);
	b.deals["Goths"].insert("I give Furs; they give 6 gold a turn");
	CHECK_EQ(All(a, b), std::string("Goths: we made a deal: I give Furs; they give 6 gold a turn"));
	CHECK_EQ(All(b, a), std::string("Goths: a deal between us ended: I give Furs; they give 6 gold a turn"));
	// a deal that ends because war began is the war's, not a deed of its own
	McpWorld c = World(12);
	c.wars[McpWorldPair("Goths", "Yugoslavia")] = "Goths";
	CHECK_EQ(All(b, c), std::string("Goths: declared war on me"));
}

static void TestAMomentSurvivesTheFile()
{
	McpWorld a = World(10);
	a.wars[McpWorldPair("Goths", "Inca")] = "Goths";
	a.friends.insert(McpWorldPair("Inca", "Madagascar"));
	a.pacts.insert(McpWorldPair("Goths", "Madagascar"));
	a.denounced.insert(std::make_pair(std::string("Goths"), std::string("Yugoslavia")));
	a.cities[4] = City("Cusco", "Goths", "Inca", true, 2, "Belgrade", 9);
	a.deals["Goths"].insert("I give Furs; they give 6 gold a turn");
	picojson::value v;
	std::string err = picojson::parse(v, McpWorldToJson(a).serialize());
	McpWorld back;
	CHECK(err.empty() && McpWorldFromJson(v, back));
	CHECK_EQ(McpWorldToJson(back).serialize(), McpWorldToJson(a).serialize());
	CHECK_EQ(All(a, back), std::string(""));
	CHECK(back.turn == 10 && back.me == "Yugoslavia" && back.cities[4].firstCapital && back.cities[4].nearTiles == 9);
	CHECK(!McpWorldFromJson(picojson::value(std::string("x")), back));
}

int main()
{
	TestNothingLiesBeforeTheFirstMoment();
	TestWarsAndPeace();
	TestCitiesTakenRazedAndFoundedCloseBy();
	TestPlayersLeaveTheGame();
	TestFriendshipsDenouncementsPactsAndDeals();
	TestAMomentSurvivesTheFile();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all world deeds tests passed\n");
	return 0;
}
