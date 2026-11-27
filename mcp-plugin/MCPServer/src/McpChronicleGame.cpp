#include "CvGameCoreDLLPCH.h"
#include "McpChronicleGame.h"
#include "McpChatGame.h"
#include "McpChronicle.h"
#include "McpWorldDeeds.h"
#include "McpSeedCompose.h"
#include "McpCityStateFiles.h"
#include "McpPlanJournal.h"
#include "McpToolSupport.h"
#include "McpToolHandlers.h"
#include "McpSeat.h"
#include "McpLua.h"
#include "CvGame.h"
#include "CvPlayer.h"
#include "CvTeam.h"
#include "CvCity.h"
#include "CvPlot.h"
#include "CvMap.h"
#include "CvDiplomacyAI.h"
#include "CvGlobals.h"
#include "CvGameCoreUtils.h"

using namespace McpTool;

namespace
{
	struct SeatSeed
	{
		bool tried;
		std::string page;
		std::vector<std::string> faults, unwritten;
		SeatSeed() : tried(false) {}
	};

	struct Seat
	{
		bool loaded;
		McpChronicle chronicle, pending;      // pending: the copy a plan_turn was checked against, kept on commit
		bool hasPending;
		std::vector<std::pair<std::string, std::string> > pendingSay;
		std::vector<std::string> pendingJournal;
		McpWorld world;
		Seat() : loaded(false), hasPending(false) {}
	};

	std::string s_game;
	std::map<int, SeatSeed> s_seeds;
	std::map<int, Seat> s_seats;
	std::map<std::pair<int, int>, int> s_declared;     // (team, team) with the lower first -> the team that declared

	void SameGame()
	{
		std::string game = McpGameId();
		if(game == s_game) return;
		s_game = game;
		s_seeds.clear();
		s_seats.clear();
		s_declared.clear();
	}

	std::string CivFile(CvPlayer* me, const char* ext) { return std::string(me->getCivilizationShortDescription()) + ext; }

	bool Major(const CvPlayer* me) { return (int)me->GetID() < MAX_MAJOR_CIVS && !me->isMinorCiv() && !me->isBarbarian(); }

	std::vector<int> RolledSheet(CvPlayer& p)
	{
		CvDiplomacyAI* d = p.GetDiplomacyAI();
		std::vector<int> s;
		s.push_back(d->GetVictoryCompetitiveness()); s.push_back(d->GetWonderCompetitiveness()); s.push_back(d->GetMinorCivCompetitiveness());
		s.push_back(d->GetBoldness()); s.push_back(d->GetDoFWillingness()); s.push_back(d->GetLoyalty()); s.push_back(d->GetNeediness());
		s.push_back(d->GetDiploBalance()); s.push_back(d->GetForgiveness()); s.push_back(d->GetWarmongerHate()); s.push_back(d->GetMeanness());
		s.push_back(d->GetDenounceWillingness());
		return s;
	}

	std::vector<int> BaseSheet(LeaderHeadTypes eLeader)
	{
		std::vector<int> s;
		CvLeaderHeadInfo* l = eLeader == NO_LEADER ? NULL : GC.getLeaderHeadInfo(eLeader);
		if(!l) return s;
		s.push_back(l->GetVictoryCompetitiveness()); s.push_back(l->GetWonderCompetitiveness()); s.push_back(l->GetMinorCivCompetitiveness());
		s.push_back(l->GetBoldness()); s.push_back(l->GetDoFWillingness()); s.push_back(l->GetLoyalty()); s.push_back(l->GetNeediness());
		s.push_back(l->GetDiploBalance()); s.push_back(l->GetForgiveness()); s.push_back(l->GetWarmongerHate()); s.push_back(l->GetMeanness());
		s.push_back(l->GetDenounceWillingness());
		return s;
	}

