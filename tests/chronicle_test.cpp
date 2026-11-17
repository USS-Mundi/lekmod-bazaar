// Native tests for the chronicle (no game needed): tests/run_chronicle_test.sh
#include "McpChronicle.h"
#include <cstdio>

static int s_failures = 0;
#define CHECK_EQ(a, b) do { std::string _a = (a), _b = (b); if(_a != _b) { std::printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); ++s_failures; } } while(0)
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++s_failures; } } while(0)

namespace
{
	picojson::object Args(const char* json)
	{
		picojson::value v;
		std::string err = picojson::parse(v, json);
		if(!err.empty() || !v.is<picojson::object>()) { std::printf("bad fixture: %s\n  %s\n", err.c_str(), json); ++s_failures; return picojson::object(); }
		return v.get<picojson::object>();
	}

	McpChronicleNow Now(int turn, const char* direction = "Hold the coast.", int era = 0, int eliminations = 0, int citiesLost = 0)
	{
		McpChronicleNow n;
		n.turn = turn; n.era = era; n.eliminations = eliminations; n.citiesLost = citiesLost; n.direction = direction;
		return n;
	}

	McpChronicle Started(int turn = 10)
	{
		McpChronicle c;
		std::vector<std::string> faults, unwritten;
		faults.push_back("One wants the small courts; the other sees only their ground.");
		unwritten.push_back("I do not know yet whether I ask or take.");
		McpChronicleStart(c, turn, 0, 0, 0, faults, unwritten);
		return c;
	}

	std::string Refusals(const McpChronicleResult& r)
	{
		std::string out;
		for(size_t i = 0; i < r.refusals.size(); ++i) out += (i ? " | " : "") + r.refusals[i];
		return out;
	}

	bool Has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

	bool Apply(McpChronicle& c, const char* json, const McpChronicleNow& now, McpChronicleResult& r)
	{
		r = McpChronicleResult();
		return McpChronicleApply(c, Args(json), now, r);
	}
}

static void TestARelationsEntryGoesStaleOnADeedAndClearsWhenResent()
{
	McpChronicle c = Started();
	McpChronicleResult r;
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf at my door.\",\"until\":30}}}}", Now(12), r));
	CHECK(c.relations["Goths"].written == 12 && c.relations["Goths"].until == 30);
	CHECK(Apply(c, "{}", Now(13), r));                                   // nothing happened: nothing owed
	McpChronicleDeed(c, 14, "Goths", "declared war on Madagascar");
	CHECK(!Apply(c, "{}", Now(15), r));
	CHECK(Has(Refusals(r), "Goths") && Has(Refusals(r), "declared war on Madagascar") && Has(Refusals(r), "written on turn 12"));
	// re-sent without a why: still refused; with one, changed or unchanged, it clears
	CHECK(!Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf at my door.\",\"until\":40}}}}", Now(15), r));
	CHECK(Has(Refusals(r), "why"));
	CHECK(c.relations["Goths"].written == 12);                           // a refused call applies nothing
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf at my door.\",\"until\":40,\"why\":\"He eats my neighbour; it changes nothing yet.\"}}}}", Now(15), r));
	CHECK(c.relations["Goths"].written == 15 && c.relations["Goths"].until == 40);
	CHECK(Apply(c, "{}", Now(16), r));
	CHECK(r.journal.empty());
}

static void TestARelationsEntryExpires()
{
	McpChronicle c = Started();
	McpChronicleResult r;
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"Useful.\",\"until\":20}}}}", Now(12), r));
	CHECK(Apply(c, "{}", Now(20), r));
	CHECK(!Apply(c, "{}", Now(21), r));
	CHECK(Has(Refusals(r), "Inca") && Has(Refusals(r), "held until turn 20"));
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"Still useful.\",\"until\":45,\"why\":\"Nothing between us moved.\"}}}}", Now(21), r));
	McpChronicleForget(c, "Inca");                                      // gone from the game: never owed again
	McpChronicleDeed(c, 30, "Inca", "left the game");
	CHECK(Apply(c, "{}", Now(60, "Hold the coast.", 0, 0, 0), r) || Has(Refusals(r), "chapter"));
	CHECK(!Has(Refusals(r), "Inca"));
}

