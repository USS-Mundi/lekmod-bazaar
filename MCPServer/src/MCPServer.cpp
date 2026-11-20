#include "CvGameCoreDLLPCH.h"

#include "CivHooks.h"
#include "CivPluginApi.h"
#ifdef MCP_LEKMOD
// Under LEKMOD for Bazaar these two hook points carry their parameter types in their names: our hook-id registry was
// seeded from Bazaar's, where the same functions have VMC's signatures, so LEKMOD's got names of their own.
namespace CivHooks
{
	typedef CvDiplomacyRequests_SendRequest_with_PlayerTypes_PlayerTypes_DiploUIStateTypes_const_char_LeaderheadAnimationTypes_int CvDiplomacyRequests_SendRequest;
	typedef CvDiplomacyRequests_SendDealRequest_with_PlayerTypes_PlayerTypes_CvDeal_DiploUIStateTypes_const_char_LeaderheadAnimationTypes CvDiplomacyRequests_SendDealRequest;
}
#endif

#include "CvDealAI.h"
#include "CvMinorCivAI.h"
#include "CvTeam.h"
#include "CvUnit.h"
#include "CvDllPlayer.h"
#include "CvDllPreGame.h"
#include "CvDllContext.h"
#include "McpBridge.h"
#include "McpCityStates.h"
#include "McpCityStateRules.h"
#include "CvTreasury.h"
#include "McpCityStateFiles.h"
#include "McpCrashLog.h"
#include "McpToolSupport.h"
#include "McpDiplomacy.h"
#include "McpChronicleGame.h"
#include "McpLua.h"
#include "McpMapDraw.h"
#include "McpSeat.h"
#include "McpVersion.h"

#include <stdarg.h>
#include <stdio.h>

#define PLUGIN_EXPORT extern "C" __declspec(dllexport)

CIV_PLUGIN_EXPORT_API_VERSION;

static const CivHostInfo* s_host = 0;

// Writes a line to Logs\Bazaar.log, headed with this plugin's name.
static void Log(const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	s_host->Log(s_host->pluginIndex, line);
}

static void Check(int result, const char* hook)
{
	if (result != CIV_HOOK_OK)
		Log("could not register %s (result %d)", hook, result);
}

// The pathfinder hooks below are entered from more than one thread; how often two are inside at once is reported here,
// on the game thread, while the count keeps rising (at most every 30 s).
static void ReportPathHookOverlaps()
{
	static long s_reported = 0;
	static DWORD s_reportedAt = 0;
	long overlaps = McpPathHookOverlaps();
	if(overlaps == s_reported || GetTickCount() - s_reportedAt < 30000) return;
	s_reported = overlaps;
	s_reportedAt = GetTickCount();
	Log("pathfinder hooks: %ld calls so far entered while another thread was inside; at most %ld threads at once", overlaps, McpPathHookMostAtOnce());
}

static bool BeforeGameUpdate(CvGame* self)
{
	McpBridge::Get().Tick();
	ReportPathHookOverlaps();
	CvDllGameContext* pContext = CvDllGameContext::GetSingleton();
	if(pContext) McpHeapWatchStart(pContext->GetHeap());
	return false;
}

static void AfterGameRead(CvGame* self, FDataStream& kStream)
{
	McpCrashLogInstall();
	McpTrail("game loaded");
	McpBridge::Get().NoteGameLoaded();
}

static void AfterDllPlayerIsHuman(const CvDllPlayer* self, bool& result)
{
	result = result && McpEngineSeesAsHuman(self->GetID());
}

static void AfterDllPreGameIsHuman(CvDllPreGame* self, PlayerTypes ePlayer, bool& result)
{
	result = result && McpEngineSeesAsHuman(ePlayer);
}

#ifdef MCP_LEKMOD
static bool BeforeSendRequest(PlayerTypes eFrom, PlayerTypes eTo, DiploUIStateTypes eState, const char* szMessage, LeaderheadAnimationTypes, int iExtraGameData)
{
	return McpInterceptRequest(eFrom, eTo, eState, szMessage, iExtraGameData);
}
#else
static bool BeforeSendRequest(PlayerTypes eFrom, PlayerTypes eTo, DiploUIStateTypes eState, DiploMessageTypes eMessage, const char* szMessage, LeaderheadAnimationTypes eAnimation, int iExtraGameData)
{
	return McpInterceptRequest(eFrom, eTo, eState, szMessage, iExtraGameData);
}
#endif