	// A seat's second leader, when the Second Leader option gave it one: its base sheet rolled as the game rolls the
	// first, keyed on the game and the seat instead of the game's random stream, so a reload deals the same person.
	bool SecondSheet(CvPlayer& p, const std::string& key, std::string& civName, std::string& leaderType, std::vector<int>& base, std::vector<int>& rolled)
	{
		std::string civType;
		if(!McpLuaSecondLeader((int)p.GetID(), civType, leaderType)) return false;
		LeaderHeadTypes eLeader = (LeaderHeadTypes)GC.getInfoTypeForString(leaderType.c_str(), true);
		CivilizationTypes eCiv = (CivilizationTypes)GC.getInfoTypeForString(civType.c_str(), true);
		base = BaseSheet(eLeader);
		CvCivilizationInfo* civ = eCiv == NO_CIVILIZATION ? NULL : GC.getCivilizationInfo(eCiv);
		if(base.size() != MCP_SEED_TRAITS || !civ) return false;
		civName = Localized(civ->getShortDescriptionKey());
		rolled.clear();
		for(int t = 0; t < MCP_SEED_TRAITS; ++t)
			rolled.push_back(McpSeedRoll(base[t], key, leaderType, kMcpSeedTraits[t], GC.getPERSONALITY_FLAVOR_MIN_VALUE(), GC.getPERSONALITY_FLAVOR_MAX_VALUE()));
		return true;
	}

	JVal Ints(const std::vector<int>& v)
	{
		JArr a;
		for(size_t i = 0; i < v.size(); ++i) a.push_back(J(v[i]));
		return JVal(a);
	}

	JVal Texts(const std::vector<std::string>& v)
	{
		JArr a;
		for(size_t i = 0; i < v.size(); ++i) a.push_back(J(v[i]));
		return JVal(a);
	}

	void TextsFrom(const JObj& o, const char* key, std::vector<std::string>& out)
	{
		JObj::const_iterator it = o.find(key);
		if(it == o.end() || !it->second.is<JArr>()) return;
		for(size_t i = 0; i < it->second.get<JArr>().size(); ++i)
			if(it->second.get<JArr>()[i].is<std::string>()) out.push_back(it->second.get<JArr>()[i].get<std::string>());
	}