static void TestAnEntryNeedsATurnItHoldsUntilAtMostThirtyAhead()
{
	McpChronicle c = Started();
	McpChronicleResult r;
	CHECK(!Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"Useful.\"}}}}", Now(12), r));
	CHECK(Has(Refusals(r), "until"));
	CHECK(!Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"Useful.\",\"until\":43}}}}", Now(12), r));
	CHECK(Has(Refusals(r), "42"));
	CHECK(!Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"Useful.\",\"until\":12}}}}", Now(12), r));
	CHECK(!Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"\",\"until\":20}}}}", Now(12), r));
	CHECK(!Apply(c, "{\"plan\":{\"relations\":\"Inca: useful.\"}}", Now(12), r));
	CHECK(Has(Refusals(r), "one entry for each player"));
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"Useful.\",\"until\":42}}}}", Now(12), r));
}

static void TestEveryRefusalComesAtOnceAndNothingIsApplied()
{
	McpChronicle c = Started();
	McpChronicleResult r;
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf.\",\"until\":30}}},\"threads\":{\"open\":[{\"with\":\"Goths\",\"words\":\"He owes me an answer.\"}]}}", Now(12), r));
	CHECK(c.threads.size() == 1 && c.threads[0].id == 1 && c.threads[0].opened == 12);
	McpChronicleDeed(c, 13, "Goths", "took Cusco, a first capital, from the Inca");
	CHECK(!Apply(c, "{\"threads\":{\"open\":[{\"with\":\"Inca\",\"words\":\"I stand with them.\"}],\"end\":[{\"id\":1,\"ending\":\"\"},{\"id\":9,\"ending\":\"x\"}]}}", Now(14), r));
	CHECK(r.refusals.size() == 3);
	CHECK(Has(Refusals(r), "Goths") && Has(Refusals(r), "thread 1") && Has(Refusals(r), "thread 9"));
	CHECK(c.threads.size() == 1 && c.threads[0].ended == -1);
}

static void TestAChapterIsDueForEachReason()
{
	McpChronicle c = Started(10);
	CHECK_EQ(McpChronicleDue(c, 0, 0, 0, 49), std::string(""));
	CHECK(Has(McpChronicleDue(c, 0, 0, 0, 50), "40 turns"));
	CHECK(Has(McpChronicleDue(c, 1, 0, 0, 20), "era"));
	CHECK(Has(McpChronicleDue(c, 0, 1, 0, 20), "left the game"));
	CHECK(Has(McpChronicleDue(c, 0, 0, 1, 20), "cities was taken"));
	McpChronicle none;
	CHECK_EQ(McpChronicleDue(none, 3, 2, 1, 200), std::string(""));       // not started: nothing due
}

static const char* CHAPTER = "{\"chapter\":{\"title\":\"The Years I Sold Furs to the Wolf\",\"became\":\"I have learned to wait.\","
	"\"threads\":[{\"id\":1,\"state\":\"owed\",\"words\":\"Not yet.\"}],\"nature\":[{\"fault\":1,\"ruled\":\"second\"},{\"unwritten\":1,\"settled\":\"I take.\"}],"
	"\"direction\":\"Arm the coast and wait for the wolf to overreach.\",\"said\":[{\"to\":\"Goths\",\"words\":\"I see you.\"}]}}";