#ifdef MCP_LEKMOD
static bool BeforeSendDealRequest(PlayerTypes eFrom, PlayerTypes eTo, CvDeal* pDeal, DiploUIStateTypes eState, const char* szMessage, LeaderheadAnimationTypes)
{
	return McpInterceptDealRequest(eFrom, eTo, pDeal, eState, szMessage);
}
#else
static bool BeforeSendDealRequest(PlayerTypes eFrom, PlayerTypes eTo, CvDeal* pDeal, DiploUIStateTypes eState, DiploMessageTypes eMessage, const char* szMessage, LeaderheadAnimationTypes eAnimation)
{
	return McpInterceptDealRequest(eFrom, eTo, pDeal, eState, szMessage);
}
#endif

static bool BeforeEqualizeDeal(CvDealAI* self, CvDeal* pDeal, PlayerTypes eOtherPlayer, bool bDontChangeMyExistingItems, bool bDontChangeTheirExistingItems, bool& bDealGoodToBeginWith, bool& bCantMatchOffer, bool& result)
{
	McpBeginEqualizing(eOtherPlayer);
	return false;
}

static void AfterEqualizeDeal(CvDealAI* self, CvDeal* pDeal, PlayerTypes eOtherPlayer, bool bDontChangeMyExistingItems, bool bDontChangeTheirExistingItems, bool& bDealGoodToBeginWith, bool& bCantMatchOffer, bool& result)
{
	McpEndEqualizing();
}

static bool ReplaceIsDealWithHumanAcceptable(CivHooks::CvDealAI_IsDealWithHumanAcceptable::Original original, CvDealAI* self, CvDeal* pDeal, PlayerTypes eOtherPlayer, int& iTotalValueToMe, int& iValueImOffering, int& iValueTheyreOffering, int& iAmountOverWeWillRequest, int& iAmountUnderWeWillOffer, bool& bCantMatchOffer)
{
	return original(self, pDeal, McpEqualizingWith(eOtherPlayer), iTotalValueToMe, iValueImOffering, iValueTheyreOffering, iAmountOverWeWillRequest, iAmountUnderWeWillOffer, bCantMatchOffer);
}

// What each civ does for and to a city-state is recorded for whoever plays it later.
static PlayerTypes s_allyBefore = NO_PLAYER;
static bool BeforeSetAlly(CvMinorCivAI* self, PlayerTypes)
{
	s_allyBefore = self->GetAlly();
	return false;
}

static void AfterSetAlly(CvMinorCivAI* self, PlayerTypes eNewAlly)
{
	PlayerTypes eMinor = self->GetPlayer()->GetID();
	if(s_allyBefore != NO_PLAYER && s_allyBefore != eNewAlly)
		McpCsNoteDeed(eMinor, s_allyBefore, eNewAlly == NO_PLAYER ? std::string("lost their alliance") : "lost their alliance to " + McpTool::CivName(eNewAlly));
	if(eNewAlly != NO_PLAYER && eNewAlly != s_allyBefore) McpCsNoteDeed(eMinor, eNewAlly, "became their ally");
	McpTrail("city-state ally changed", eMinor, eNewAlly);
	McpCsAfterSetAlly(eMinor, eNewAlly);
	McpCsPeaceWithSameSide(eMinor);
}

// Gold a city-state is given goes into its treasury, and tribute comes out of it: the game moves neither. What moved is
// read from the major's treasury, so a gift the city-state refused is neither banked nor recorded.
static int s_majorGoldBefore = 0;
static bool BeforeMinorGold(CvMinorCivAI*, PlayerTypes eMajor, int)
{
	s_majorGoldBefore = (eMajor >= 0 && eMajor < MAX_MAJOR_CIVS) ? GET_PLAYER(eMajor).GetTreasury()->GetGold() : 0;
	return false;
}

