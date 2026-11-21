// Time of Day: shades the map through the day (dawn, golden hour, dusk, night) on the graphics card, grading the
// finished world picture once a frame before Civ draws its interface. The Direct3D side is in ColorGradeD3D.cpp, the
// grades in CgLooks.cpp; this file is the plugin's entry and the Lua functions the panel calls.
#include "CvGameCoreDLLPCH.h"

#include "CivHooks.h"
#include "CivPluginApi.h"
#include "PluginVersion.h"

#include "CgLooks.h"
#include "ColorGrade.h"

#define PLUGIN_EXPORT extern "C" __declspec(dllexport)

CIV_PLUGIN_EXPORT_API_VERSION;

namespace
{
	const CivHostInfo* g_host = 0;

	float Num(lua_State* L, int i, float fallback) { return lua_isnumber(L, i) ? (float)lua_tonumber(L, i) : fallback; }

	//void CGSet(int mode, float daySeconds, float strength, float nightDepth, float warmth, float brightness, float colour, float contrast)
	int lSet(lua_State* L)
	{
		CgSet((int)Num(L, 1, 0), Num(L, 2, 600), Num(L, 3, 1), Num(L, 4, 1), Num(L, 5, 1), Num(L, 6, 1), Num(L, 7, 1), Num(L, 8, 1));
		return 0;
	}

	//void CGTarget(float phase or string look, bool jump, float seconds): a jump, or a glide over seconds
	int lTarget(lua_State* L)
	{
		float p = lua_type(L, 1) == LUA_TSTRING ? CgPhaseOf(lua_tostring(L, 1)) : Num(L, 1, 0);
		if(p >= 0) CgSetTarget(p, lua_toboolean(L, 2) != 0 ? 0 : Num(L, 3, 0));
		return 0;
	}

	//void CGLand(float seconds): a glide under way lands within seconds
	int lLand(lua_State* L)
	{
		CgLand(Num(L, 1, 0));
		return 0;
	}

	//float, string, string CGState(); the phase, the part of the day, and what the plugin is doing.
	int lState(lua_State* L)
	{
		float p = CgPhase();
		lua_pushnumber(L, p);
		lua_pushstring(L, CgPartOf(p));
		lua_pushstring(L, CgStatus());
		return 3;
	}

	//void CGSetPhase(float phase)
	int lSetPhase(lua_State* L)
	{
		CgSetPhase(Num(L, 1, 0));
		return 0;
	}

	void Add(lua_State* L, const char* name, CivLuaFunction fn)
	{
		g_host->PushLuaFunction(g_host->pluginIndex, L, fn);
		lua_setfield(L, -2, name);
	}

	void AfterRegisterGame(lua_State* L)
	{
		Add(L, "CGSet", &lSet);
		Add(L, "CGTarget", &lTarget);
		Add(L, "CGLand", &lLand);
		Add(L, "CGState", &lState);
		Add(L, "CGSetPhase", &lSetPhase);
	}
}

PLUGIN_EXPORT bool CivPluginEntry(const CivHostInfo* host, CivPluginInfo* plugin)
{
	g_host = host;
	plugin->name = "Time of Day";
	plugin->version = PLUGIN_VERSION;
	CgNote("---- loaded");
	CgInstallPresentHook();
	if(CivRegisterAfter<CivHooks::CvLuaGame_RegisterMembers>(host, &AfterRegisterGame) != CIV_HOOK_OK) CgNote("could not add the Lua functions");
	return true;
}

PLUGIN_EXPORT void CivPluginShutdown()
{
	CgRestorePresent();
}