static void TestADueChapterMustBeClosedAndItsDirectionWrittenAgain()
{
	McpChronicle c = Started(10);
	McpChronicleResult r;
	CHECK(Apply(c, "{\"threads\":{\"open\":[{\"with\":\"Goths\",\"words\":\"He owes me an answer.\"}]}}", Now(12), r));
	CHECK(!Apply(c, "{}", Now(20, "Hold the coast.", 1), r));
	CHECK(Has(Refusals(r), "chapter") && Has(Refusals(r), "era"));
	// identical direction: refused
	CHECK(!Apply(c, CHAPTER, Now(20, "Arm the coast and wait for the wolf to overreach.", 1), r));
	CHECK(Has(Refusals(r), "write it again"));
	// a thread left unanswered, a fault left unanswered, and a state that is none of the three: refused
	CHECK(!Apply(c, "{\"chapter\":{\"title\":\"T\",\"became\":\"B\",\"threads\":[],\"nature\":[],\"direction\":\"D\"}}", Now(20, "Hold the coast.", 1), r));
	CHECK(Has(Refusals(r), "thread 1") && Has(Refusals(r), "fault 1"));
	CHECK(!Apply(c, "{\"chapter\":{\"title\":\"T\",\"became\":\"B\",\"threads\":[{\"id\":1,\"state\":\"forgotten\",\"words\":\"w\"}],\"nature\":[{\"fault\":1,\"ruled\":\"first\"}],\"direction\":\"D\"}}", Now(20, "Hold the coast.", 1), r));
	CHECK(Has(Refusals(r), "kept, broken or owed"));
	CHECK(Apply(c, CHAPTER, Now(20, "Hold the coast.", 1), r));
	CHECK(r.chapterClosed);
	CHECK_EQ(r.direction, std::string("Arm the coast and wait for the wolf to overreach."));
	CHECK(r.say.size() == 1 && r.say[0].first == "Goths" && r.say[0].second == "I see you.");
	CHECK(c.chapters.size() == 1 && c.chapters[0].opened == 10 && c.chapters[0].closed == 20 && c.opened == 20 && c.era == 1);
	CHECK(c.threads[0].ended == -1 && c.threads[0].chapters == 1);         // owed: carried
	CHECK(c.settled.size() == 1 && c.settled[0] == "I take.");
	CHECK_EQ(McpChronicleBecame(c), std::string("I have learned to wait."));
	CHECK_EQ(McpChronicleDue(c, 1, 0, 0, 21), std::string(""));
	// kept or broken ends the thread with the words as its ending
	CHECK(Apply(c, "{\"chapter\":{\"title\":\"Two\",\"became\":\"Harder.\",\"threads\":[{\"id\":1,\"state\":\"broken\",\"words\":\"I let it go.\"}],\"nature\":[{\"fault\":1,\"ruled\":\"second\"}],\"direction\":\"Now the wolf.\"}}", Now(30, "Arm the coast and wait for the wolf to overreach.", 1), r));
	CHECK(c.threads[0].ended == 30 && c.threads[0].ending == "broken: I let it go.");
	std::string block = McpChronicleBlock(c, "Yugoslavia", "I rule Yugoslavia.", 31, "");
	CHECK(Has(block, "second ruled 2 of 2 ages"));
}