static void AfterGoldGift(CvMinorCivAI* self, PlayerTypes eMajor, int)
{
	if(eMajor < 0 || eMajor >= MAX_MAJOR_CIVS) return;
	int iMoved = McpCsGoldMoved(s_majorGoldBefore, GET_PLAYER(eMajor).GetTreasury()->GetGold());
	if(iMoved <= 0) return;
	CvTreasury* pTreasury = self->GetPlayer()->GetTreasury();
	int iBefore = pTreasury->GetGold();
	pTreasury->ChangeGold(iMoved);
	McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, McpCsGiftDeed(iMoved, iBefore));
}

// The gift can destroy the unit (a gifted great person is killed at once), so its type is read before.
static UnitTypes s_giftType = NO_UNIT;
static bool BeforeUnitGift(CvMinorCivAI*, PlayerTypes, CvUnit* pUnit, bool)
{
	s_giftType = pUnit ? pUnit->getUnitType() : NO_UNIT;
	return false;
}

static void AfterUnitGift(CvMinorCivAI* self, PlayerTypes eMajor, CvUnit*, bool)
{
	McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, std::string("gave them a unit") + (s_giftType != NO_UNIT ? std::string(" (") + McpTool::UnitTypeName(s_giftType) + ")" : std::string()));
	s_giftType = NO_UNIT;
}

static void AfterImprovementGift(CvMinorCivAI* self, PlayerTypes eMajor, int iX, int iY)
{
	McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, McpFormat("built an improvement for them at (%d,%d)", iX, iY));
}

static void AfterBullyGold(CvMinorCivAI* self, PlayerTypes eMajor, int)
{
	if(eMajor < 0 || eMajor >= MAX_MAJOR_CIVS) return;
	int iTaken = GET_PLAYER(eMajor).GetTreasury()->GetGold() - s_majorGoldBefore;
	if(iTaken <= 0)
	{
		McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, "demanded gold from them by threat and got none");
		return;
	}
	CvTreasury* pTreasury = self->GetPlayer()->GetTreasury();
	int iBefore = pTreasury->GetGold();
	int iPaid = McpCsTributePaid(iBefore, iTaken);
	pTreasury->ChangeGold(-iPaid);
	McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, McpCsTributeDeed(iTaken, iPaid, iBefore));
}

static void AfterBullyUnit(CvMinorCivAI* self, PlayerTypes eMajor, UnitTypes eUnit)
{
	McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, std::string("took a ") + McpTool::UnitTypeName(eUnit) + " from them by threat");
}

// A city-state that dies cancels the pledge of every civ slot, so a pledge is a deed only when it changed.
static bool s_protectedBefore = false;
static bool BeforeProtection(CvMinorCivAI* self, PlayerTypes eMajor, bool, bool)
{
	s_protectedBefore = eMajor >= 0 && eMajor < MAX_MAJOR_CIVS && self->IsProtectedByMajor(eMajor);
	return false;
}

static void AfterProtection(CvMinorCivAI* self, PlayerTypes eMajor, bool, bool)
{
	if(eMajor < 0 || eMajor >= MAX_MAJOR_CIVS) return;
	std::string deed = McpCsPledgeDeed(s_protectedBefore, self->IsProtectedByMajor(eMajor));
	if(!deed.empty()) McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, deed);
}

static void AfterWarOnMinor(CvMinorCivAI* self, TeamTypes eTeam)
{
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i)
		if(GET_PLAYER((PlayerTypes)i).isAlive() && GET_PLAYER((PlayerTypes)i).getTeam() == eTeam) McpCsNoteDeed(self->GetPlayer()->GetID(), (PlayerTypes)i, "declared war on them");
}

#ifdef MCP_LEKMOD
static void AfterBuyout(CvMinorCivAI* self, PlayerTypes eMajor)
#else
static void AfterBuyout(CvMinorCivAI* self, PlayerTypes eMajor, bool)
#endif
{
	McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, "bought them out");
}

// A quest finished during the game's completed-quests pass is one active before it and gone after it.
static bool s_questsBefore[MAX_MAJOR_CIVS][NUM_MINOR_CIV_QUEST_TYPES];
static void SnapshotQuests(CvMinorCivAI* self, PlayerTypes eMajor)
{
	for(int q = 0; q < NUM_MINOR_CIV_QUEST_TYPES; ++q) s_questsBefore[eMajor][q] = self->IsActiveQuestForPlayer(eMajor, (MinorCivQuestTypes)q);
}