	const SeatSeed& Seed(CvPlayer* me)
	{
		SameGame();
		SeatSeed& seed = s_seeds[(int)me->GetID()];
		if(seed.tried) return seed;
		seed.tried = true;
		// dealt before, in this game: the file is the person
		std::string kept = McpGameFileRead(s_game, "seeds", CivFile(me, ".json"));
		JVal v;
		if(!kept.empty() && picojson::parse(v, kept).empty() && v.is<JObj>() && !ArgStr(v.get<JObj>(), "page").empty())
		{
			seed.page = ArgStr(v.get<JObj>(), "page");
			TextsFrom(v.get<JObj>(), "faults", seed.faults);
			TextsFrom(v.get<JObj>(), "unwritten", seed.unwritten);
			return seed;
		}
		McpSeedInput in;
		in.dealKey = s_game + ":" + me->getCivilizationTypeKey();
		in.civ = McpSeedThe(me->getCivilizationShortDescription());
		in.a = RolledSheet(*me);
		std::vector<int> base = BaseSheet(me->getLeaderType()), secondBase;
		std::string secondCiv, secondLeader;
		bool two = SecondSheet(*me, in.dealKey, secondCiv, secondLeader, secondBase, in.b);
		if(two) in.second = McpSeedThe(secondCiv);
		else in.base = base;
		// where the seat stands at this table: every other major seat's value, doubled, a pair of leaders at its mean
		in.table.assign(MCP_SEED_TRAITS, std::vector<int>());
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || p.isMinorCiv() || i == (int)me->GetID()) continue;
			std::vector<int> a = RolledSheet(p), b, bBase;
			std::string c, l;
			bool pair = SecondSheet(p, s_game + ":" + p.getCivilizationTypeKey(), c, l, bBase, b);
			for(int t = 0; t < MCP_SEED_TRAITS; ++t) in.table[t].push_back(pair ? a[t] + b[t] : 2 * a[t]);
		}
		if(in.table[0].empty()) in.table.clear();
		std::string err;
		McpSeedRecord rec;
		if(!McpComposeSeed(in, McpSeedLibraryBuiltIn(), seed.page, rec, err))
		{
			McpDiagLog("leader seed: no page for " + std::string(me->getCivilizationShortDescription()) + ": " + err);
			seed.page.clear();
			return seed;
		}
		for(size_t i = 0; i < rec.ids.size(); ++i)
		{
			if(rec.ids[i].find('~') != std::string::npos) seed.faults.push_back(McpSeedLineText(McpSeedLibraryBuiltIn(), rec.ids[i]));
			else if(rec.ids[i].find('?') != std::string::npos) seed.unwritten.push_back(McpSeedLineText(McpSeedLibraryBuiltIn(), rec.ids[i]));
		}
		JObj o;
		o["page"] = J(seed.page);
		o["faults"] = Texts(seed.faults);
		o["unwritten"] = Texts(seed.unwritten);
		o["key"] = J(in.dealKey);
		JArr leaders, rolled, bases;
		const char* leaderKey = GC.getLeaderHeadInfo(me->getLeaderType()) ? GC.getLeaderHeadInfo(me->getLeaderType())->GetType() : "";
		leaders.push_back(J(leaderKey)); rolled.push_back(Ints(in.a)); bases.push_back(Ints(base));
		if(two) { leaders.push_back(J(secondLeader)); rolled.push_back(Ints(in.b)); bases.push_back(Ints(secondBase)); }
		o["leaders"] = JVal(leaders); o["rolled"] = JVal(rolled); o["base"] = JVal(bases);
		JObj classes;
		for(size_t i = 0; i < rec.classes.size(); ++i) classes[rec.classes[i].first] = J(rec.classes[i].second);
		o["classes"] = JVal(classes); o["ids"] = Texts(rec.ids); o["cut"] = Texts(rec.cut); o["words"] = J(rec.words);
		McpGameFileAppend(s_game, "seeds", CivFile(me, ".json"), "", JVal(o).serialize() + "\n");
		McpGameFileAppend(s_game, "seeds", CivFile(me, ".md"), "", seed.page + "\n");
		if(McpPlanJournalOn())
		{
			JObj row = o;
			row.erase("page");
			row["event"] = J("seed"); row["game"] = J(s_game); row["turn"] = J(GC.getGame().getGameTurn());
			row["civ"] = J(me->getCivilizationShortDescription());
			McpPlanJournalAppend(JVal(row).serialize());
		}
		return seed;
	}

	void Flush(CvPlayer* me, McpChronicle& c)
	{
		for(size_t i = 0; i < c.log.size(); ++i) McpGameFileAppend(s_game, "chronicle", CivFile(me, ".jsonl"), "", c.log[i] + "\n");
		c.log.clear();
		for(size_t i = 0; i < c.journal.size(); ++i)
		{
			JVal v;
			if(!McpPlanJournalOn() || !picojson::parse(v, c.journal[i]).empty() || !v.is<JObj>()) continue;
			v.get<JObj>()["game"] = J(s_game);
			v.get<JObj>()["civ"] = J(me->getCivilizationShortDescription());
			McpPlanJournalAppend(v.serialize());
		}
		c.journal.clear();
	}

	Seat& SeatOf(CvPlayer* me)
	{
		SameGame();
		Seat& s = s_seats[(int)me->GetID()];
		if(s.loaded) return s;
		s.loaded = true;
		McpChronicleLoad(s.chronicle, McpGameFileRead(s_game, "chronicle", CivFile(me, ".jsonl")));
		// the last moment the seat was brought up to date at: the file's last line
		std::string moments = McpGameFileRead(s_game, "chronicle", CivFile(me, ".world.jsonl"));
		while(!moments.empty() && (moments[moments.size() - 1] == '\n' || moments[moments.size() - 1] == '\r')) moments.erase(moments.size() - 1);
		size_t at = moments.rfind('\n');
		JVal v;
		if(!moments.empty() && picojson::parse(v, at == std::string::npos ? moments : moments.substr(at + 1)).empty()) McpWorldFromJson(v, s.world);
		return s;
	}

	int Eliminations()
	{
		int n = 0;
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i) if(GET_PLAYER((PlayerTypes)i).isEverAlive() && !GET_PLAYER((PlayerTypes)i).isAlive()) ++n;
		return n;
	}

	// A deal's terms as the seat would say them, without its clock: "I give Furs 1; they give 6 gold a turn".
	std::string DealTerms(const JObj& deal, const std::string& me)
	{
		std::string give, get;
		JObj::const_iterator items = deal.find("items");
		if(items == deal.end() || !items->second.is<JArr>()) return "";
		for(size_t i = 0; i < items->second.get<JArr>().size(); ++i)
		{
			if(!items->second.get<JArr>()[i].is<JObj>()) continue;
			const JObj& it = items->second.get<JArr>()[i].get<JObj>();
			std::string kind = ArgStr(it, "item"), what;
			int amount = ArgInt(it, "amount", 0);
			if(kind == "resource") what = ArgStr(it, "resource") + McpFormat(" %d", amount);
			else if(kind == "gold_per_turn") what = McpFormat("%d gold a turn", amount);
			else if(kind == "gold") what = McpFormat("%d gold", amount);
			else
			{
				what = kind;
				for(size_t k = 0; k < what.size(); ++k) if(what[k] == '_') what[k] = ' ';
			}
			std::string& side = ArgStr(it, "given_by") == me ? give : get;
			side += (side.empty() ? "" : ", ") + what;
		}
		std::string out;
		if(!give.empty()) out += "I give " + give;
		if(!get.empty()) out += (out.empty() ? "" : "; ") + std::string("they give ") + get;
		return out;
	}

	// What the seat can know of the world now: the majors it has met, their wars and ties, their cities it has seen,
	// and its own deals.
	McpWorld World(CvPlayer* me)
	{
		McpWorld w;
		w.turn = GC.getGame().getGameTurn();
		w.me = CivName(me->GetID());
		CvTeam& mine = GET_TEAM(me->getTeam());
		std::vector<PlayerTypes> known;
		for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || p.isMinorCiv() || p.isBarbarian()) continue;
			if(i != (int)me->GetID() && !mine.isHasMet(p.getTeam())) continue;
			known.push_back((PlayerTypes)i);
			w.players.insert(CivName((PlayerTypes)i));
		}
		for(size_t a = 0; a < known.size(); ++a)
			for(size_t b = a + 1; b < known.size(); ++b)
			{
				CvPlayer &pa = GET_PLAYER(known[a]), &pb = GET_PLAYER(known[b]);
				if(pa.getTeam() == pb.getTeam()) continue;
				std::string na = CivName(known[a]), nb = CivName(known[b]);
				std::pair<int, int> teams((int)std::min(pa.getTeam(), pb.getTeam()), (int)std::max(pa.getTeam(), pb.getTeam()));
				if(GET_TEAM(pa.getTeam()).isAtWar(pb.getTeam()))
				{
					std::map<std::pair<int, int>, int>::const_iterator by = s_declared.find(teams);
					w.wars[McpWorldPair(na, nb)] = by == s_declared.end() ? std::string() : by->second == (int)pa.getTeam() ? na : nb;
				}
				else s_declared.erase(teams);
				if(pa.GetDiplomacyAI()->IsDoFAccepted(known[b]) || pb.GetDiplomacyAI()->IsDoFAccepted(known[a])) w.friends.insert(McpWorldPair(na, nb));
				if(GET_TEAM(pa.getTeam()).IsHasDefensivePact(pb.getTeam())) w.pacts.insert(McpWorldPair(na, nb));
				if(pa.GetDiplomacyAI()->IsDenouncedPlayer(known[b])) w.denounced.insert(std::make_pair(na, nb));
				if(pb.GetDiplomacyAI()->IsDenouncedPlayer(known[a])) w.denounced.insert(std::make_pair(nb, na));
			}
		// cities, by the tile they stand on: a city keeps its tile whoever holds it
		for(int i = 0; i < MAX_CIV_PLAYERS; ++i)
		{
			CvPlayer& p = GET_PLAYER((PlayerTypes)i);
			if(!p.isAlive() || p.isBarbarian()) continue;
			bool bMine = i == (int)me->GetID();
			if(!bMine && !mine.isHasMet(p.getTeam())) continue;
			int iLoop;
			for(CvCity* c = p.firstCity(&iLoop); c; c = p.nextCity(&iLoop))
			{
				if(!bMine && !c->plot()->isRevealed(me->getTeam())) continue;
				McpWorldCity wc;
				wc.name = c->getName().c_str();
				wc.owner = CivName((PlayerTypes)i);
				wc.firstOwner = c->getOriginalOwner() == NO_PLAYER ? wc.owner : CivName(c->getOriginalOwner());
				wc.firstCapital = c->IsOriginalMajorCapital();
				wc.founded = c->getGameTurnFounded();
				if(!bMine)
				{
					int iMine;
					for(CvCity* m = me->firstCity(&iMine); m; m = me->nextCity(&iMine))
					{
						int d = plotDistance(c->getX(), c->getY(), m->getX(), m->getY());
						if(wc.nearTiles < 0 || d < wc.nearTiles) { wc.nearTiles = d; wc.nearCity = m->getName().c_str(); }
					}
				}
				w.cities[c->plot()->GetPlotIndex()] = wc;
			}
		}
		std::string err;
		JVal diplomacy = ToolDiplomacy(JObj(), err);
		if(diplomacy.is<JObj>())
		{
			JObj::const_iterator deals = diplomacy.get<JObj>().find("current_deals");
			if(deals != diplomacy.get<JObj>().end() && deals->second.is<JArr>())
				for(size_t i = 0; i < deals->second.get<JArr>().size(); ++i)
				{
					if(!deals->second.get<JArr>()[i].is<JObj>()) continue;
					const JObj& d = deals->second.get<JArr>()[i].get<JObj>();
					JObj::const_iterator di = d.find("deal");
					const JObj& deal = di != d.end() && di->second.is<JObj>() ? di->second.get<JObj>() : d;
					std::string from = ArgStr(deal, "from"), to = ArgStr(deal, "to"), terms = DealTerms(deal, w.me);
					if(terms.empty() || (from != w.me && to != w.me)) continue;
					w.deals[from == w.me ? to : from].insert(terms);
				}
		}
		return w;
	}

	bool SameMoment(const McpWorld& a, McpWorld b)
	{
		b.turn = a.turn;
		return McpWorldToJson(a).serialize() == McpWorldToJson(b).serialize();
	}

	bool HasCity(CvPlayer* me, const std::string& name)
	{
		int iLoop;
		for(CvCity* c = me->firstCity(&iLoop); c; c = me->nextCity(&iLoop))
			if(MatchesKey("", c->getName().c_str(), name)) return true;
		return false;
	}

	std::map<int, McpVowFact> VowFacts(CvPlayer* me, const McpChronicle& c)
	{
		std::map<int, McpVowFact> facts;
		for(size_t i = 0; i < c.threads.size(); ++i)
		{
			const McpThread& t = c.threads[i];
			if(t.ended >= 0 || t.vow.empty() || !t.vowState.empty()) continue;
			McpVowFact f;
			PlayerTypes eThem = FindPlayer(t.with);
			bool bThem = eThem != NO_PLAYER && eThem != me->GetID() && GET_PLAYER(eThem).isAlive();
			bool bWar = bThem && GET_TEAM(me->getTeam()).isAtWar(GET_PLAYER(eThem).getTeam());
			if(t.vow == "war") f.met = bWar;
			else if(t.vow == "peace")
			{
				std::pair<int, int> teams(0, 0);
				if(bThem) teams = std::make_pair((int)std::min(me->getTeam(), GET_PLAYER(eThem).getTeam()), (int)std::max(me->getTeam(), GET_PLAYER(eThem).getTeam()));
				std::map<std::pair<int, int>, int>::const_iterator by = s_declared.find(teams);
				f.failed = bWar && by != s_declared.end() && by->second == (int)me->getTeam();
			}
			else if(t.vow == "pact") f.met = bThem && GET_TEAM(me->getTeam()).IsHasDefensivePact(GET_PLAYER(eThem).getTeam());
			else if(t.vow == "deal")
			{
				for(size_t k = 0; k < c.deeds.size(); ++k)
					if(c.deeds[k].with == t.with && c.deeds[k].turn >= t.opened && c.deeds[k].text.compare(0, 14, "we made a deal") == 0) f.met = true;
			}
			else if(t.vow == "take") f.met = HasCity(me, t.subject);
			else if(t.vow == "hold") f.failed = !HasCity(me, t.subject);
			facts[t.id] = f;
		}
		return facts;
	}

	McpChronicleNow Now(CvPlayer* me, const McpWorld& w, const std::string& direction)
	{
		McpChronicleNow now;
		now.turn = GC.getGame().getGameTurn();
		now.era = (int)GET_TEAM(me->getTeam()).GetCurrentEra();
		now.eliminations = Eliminations();
		now.citiesLost = me->getCitiesLost();
		now.direction = direction;
		for(std::set<std::string>::const_iterator it = w.players.begin(); it != w.players.end(); ++it) if(*it != w.me) now.players.push_back(*it);
		return now;
	}
}