static void TestVowsAreKeptBrokenAndWithdrawn()
{
	McpChronicle c = Started(10);
	McpChronicleResult r;
	CHECK(!Apply(c, "{\"threads\":{\"open\":[{\"with\":\"Goths\",\"words\":\"I will have Ravenna.\",\"vow\":{\"kind\":\"conquer\",\"by\":30}}]}}", Now(12), r));
	CHECK(Has(Refusals(r), "war, peace, pact, deal, take or hold"));
	CHECK(!Apply(c, "{\"threads\":{\"open\":[{\"with\":\"Goths\",\"words\":\"I will have Ravenna.\",\"vow\":{\"kind\":\"take\",\"by\":30}}]}}", Now(12), r));
	CHECK(Has(Refusals(r), "subject"));
	CHECK(Apply(c, "{\"threads\":{\"open\":["
		"{\"with\":\"Goths\",\"words\":\"I will have Ravenna.\",\"vow\":{\"kind\":\"take\",\"subject\":\"Ravenna\",\"by\":30}},"
		"{\"with\":\"Goths\",\"words\":\"War before the age is out.\",\"vow\":{\"kind\":\"war\",\"by\":20}},"
		"{\"with\":\"Inca\",\"words\":\"No blade against them.\",\"vow\":{\"kind\":\"peace\",\"by\":25}},"
		"{\"with\":\"Inca\",\"words\":\"Belgrade stands.\",\"vow\":{\"kind\":\"hold\",\"subject\":\"Belgrade\",\"by\":22}},"
		"{\"with\":\"Maya\",\"words\":\"A pact, soon.\",\"vow\":{\"kind\":\"pact\",\"by\":40}}]}}", Now(12), r));
	CHECK(c.threads.size() == 5);
	std::map<int, McpVowFact> facts;
	facts[1].met = true;                       // Ravenna taken on turn 15
	McpVowsResolve(c, facts, 15);
	CHECK_EQ(c.threads[0].vowState, std::string("kept"));
	CHECK(c.deeds.size() == 1 && c.deeds[0].with == "Goths" && Has(c.deeds[0].text, "kept") && Has(c.deeds[0].text, "Ravenna"));
	facts.clear();
	facts[3].failed = true;                    // the seat declared war on the Inca
	McpVowsResolve(c, facts, 18);
	CHECK_EQ(c.threads[2].vowState, std::string("broken"));
	facts.clear();
	McpVowsResolve(c, facts, 21);              // turn 20 passed with no war on the Goths
	CHECK_EQ(c.threads[1].vowState, std::string("broken"));
	CHECK_EQ(c.threads[3].vowState, std::string(""));
	McpVowsResolve(c, facts, 22);              // Belgrade held through turn 22
	CHECK_EQ(c.threads[3].vowState, std::string("kept"));
	size_t deeds = c.deeds.size();
	McpVowsResolve(c, facts, 23);              // settled vows are not settled twice
	CHECK(c.deeds.size() == deeds);
	// ending a thread while its vow is open withdraws it, and that too is a deed
	CHECK(Apply(c, "{\"threads\":{\"end\":[{\"id\":5,\"ending\":\"They are not worth a pact.\"}]}}", Now(24), r));
	CHECK_EQ(c.threads[4].vowState, std::string("withdrawn"));
	CHECK(Has(c.deeds.back().text, "withdr") && c.deeds.back().with == "Maya");
	// a vow's result makes the entry on that player stale
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Maya\":{\"text\":\"Small.\",\"until\":50}}}}", Now(24), r));
	CHECK(Apply(c, "{}", Now(25), r));
}

static void TestAVowResultMakesTheEntryStale()
{
	McpChronicle c = Started(10);
	McpChronicleResult r;
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"Mine to break.\",\"until\":40}}},\"threads\":{\"open\":[{\"with\":\"Goths\",\"words\":\"War.\",\"vow\":{\"kind\":\"war\",\"by\":14}}]}}", Now(12), r));
	std::map<int, McpVowFact> facts;
	McpVowsResolve(c, facts, 15);
	CHECK(!Apply(c, "{}", Now(15), r));
	CHECK(Has(Refusals(r), "Goths") && Has(Refusals(r), "broken"));
}