static void NoteQuestsDone(CvMinorCivAI* self, PlayerTypes eMajor)
{
	for(int q = 0; q < NUM_MINOR_CIV_QUEST_TYPES; ++q)
		if(s_questsBefore[eMajor][q] && !self->IsActiveQuestForPlayer(eMajor, (MinorCivQuestTypes)q))
			McpCsNoteDeed(self->GetPlayer()->GetID(), eMajor, std::string("completed their quest: ") + McpMinorQuestName(q));
}

#ifdef MCP_LEKMOD
// LEKMOD's SDK hooks the pass over every player, not the per-player one.
static bool BeforeQuestsDone(CvMinorCivAI* self)
{
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i) SnapshotQuests(self, (PlayerTypes)i);
	return false;
}

static void AfterQuestsDone(CvMinorCivAI* self)
{
	McpTrail("city-state quests checked", self->GetPlayer()->GetID());
	for(int i = 0; i < MAX_MAJOR_CIVS; ++i) NoteQuestsDone(self, (PlayerTypes)i);
}
#else
static bool BeforeQuestsDone(CvMinorCivAI* self, PlayerTypes eMajor, MinorCivQuestTypes)
{
	SnapshotQuests(self, eMajor);
	return false;
}

static void AfterQuestsDone(CvMinorCivAI* self, PlayerTypes eMajor, MinorCivQuestTypes)
{
	McpTrail("city-state quests checked", self->GetPlayer()->GetID(), eMajor);
	NoteQuestsDone(self, eMajor);
}
#endif

static void AfterMinorTurn(CvMinorCivAI* self)
{
	McpTrail("city-state turn: plugin begins", self->GetPlayer()->GetID());
	McpCsAfterMinorTurn(self->GetPlayer()->GetID());
	McpTrail("city-state turn: plugin done", self->GetPlayer()->GetID());
}

// The crash trail through the AI's part of the turn, where every crash so far came: each player's turn and unit
// moves, the barbarians' units, and each unit stepping onto a tile, so a report names whose move it died in.
static bool BeforePlayerTurn(CvPlayer* self) { McpTrail("player turn begins", self->GetID()); return false; }
static void AfterPlayerTurn(CvPlayer* self) { McpTrail("player turn done", self->GetID()); }
static bool BeforePlayerTurnUnits(CvPlayer* self) { McpTrail("player units' turn begins", self->GetID()); return false; }
static void AfterPlayerTurnUnits(CvPlayer* self) { McpTrail("player units' turn done", self->GetID()); }
static bool BeforeBarbarianUnits() { McpTrail("barbarian units begin"); return false; }
static void AfterBarbarianUnits() { McpTrail("barbarian units done"); }

// The AI moves its units many times a turn; only a change of player is recorded, so the trail keeps its reach.
static int s_lastUnitUpdate = -1;
static bool BeforeAIUnitUpdate(CvPlayerAI* self)
{
	if(self->GetID() != s_lastUnitUpdate) McpTrail("AI moving units of player", self->GetID());
	s_lastUnitUpdate = self->GetID();
	return false;
}

static bool TrailBeforeUnitSetXY(CvUnit* self, int x, int y, bool, bool, bool, bool, bool)
{
	McpTrail("unit to tile (id, x*1000+y)", self->GetID(), x * 1000 + y);
	return false;
}

// Counts the pathfinder hooks' calls that overlap across threads, for the overlap report and a crash report.
struct PathHookCall
{
	PathHookCall() { McpPathHookEnter(); }
	~PathHookCall() { McpPathHookLeave(); }
};

// A switched-on city-state's units may enter its ally's land (from the influence step that allows it). Both border
// checks the pathfinder uses answer alike, or paths and moves disagree. The first test keeps the hot path cheap.
static bool ReplaceCanEnterTerritory(CivHooks::CvUnit_canEnterTerritory::Original original, const CvUnit* self, TeamTypes eTeam, bool bIgnoreRightOfPassage, bool bIsCity, bool bIsDeclareWarMove)
{
	PathHookCall call;
	if(McpCsAnySwitchedOn() && McpCsIsSwitchedOn(self->getOwner()) && McpCsMayEnter(self->getOwner(), eTeam)) return true;
	if(McpCsTeamsSameSide(self->getTeam(), eTeam)) return true;
	return original(self, eTeam, bIgnoreRightOfPassage, bIsCity, bIsDeclareWarMove);
}