bool McpChronicleOn(CvPlayer* me) { return me && Major(me) && !McpLogsFlag("MCPServer-chronicle.off"); }

std::string McpWhoYouAre(CvPlayer* me) { return McpChronicleOn(me) ? Seed(me).page : std::string(); }

void McpChronicleSync(CvPlayer* me)
{
	if(!McpChronicleOn(me) || Seed(me).page.empty()) return;
	Seat& s = SeatOf(me);
	McpChronicle& c = s.chronicle;
	int iTurn = GC.getGame().getGameTurn();
	if(!c.started)
		McpChronicleStart(c, iTurn, (int)GET_TEAM(me->getTeam()).GetCurrentEra(), Eliminations(), me->getCitiesLost(), Seed(me).faults, Seed(me).unwritten);
	McpWorld now = World(me);
	std::vector<std::string> gone;
	std::vector<McpWorldDeed> deeds = McpWorldDeeds(s.world, now, gone);
	for(size_t i = 0; i < deeds.size(); ++i)
	{
		// filed only under players the seat keeps company with: the ones in its world now, and the ones that just left
		bool bGone = std::find(gone.begin(), gone.end(), deeds[i].with) != gone.end();
		if(now.players.count(deeds[i].with) || bGone) McpChronicleDeed(c, iTurn, deeds[i].with, deeds[i].text);
	}
	// An entry on a name the seat cannot write under is owed nothing. A civilization that is out of the game keeps its
	// entry, as one that just left does; an entry on anything else (a city-state, a name never met) goes.
	for(std::map<std::string, McpRelation>::const_iterator it = c.relations.begin(); it != c.relations.end(); ++it)
	{
		if(now.players.count(it->first) || c.gone.count(it->first)) continue;
		PlayerTypes eThem = FindPlayer(it->first);
		if(eThem != NO_PLAYER && (int)eThem < MAX_MAJOR_CIVS && !GET_PLAYER(eThem).isAlive()) gone.push_back(it->first);
	}
	for(size_t i = 0; i < gone.size(); ++i) McpChronicleForget(c, gone[i]);
	McpChronicleDropUnknown(c, now.players);
	if(s.world.turn < 0 || !SameMoment(s.world, now))
	{
		s.world = now;
		McpGameFileAppend(s_game, "chronicle", CivFile(me, ".world.jsonl"), "", McpWorldToJson(now).serialize() + "\n");
	}
	McpVowsResolve(c, VowFacts(me, c), iTurn);
	Flush(me, c);
}

