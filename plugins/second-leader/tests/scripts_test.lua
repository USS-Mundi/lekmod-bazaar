-- Drives SecondLeaderScripts.lua against stand-ins for the game and LEKMOD's real civ scripts (LuaJIT).
-- Usage: luajit scripts_test.lua <SecondLeaderScripts.lua> <LEKMOD Lua folder>
local SCRIPT, LEK = arg[1], arg[2];
local fails, passes = 0, 0;
local function check(ok, what) if ok then passes = passes + 1 else fails = fails + 1; print("FAIL " .. what) end end

local DENMARK, UAE, CHILE, KILWA, ROME = 1, 2, 3, 4, 5;
GameInfoTypes = { CIVILIZATION_DENMARK = DENMARK, CIVILIZATION_UAE = UAE, CIVILIZATION_CHILE = CHILE,
	CIVILIZATION_KILWA = KILWA, CIVILIZATION_ROME = ROME, BUILDING_KILWA_TRAIT = 900, UNIT_QASIMI_RAIDER = 700 };
GameDefines = { MAX_MAJOR_CIVS = 4 };
SlotStatus = { SS_TAKEN = 1, SS_COMPUTER = 2 };
local wonder, plain = 50, 51;
GameInfo = { Buildings = { [wonder] = { BuildingClass = "BUILDINGCLASS_W" }, [plain] = { BuildingClass = "BUILDINGCLASS_P" } },
	BuildingClasses = { BUILDINGCLASS_W = { MaxGlobalInstances = 1 }, BUILDINGCLASS_P = { MaxGlobalInstances = -1 } } };
Events = { GameplayAlertMessage = function() end, AddPopupTextEvent = function() end };
Locale = { ConvertTextKey = function(k) return k end };
function ToHexFromGrid(p) return p end
function HexToWorld(p) return p end

-- players: 0 Denmark + UAE, 1 Chile + Kilwa, 2 Rome with a second whose ability clashed (UAE stands in), 3 empty
local function makeUnit(onRoute, combat)
	local u = { xp = 0, onRoute = onRoute, combat = combat };
	u.plot = { onRoute = onRoute, GetX = function() return 0 end, GetY = function() return 0 end };
	function u:GetPlot() return self.plot end
	function u:IsCombatUnit() return self.combat end
	function u:ChangeExperience(n) self.xp = self.xp + n end
	return u;
end
local function makePlayer(id, civ, units)
	local city = { real = 0, wltkd = 0 };
	function city:SetNumRealBuilding(b, n) self.real = n end
	function city:ChangeWeLoveTheKingDayCounter(n) self.wltkd = self.wltkd + n end
	function city:GetName() return "C" end
	function city:GetOwner() return id end
	local p = { id = id, civ = civ, gold = 0, units = units or {}, city = city };
	local methods = {
		IsAlive = function(self) return true end, IsHuman = function() return false end,
		GetID = function(self) return self.id end,
		GetCivilizationType = function(self) return self.civ end,
		ChangeGold = function(self, n) self.gold = self.gold + n end,
		Units = function(self) local i = 0; return function() i = i + 1; return self.units[i] end end,
		Cities = function(self) local done = false; return function() if not done then done = true; return self.city end end end,
		GetCityByID = function(self) return self.city end,
		GetInternationalTradeRoutePlotToolTip = function(self, plot) return plot.onRoute and { "route" } or {} end,
		GetTradeRoutes = function(self) return { { Domain = 0, FromCity = self.city, ToCity = { GetOwner = function() return 9 end } } } end,
	};
	-- engine objects are userdata: methods come through the metatable, not the object's own fields
	return setmetatable(p, { __index = methods });
end
local denmarkUnits = { makeUnit(true, true), makeUnit(false, true), makeUnit(true, false) };
Players = { [0] = makePlayer(0, DENMARK, denmarkUnits), [1] = makePlayer(1, CHILE), [2] = makePlayer(2, ROME, { makeUnit(true, true) }) };
local civs = { [0] = DENMARK, [1] = CHILE, [2] = ROME };
PreGame = { GetSlotStatus = function(s) return civs[s] and SlotStatus.SS_TAKEN or 0 end, GetCivilization = function(s) return civs[s] or -1 end };

local seconds = { [0] = { Civ = "CIVILIZATION_UAE", Traits = { { Merged = true } } },
	[1] = { Civ = "CIVILIZATION_KILWA", Traits = { { Merged = true } } },
	[2] = { Civ = "CIVILIZATION_UAE", Traits = { { Merged = false } } } };
local handlers = {};
GameEvents = setmetatable({}, { __index = function(t, name)
	local e = { Add = function(fn) handlers[name] = handlers[name] or {}; table.insert(handlers[name], fn) end };
	rawset(t, name, e); return e;
end });
local function fire(name, ...) for _, fn in ipairs(handlers[name] or {}) do fn(...) end end
Game = { SecondLeaderOn = function() return true end, SecondLeader = function(id) return seconds[id] end,
	GetActivePlayer = function() return 99 end };
local included = {};
function include(name)
	for _, dir in ipairs({ "/Civilizations/", "/Utilities/" }) do
		local f = loadfile(LEK .. dir .. name);
		if f then included[name] = (included[name] or 0) + 1; f(); return end
	end
end
local printed = {};
local realPrint = print;
print = function(s) table.insert(printed, s) end

local realPlayers = Players;
dofile(SCRIPT);
print = realPrint;

check(included["Lekmod_uae.lua"] == 1, "the UAE script is included once though two players drew it");
check(included["Lekmod_kilwa.lua"] == 1, "the Kilwa script is included");
check(Players ~= realPlayers, "Players stands in here");

fire("PlayerDoTurn", 0);
local dk = realPlayers[0];
check(dk.gold == 3, "Denmark + UAE gets 3 gold for its one combat unit on a trade route (got " .. dk.gold .. ")");
check(denmarkUnits[1].xp == 1 and denmarkUnits[2].xp == 0 and denmarkUnits[3].xp == 0, "only the combat unit on a route gains 1 XP");

fire("PlayerDoTurn", 2);
check(realPlayers[2].gold == 0, "a second whose ability clashed gets nothing");

fire("CityConstructed", 0, 1, wonder);
check(dk.gold == 103 and dk.city.wltkd == 15, "a wonder pays Denmark 100 gold and 15 turns of We Love the King Day");
fire("CityConstructed", 0, 1, plain);
check(dk.gold == 103, "an ordinary building pays nothing");

fire("PlayerDoTurn", 1);
check(realPlayers[1].city.real == 1, "Chile + Kilwa gets Kilwa's building for its international route");
check(realPlayers[1].gold == 0, "Chile gets none of the UAE's gold");

Players, PreGame = realPlayers, PreGame;
-- nothing turns on when the option is off
handlers, included = {}, {};
Game.SecondLeaderOn = function() return false end;
print = function() end; dofile(SCRIPT); print = realPrint;
check(next(included) == nil and Players == realPlayers, "with the option off nothing is included or stood in");

print(string.format("%d/%d", passes, passes + fails));
os.exit(fails == 0 and 0 or 1);