static bool ReplaceAllowsOpenBorders(CivHooks::CvTeam_IsAllowsOpenBordersToTeam::Original original, const CvTeam* self, TeamTypes eTeam)
{
	PathHookCall call;
	if(McpCsAnySwitchedOn() && McpCsTeamSwitchedOn(eTeam) && McpCsTeamMayEnter(eTeam, self->GetID())) return true;
	if(McpCsTeamsSameSide(eTeam, self->GetID())) return true;
	return original(self, eTeam);
}

// What a switched-on city-state's units see stays uncovered on its ally's map; a unit sharing live sight takes it along.
static void AfterUnitSetXY(CvUnit* self, int, int, bool, bool, bool, bool, bool)
{
	McpTrail("unit on tile (owner, id)", self->getOwner(), self->GetID());
	if(!McpCsAnySwitchedOn() || !McpCsIsSwitchedOn(self->getOwner())) return;
	McpCsRevealAround(self);
	McpCsSightAfterMove(self);
}

static bool BeforeUnitKill(CvUnit* self, bool, PlayerTypes)
{
	McpCsSightBeforeKill(self);
	return false;
}

static void AfterRegisterGame(lua_State* L)
{
	McpAddGameMethods(s_host, L);
}

static void UnitOrdersCancelled(CvUnit* unit, int reason)
{
	McpNoteOrdersCancelled(unit, (CivOrdersCancelledReason)reason);
}

static bool IsPluginControlledPlayer(int player)
{
	return McpIsSeat((PlayerTypes)player);
}

static bool KeepDeadSlotOpen(int player)
{
	return McpKeepDeadSlotOpen((PlayerTypes)player);
}

static void PopupRaised(int player, int popupType, int data1, int data2, int data3, bool option1, bool option2, const char* text)
{
	McpNoteSeatPopup((PlayerTypes)player, popupType, data1, data2, data3, option1, text);
}

static void NotificationAdded(int player, int notificationType, const char* message, const char* summary, int x, int y, int data1, int data2, int lookupIndex)
{
	McpNoteSeatNotification((PlayerTypes)player, message, summary);
	// The game tells the one declared on (the declarer in data1) and everyone who has met both (the team declared on in data2).
	if(notificationType == NOTIFICATION_WAR_ACTIVE_PLAYER && player >= 0 && player < MAX_CIV_PLAYERS)
		McpChronicleNoteWar(data1, (int)GET_PLAYER((PlayerTypes)player).getTeam());
	else if(notificationType == NOTIFICATION_WAR) McpChronicleNoteWar(data1, data2);
}

static void DealResolved(int fromPlayer, int toPlayer, int actingPlayer, bool accepted, bool finalized)
{
	McpNoteDealResolved((PlayerTypes)fromPlayer, (PlayerTypes)toPlayer, (PlayerTypes)actingPlayer, accepted, finalized);
}