bool McpChroniclePlanTurn(CvPlayer* me, const picojson::object& args, const std::string& direction, McpChroniclePlan& out)
{
	out = McpChroniclePlan();
	if(!McpChronicleOn(me) || Seed(me).page.empty()) return true;
	McpChronicleSync(me);
	Seat& s = SeatOf(me);
	s.pending = s.chronicle;
	s.hasPending = false;
	McpChronicleResult r;
	if(!McpChronicleApply(s.pending, args, Now(me, s.world, direction), r))
	{
		out.refusals = r.refusals;
		return false;
	}
	s.hasPending = true;
	s.pendingSay = r.say;
	s.pendingJournal = r.journal;
	JObj::const_iterator plan = args.find("plan");
	out.relationsGiven = plan != args.end() && plan->second.is<JObj>() && plan->second.get<JObj>().count("relations") > 0;
	out.relationsText = McpChronicleRelationsText(s.pending);
	out.chapterClosed = r.chapterClosed;
	out.direction = r.direction;
	out.say = r.say;
	if(r.chapterClosed) out.why = "closed the chapter \"" + s.pending.chapters.back().title + "\"";
	else if(out.relationsGiven) out.why = "relations written again";
	return true;
}

void McpChronicleCommit(CvPlayer* me)
{
	if(!McpChronicleOn(me)) return;
	Seat& s = SeatOf(me);
	if(!s.hasPending) return;
	s.hasPending = false;
	s.chronicle = s.pending;
	s.chronicle.journal.insert(s.chronicle.journal.end(), s.pendingJournal.begin(), s.pendingJournal.end());
	Flush(me, s.chronicle);
	for(size_t i = 0; i < s.pendingSay.size(); ++i) McpSayQueue(me, s.pendingSay[i].first, s.pendingSay[i].second);
	s.pendingSay.clear();
	s.pendingJournal.clear();
}

