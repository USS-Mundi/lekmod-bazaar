#pragma once
// Building the MCP Server plugin against LEKMOD for Bazaar, which is LEKMOD's game core rather than Bazaar's.
// Force-included into every plugin source, before the game's headers, so it carries nothing that needs them.
//
// LEKMOD is not VMC: the option flags the plugin tests are not defined there, and are false here.
#define MOD_BUGFIX_DUMMY_POLICIES false
#define MOD_GLOBAL_CS_MARRIAGE_KEEPS_RESOURCES false
#define MOD_GLOBAL_TRULY_FREE_GP false

// Reads the plugin makes that LEKMOD has no public form for. Defined in McpLekmodCompat.cpp.
class CvNotifications;
class CvUnit;
class CvCity;
class CvPlot;
class CvCombatInfo;
int McpNotificationX(CvNotifications* pNotifications, int iZeroBasedIndex);
int McpNotificationY(CvNotifications* pNotifications, int iZeroBasedIndex);
int McpNotificationType(CvNotifications* pNotifications, int iZeroBasedIndex);           // a NotificationTypes value
int McpNotificationExtraGameData(CvNotifications* pNotifications, int iZeroBasedIndex);
// A combat the plugin wants the strength of, for LEKMOD's CvCombatInfo-based strength functions.
CvCombatInfo McpCombat(CvUnit* pAttacker, CvUnit* pDefender, CvCity* pDefendingCity, CvPlot* pFromPlot, CvPlot* pTargetPlot, bool bRanged);