PLUGIN_EXPORT bool CivPluginEntry(const CivHostInfo* host, CivPluginInfo* plugin)
{
	s_host = host;
	McpCrashLogInstall();
	// VC++ 2008 doesn't guard a function's static, and the game and UI threads could both build it on first use.
	McpBridge::Get();
	plugin->name = "MCP Server for Bazaar";
	plugin->version = MCP_SERVER_VERSION;
	Check(CivRegisterBefore<CivHooks::CvGame_update>(host, &BeforeGameUpdate), "before CvGame::update");
	Check(CivRegisterAfter<CivHooks::CvGame_Read>(host, &AfterGameRead), "after CvGame::Read");
	Check(host->RegisterHook(host->pluginIndex, CivHooks::CvDllPlayer_IsHuman::ID, CIV_HOOK_AFTER, (void*)&AfterDllPlayerIsHuman), "after CvDllPlayer::IsHuman");
	Check(host->RegisterHook(host->pluginIndex, CivHooks::CvDllPreGame_isHuman::ID, CIV_HOOK_AFTER, (void*)&AfterDllPreGameIsHuman), "after CvDllPreGame::isHuman");
	Check(CivRegisterBefore<CivHooks::CvDiplomacyRequests_SendRequest>(host, &BeforeSendRequest), "before CvDiplomacyRequests::SendRequest");
	Check(CivRegisterBefore<CivHooks::CvDiplomacyRequests_SendDealRequest>(host, &BeforeSendDealRequest), "before CvDiplomacyRequests::SendDealRequest");
	Check(CivRegisterBefore<CivHooks::CvDealAI_DoEqualizeDealWithHuman>(host, &BeforeEqualizeDeal), "before CvDealAI::DoEqualizeDealWithHuman");
	Check(CivRegisterAfter<CivHooks::CvDealAI_DoEqualizeDealWithHuman>(host, &AfterEqualizeDeal), "after CvDealAI::DoEqualizeDealWithHuman");
	Check(CivRegisterReplace<CivHooks::CvDealAI_IsDealWithHumanAcceptable>(host, &ReplaceIsDealWithHumanAcceptable), "replace CvDealAI::IsDealWithHumanAcceptable");
	Check(CivRegisterAfter<CivHooks::CvLuaGame_RegisterMembers>(host, &AfterRegisterGame), "after CvLuaGame::RegisterMembers");
	const bool bCityStates = !McpCityStatesOffAtStart();
	if(!bCityStates) Log("city-state hooks off: MCPServer-city-states-off is in My Games\\Sid Meier's Civilization 5");
	if(bCityStates)
	{
		Check(CivRegisterBefore<CivHooks::CvMinorCivAI_SetAlly>(host, &BeforeSetAlly), "before CvMinorCivAI::SetAlly");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_SetAlly>(host, &AfterSetAlly), "after CvMinorCivAI::SetAlly");
		Check(CivRegisterBefore<CivHooks::CvMinorCivAI_DoGoldGiftFromMajor>(host, &BeforeMinorGold), "before CvMinorCivAI::DoGoldGiftFromMajor");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoGoldGiftFromMajor>(host, &AfterGoldGift), "after CvMinorCivAI::DoGoldGiftFromMajor");
		Check(CivRegisterBefore<CivHooks::CvMinorCivAI_DoUnitGiftFromMajor>(host, &BeforeUnitGift), "before CvMinorCivAI::DoUnitGiftFromMajor");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoUnitGiftFromMajor>(host, &AfterUnitGift), "after CvMinorCivAI::DoUnitGiftFromMajor");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoTileImprovementGiftFromMajor>(host, &AfterImprovementGift), "after CvMinorCivAI::DoTileImprovementGiftFromMajor");
		Check(CivRegisterBefore<CivHooks::CvMinorCivAI_DoMajorBullyGold>(host, &BeforeMinorGold), "before CvMinorCivAI::DoMajorBullyGold");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoMajorBullyGold>(host, &AfterBullyGold), "after CvMinorCivAI::DoMajorBullyGold");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoMajorBullyUnit>(host, &AfterBullyUnit), "after CvMinorCivAI::DoMajorBullyUnit");
		Check(CivRegisterBefore<CivHooks::CvMinorCivAI_DoChangeProtectionFromMajor>(host, &BeforeProtection), "before CvMinorCivAI::DoChangeProtectionFromMajor");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoChangeProtectionFromMajor>(host, &AfterProtection), "after CvMinorCivAI::DoChangeProtectionFromMajor");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoTeamDeclaredWarOnMe>(host, &AfterWarOnMinor), "after CvMinorCivAI::DoTeamDeclaredWarOnMe");
#ifdef MCP_LEKMOD
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoBuyout_with_PlayerTypes>(host, &AfterBuyout), "after CvMinorCivAI::DoBuyout");
		Check(CivRegisterBefore<CivHooks::CvMinorCivAI_DoCompletedQuests>(host, &BeforeQuestsDone), "before CvMinorCivAI::DoCompletedQuests");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoCompletedQuests>(host, &AfterQuestsDone), "after CvMinorCivAI::DoCompletedQuests");