std::string McpChronicleViewBlock(CvPlayer* me)
{
	if(!McpChronicleOn(me) || Seed(me).page.empty()) return "";
	McpChronicleSync(me);
	Seat& s = SeatOf(me);
	int iTurn = GC.getGame().getGameTurn();
	std::string due = McpChronicleDue(s.chronicle, (int)GET_TEAM(me->getTeam()).GetCurrentEra(), Eliminations(), me->getCitiesLost(), iTurn);
	return McpChronicleBlock(s.chronicle, me->getCivilizationShortDescription(), Seed(me).page, iTurn, due);
}

std::vector<std::string> McpChronicleMirrorFor(CvPlayer* me, const std::vector<std::string>& serves, const std::vector<std::string>& said)
{
	std::vector<std::string> none;
	if(!McpChronicleOn(me) || Seed(me).page.empty()) return none;
	Seat& s = SeatOf(me);
	if(!s.chronicle.started) return none;
	// a player is served when an order's serves names it, or a line went to it or to the whole table
	std::set<std::string> names;
	for(std::map<std::string, McpRelation>::const_iterator it = s.chronicle.relations.begin(); it != s.chronicle.relations.end(); ++it) names.insert(it->first);
	for(size_t i = 0; i < s.chronicle.threads.size(); ++i) if(s.chronicle.threads[i].ended < 0) names.insert(s.chronicle.threads[i].with);
	std::vector<std::string> served;
	for(std::set<std::string>::const_iterator n = names.begin(); n != names.end(); ++n)
	{
		bool hit = false;
		for(size_t i = 0; i < serves.size() && !hit; ++i) hit = serves[i].find(*n) != std::string::npos;
		for(size_t i = 0; i < said.size() && !hit; ++i) hit = said[i] == "all" || said[i].find(*n) != std::string::npos;
		if(hit) served.push_back(*n);
	}
	std::vector<std::string> lines = McpChronicleMirror(s.chronicle, served, GC.getGame().getGameTurn());
	Flush(me, s.chronicle);
	return lines;
}

