// The helpers McpLekmodCompat.h declares.
//
// CvNotifications keeps its notifications in a protected store with no accessor for a notification's place,
// kind or extra data, which Community Patch adds and the plugin reads. Access control is the compiler's alone
// and leaves no trace in the binary, so this one translation unit opens the class by renaming the keywords
// before the game's headers are read. Nothing about the class's layout changes.
#define private public
#define protected public
#include "CvGameCoreDLLPCH.h"
#undef private
#undef protected

#include "McpLekmodCompat.h"

static const CvNotifications::Notification& NotificationAt(CvNotifications* pNotifications, int iZeroBasedIndex)
{
	// As CvNotifications::GetNotificationID indexes the store.
	const int iRealIndex = (pNotifications->m_iNotificationsBeginIndex + iZeroBasedIndex) % pNotifications->m_aNotifications.size();
	return pNotifications->m_aNotifications[iRealIndex];
}

int McpNotificationX(CvNotifications* pNotifications, int iZeroBasedIndex) { return NotificationAt(pNotifications, iZeroBasedIndex).m_iX; }
int McpNotificationY(CvNotifications* pNotifications, int iZeroBasedIndex) { return NotificationAt(pNotifications, iZeroBasedIndex).m_iY; }
int McpNotificationType(CvNotifications* pNotifications, int iZeroBasedIndex) { return (int)NotificationAt(pNotifications, iZeroBasedIndex).m_eNotificationType; }
int McpNotificationExtraGameData(CvNotifications* pNotifications, int iZeroBasedIndex) { return NotificationAt(pNotifications, iZeroBasedIndex).m_iExtraGameData; }

CvCombatInfo McpCombat(CvUnit* pAttacker, CvUnit* pDefender, CvCity* pDefendingCity, CvPlot* pFromPlot, CvPlot* pTargetPlot, bool bRanged)
{
	// Filled as LEKMOD's own CvLuaUnit::lGetMaxAttackStrength fills it for the game's combat preview.
	CvCombatInfo kInfo;
	kInfo.setUnit(BATTLE_UNIT_ATTACKER, pAttacker);
	kInfo.setUnit(BATTLE_UNIT_DEFENDER, pDefender);
	kInfo.setCity(BATTLE_UNIT_DEFENDER, pDefendingCity);
	kInfo.setUnit(BATTLE_UNIT_INTERCEPTOR, NULL);
	kInfo.setPlot(pTargetPlot);
	kInfo.setFromPlot(pFromPlot);
	kInfo.setAttackIsRanged(bRanged);
	kInfo.setAttackIsBombingMission(false);
	kInfo.setAttackIsAirSweep(false);
	kInfo.setDefenderRetaliates(bRanged);
	return kInfo;
}