#else
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoBuyout>(host, &AfterBuyout), "after CvMinorCivAI::DoBuyout");
		Check(CivRegisterBefore<CivHooks::CvMinorCivAI_DoCompletedQuestsForPlayer>(host, &BeforeQuestsDone), "before CvMinorCivAI::DoCompletedQuestsForPlayer");
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoCompletedQuestsForPlayer>(host, &AfterQuestsDone), "after CvMinorCivAI::DoCompletedQuestsForPlayer");
#endif
		Check(CivRegisterAfter<CivHooks::CvMinorCivAI_DoTurn>(host, &AfterMinorTurn), "after CvMinorCivAI::DoTurn");
	}
	Check(CivRegisterBefore<CivHooks::CvPlayer_doTurn>(host, &BeforePlayerTurn), "before CvPlayer::doTurn");
	Check(CivRegisterAfter<CivHooks::CvPlayer_doTurn>(host, &AfterPlayerTurn), "after CvPlayer::doTurn");
	Check(CivRegisterBefore<CivHooks::CvPlayer_doTurnUnits>(host, &BeforePlayerTurnUnits), "before CvPlayer::doTurnUnits");
	Check(CivRegisterAfter<CivHooks::CvPlayer_doTurnUnits>(host, &AfterPlayerTurnUnits), "after CvPlayer::doTurnUnits");
	Check(CivRegisterBefore<CivHooks::CvBarbarians_DoUnits>(host, &BeforeBarbarianUnits), "before CvBarbarians::DoUnits");
	Check(CivRegisterAfter<CivHooks::CvBarbarians_DoUnits>(host, &AfterBarbarianUnits), "after CvBarbarians::DoUnits");
	Check(CivRegisterBefore<CivHooks::CvPlayerAI_AI_unitUpdate>(host, &BeforeAIUnitUpdate), "before CvPlayerAI::AI_unitUpdate");
	Check(CivRegisterBefore<CivHooks::CvUnit_setXY>(host, &TrailBeforeUnitSetXY), "before CvUnit::setXY");
	if(bCityStates)
	{
		Check(CivRegisterReplace<CivHooks::CvUnit_canEnterTerritory>(host, &ReplaceCanEnterTerritory), "replace CvUnit::canEnterTerritory");
		Check(CivRegisterReplace<CivHooks::CvTeam_IsAllowsOpenBordersToTeam>(host, &ReplaceAllowsOpenBorders), "replace CvTeam::IsAllowsOpenBordersToTeam");
		Check(CivRegisterBefore<CivHooks::CvUnit_kill>(host, &BeforeUnitKill), "before CvUnit::kill");
	}
	Check(CivRegisterAfter<CivHooks::CvUnit_setXY>(host, &AfterUnitSetXY), "after CvUnit::setXY");
	Check(host->RegisterHandHook(host->pluginIndex, CIV_HAND_IS_PLUGIN_CONTROLLED_PLAYER, (void*)static_cast<CivPlayerQueryFn>(&IsPluginControlledPlayer)), "IsPluginControlledPlayer");
	Check(host->RegisterHandHook(host->pluginIndex, CIV_HAND_KEEP_DEAD_SLOT_OPEN, (void*)static_cast<CivPlayerQueryFn>(&KeepDeadSlotOpen)), "KeepDeadSlotOpen");
	Check(host->RegisterHandHook(host->pluginIndex, CIV_HAND_POPUP_RAISED, (void*)static_cast<CivPopupRaisedFn>(&PopupRaised)), "PopupRaised");
	Check(host->RegisterHandHook(host->pluginIndex, CIV_HAND_NOTIFICATION_ADDED, (void*)static_cast<CivNotificationAddedFn>(&NotificationAdded)), "NotificationAdded");
	Check(host->RegisterHandHook(host->pluginIndex, CIV_HAND_DEAL_RESOLVED, (void*)static_cast<CivDealResolvedFn>(&DealResolved)), "DealResolved");
	Check(host->RegisterHandHook(host->pluginIndex, CIV_HAND_UNIT_ORDERS_CANCELLED, (void*)static_cast<CivUnitOrdersCancelledFn>(&UnitOrdersCancelled)), "UnitOrdersCancelled");
	Log("if the game crashes, a report and a dump go to Logs\\MCPServer-crash-<time>.txt and .dmp");
	return true;
}

PLUGIN_EXPORT void CivPluginShutdown()
{
	McpBridge::Get().Shutdown();
	McpShutdownMapDraw();
	Log("shutting down");
}