static void TestTheFileReplaysToTheSameChronicle()
{
	McpChronicle c = Started(10);
	McpChronicleResult r;
	McpChronicleDeed(c, 11, "Goths", "declared war on Madagascar");
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf.\",\"until\":30}}},\"threads\":{\"open\":[{\"with\":\"Goths\",\"words\":\"War.\",\"vow\":{\"kind\":\"war\",\"by\":14}},{\"with\":\"Inca\",\"words\":\"I stand with them.\"}]}}", Now(12), r));
	std::map<int, McpVowFact> facts;
	McpVowsResolve(c, facts, 15);
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf I did not hunt.\",\"until\":40,\"why\":\"I let the hour pass.\"}}},\"threads\":{\"end\":[{\"id\":2,\"ending\":\"They fell without me.\"}]}}", Now(16), r));
	std::vector<std::string> served;
	served.push_back("Inca");
	McpChronicleMirror(c, served, 16);
	CHECK(Apply(c, "{\"chapter\":{\"title\":\"One\",\"became\":\"Slower.\",\"threads\":[{\"id\":1,\"state\":\"owed\",\"words\":\"Still owed.\"}],\"nature\":[{\"fault\":1,\"ruled\":\"both\"}],\"direction\":\"New course.\",\"said\":[]}}", Now(17), r));
	McpChronicleForget(c, "Inca");
	std::string file;
	for(size_t i = 0; i < c.log.size(); ++i) file += c.log[i] + "\n";
	McpChronicle back;
	McpChronicleLoad(back, file + "not json\n");
	CHECK(back.log.empty());
	CHECK_EQ(McpChronicleBlock(back, "Yugoslavia", "I rule Yugoslavia.", 20, ""), McpChronicleBlock(c, "Yugoslavia", "I rule Yugoslavia.", 20, ""));
	CHECK_EQ(McpChronicleFull(back).serialize(), McpChronicleFull(c).serialize());
	CHECK(back.started && back.opened == 17 && back.threads.size() == 2 && back.chapters.size() == 1);
	CHECK_EQ(McpChronicleDue(back, 0, 0, 0, 56), McpChronicleDue(c, 0, 0, 0, 56));
}

static void TestTheMirrorCountsRoundsNothingServed()
{
	McpChronicle c = Started(10);
	McpChronicleResult r;
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf.\",\"until\":30},\"Inca\":{\"text\":\"Mine to guard.\",\"until\":30}}},\"threads\":{\"open\":[{\"with\":\"Inca\",\"words\":\"I stand with them.\"}]}}", Now(12), r));
	std::vector<std::string> served;
	served.push_back("Goths");
	std::vector<std::string> m = McpChronicleMirror(c, served, 12);
	CHECK(m.size() == 2);
	CHECK(Has(m[0], "Inca") && Has(m[0], "1 round"));
	m = McpChronicleMirror(c, served, 13);
	CHECK(Has(m[0], "2 rounds") && Has(m[1], "thread 1") && Has(m[1], "2 rounds"));
	served.push_back("Inca");
	m = McpChronicleMirror(c, served, 14);
	CHECK(m.empty());
	served.clear();
	m = McpChronicleMirror(c, served, 15);
	CHECK(m.size() == 3 && Has(m[0], "1 round"));
}