void McpSayQueue(CvPlayer* me, const std::string& to, const std::string& words)
{
	SameGame();
	std::string err;
	McpChatSay((int)me->GetID(), to, words, "", err);
	if(!err.empty()) McpGameFileAppend(s_game, "log", CivFile(me, ".log"), "", McpFormat("turn %d: not said to %s (%s): ", GC.getGame().getGameTurn(), to.c_str(), err.c_str()) + words + "\n");
}

void McpChronicleNoteWar(int declaringPlayer, int onTeam)
{
	if(declaringPlayer < 0 || declaringPlayer >= MAX_CIV_PLAYERS || onTeam < 0) return;
	int by = (int)GET_PLAYER((PlayerTypes)declaringPlayer).getTeam();
	if(by == onTeam) return;
	s_declared[std::make_pair(std::min(by, onTeam), std::max(by, onTeam))] = by;
}

picojson::value McpChronicleToolView(CvPlayer* me)
{
	McpChronicleSync(me);
	Seat& s = SeatOf(me);
	JVal v = McpChronicleFull(s.chronicle);
	if(v.is<JObj>()) v.get<JObj>()["first_page"] = J(Seed(me).page);
	return v;
}

void McpChronicleReset()
{
	s_game.clear();
	s_seeds.clear();
	s_seats.clear();
	s_declared.clear();
}