static void TestTheBlockShowsWhatIsOwed()
{
	McpChronicle c = Started(10);
	McpChronicleResult r;
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf.\",\"until\":30}}},\"threads\":{\"open\":[{\"with\":\"Goths\",\"words\":\"He owes me an answer.\",\"vow\":{\"kind\":\"war\",\"by\":40}}]}}", Now(12), r));
	McpChronicleDeed(c, 14, "Goths", "declared war on Madagascar");
	McpChronicleDeed(c, 14, "Madagascar", "the Goths declared war on them");
	std::string b = McpChronicleBlock(c, "Yugoslavia", "I rule Yugoslavia.\n\nI go first.", 15, "your era changed");
	CHECK(Has(b, "THE CHRONICLE OF YUGOSLAVIA") && Has(b, "turn 15") && Has(b, "chapter 1, opened T10"));
	CHECK(Has(b, "FIRST PAGE\n  I rule Yugoslavia.\n\n  I go first."));
	CHECK(!Has(b, "WHO I HAVE BECOME"));
	CHECK(Has(b, "THE NATURES, AGE BY AGE\n  1. One wants the small courts; the other sees only their ground.\n     no age closed yet"));
	CHECK(Has(b, "STILL UNWRITTEN\n  1. I do not know yet whether I ask or take."));
	CHECK(Has(b, "DEEDS NOT YET IN YOUR RELATIONS"));
	CHECK(Has(b, "Goths       T14 declared war on Madagascar.") && Has(b, "your entry on Goths was written T12"));
	CHECK(Has(b, "Madagascar  T14 the Goths declared war on them.") && Has(b, "you have no entry on Madagascar"));
	CHECK(Has(b, "OPEN THREADS\n  #1  Goths  \"He owes me an answer.\"  opened T12 \xC2\xB7 3 turns \xC2\xB7 vow: war by T40"));
	CHECK(Has(b, "CHAPTER DUE: your era changed"));
	CHECK(!Has(b, "army") && !Has(b, "score"));
	// answered: the deed leaves the list; an entry about to run out is said
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf, and now my enemy's enemy.\",\"until\":16,\"why\":\"His war is not mine.\"}}}}", Now(15), r));
	b = McpChronicleBlock(c, "Yugoslavia", "I rule Yugoslavia.", 17, "");
	CHECK(!Has(b, "T14 declared war on Madagascar"));
	CHECK(Has(b, "YOUR ENTRIES THAT HAVE RUN OUT\n  Goths  held until T16"));
	CHECK(!Has(b, "CHAPTER DUE"));
	McpChronicle empty;
	CHECK_EQ(McpChronicleBlock(empty, "Yugoslavia", "", 3, ""), std::string(""));
	CHECK(Apply(c, "{\"plan\":{\"relations\":{\"Inca\":{\"text\":\"Mine to guard\",\"until\":40}}}}", Now(16), r));
	CHECK_EQ(McpChronicleRelationsText(c), std::string("Goths (holds until T16): A wolf, and now my enemy's enemy. Inca (holds until T40): Mine to guard."));
}

static void TestAnEntryOrThreadMustNameAPlayerTheSeatHasMet()
{
	McpChronicle c = Started();
	McpChronicleResult r;
	McpChronicleNow now = Now(12);
	now.players.push_back("Goths"); now.players.push_back("Inca");
	r = McpChronicleResult();
	CHECK(!McpChronicleApply(c, Args("{\"plan\":{\"relations\":{\"The Goths\":{\"text\":\"A wolf.\",\"until\":30}}},\"threads\":{\"open\":[{\"with\":\"Gothia\",\"words\":\"w\"}]}}"), now, r));
	CHECK(r.refusals.size() == 2 && Has(Refusals(r), "Goths, Inca"));
	CHECK(McpChronicleApply(c, Args("{\"plan\":{\"relations\":{\"Goths\":{\"text\":\"A wolf.\",\"until\":30}}}}"), now, r));
	McpChronicle none;                                                   // not started: plan_turn goes on as before
	CHECK(McpChronicleApply(none, Args("{\"plan\":{\"relations\":\"anything\"}}"), now, r));
}

int main()
{
	TestARelationsEntryGoesStaleOnADeedAndClearsWhenResent();
	TestARelationsEntryExpires();
	TestAnEntryNeedsATurnItHoldsUntilAtMostThirtyAhead();
	TestEveryRefusalComesAtOnceAndNothingIsApplied();
	TestAChapterIsDueForEachReason();
	TestADueChapterMustBeClosedAndItsDirectionWrittenAgain();
	TestVowsAreKeptBrokenAndWithdrawn();
	TestAVowResultMakesTheEntryStale();
	TestTheFileReplaysToTheSameChronicle();
	TestTheMirrorCountsRoundsNothingServed();
	TestTheBlockShowsWhatIsOwed();
	TestAnEntryOrThreadMustNameAPlayerTheSeatHasMet();
	if(s_failures) { std::printf("%d failed\n", s_failures); return 1; }
	std::printf("all chronicle tests passed\n");
	return 0;
}
