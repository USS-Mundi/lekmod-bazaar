-- Drives SecondLeaderRows.lua against a stand-in GameInfo and LEKMOD's real TechButtonInclude.lua (LuaJIT).
-- Usage: luajit rows_test.lua <SecondLeaderRows.lua> <LEKMOD Lua folder>
local ROWS, LEK = arg[1], arg[2];
local fails, passes = 0, 0;
local function check(ok, what) if ok then passes = passes + 1 else fails = fails + 1; print("FAIL " .. what) end end

-- a GameInfo table: callable with no filter, a table of column values or a condition string; indexed by Type and ID.
-- Like the game's, it refuses a filter given as nil (in a real game: "Filter must be of type 'string' or 'table'").
local function gtable(rows)
	local t = {};
	for i, r in ipairs(rows) do r.ID = r.ID or (i - 1); if r.Type then t[r.Type] = r end; t[r.ID] = r end
	return setmetatable(t, { __call = function(_, ...)
		local filter = ...;
		if select("#", ...) > 0 and type(filter) ~= "string" and type(filter) ~= "table" then
			error("Filter must be of type 'string' or 'table'", 2);
		end
		local i = 0;
		return function()
			while true do
				i = i + 1;
				local r = rows[i];
				if not r then return nil end
				local ok = true;
				if type(filter) == "table" then for k, v in pairs(filter) do ok = ok and r[k] == v end
				elseif type(filter) == "string" then for k, v in filter:gmatch("([%w_]+)%s*=%s*'([^']*)'") do ok = ok and r[k] == v end end
				if ok then return r end
			end
		end
	end });
end
local function db()
	return {
		Civilizations = gtable({ { Type = "CIVILIZATION_DENMARK" }, { Type = "CIVILIZATION_UAE" }, { Type = "CIVILIZATION_CHILE" }, { Type = "CIVILIZATION_KILWA" } }),
		UnitClasses = gtable({ { Type = "UNITCLASS_PRIVATEER", DefaultUnit = "UNIT_PRIVATEER" }, { Type = "UNITCLASS_LONGSWORDSMAN", DefaultUnit = "UNIT_LONGSWORDSMAN" },
			{ Type = "UNITCLASS_CARAVEL", DefaultUnit = "UNIT_CARAVEL" } }),
		BuildingClasses = gtable({ { Type = "BUILDINGCLASS_HOTEL", DefaultBuilding = "BUILDING_HOTEL" }, { Type = "BUILDINGCLASS_WORKSHOP", DefaultBuilding = "BUILDING_WORKSHOP" } }),
		Units = gtable({ { Type = "UNIT_QASIMI_RAIDER", Class = "UNITCLASS_PRIVATEER" }, { Type = "UNIT_BERSERKER", Class = "UNITCLASS_LONGSWORDSMAN" },
			{ Type = "UNIT_DHOW", Class = "UNITCLASS_CARAVEL" } }),
		Buildings = gtable({ { Type = "BUILDING_BURJ", BuildingClass = "BUILDINGCLASS_HOTEL" }, { Type = "BUILDING_CORAL_PORT", BuildingClass = "BUILDINGCLASS_WORKSHOP" } }),
		Civilization_UnitClassOverrides = gtable({ { CivilizationType = "CIVILIZATION_DENMARK", UnitClassType = "UNITCLASS_LONGSWORDSMAN", UnitType = "UNIT_BERSERKER" },
			{ CivilizationType = "CIVILIZATION_UAE", UnitClassType = "UNITCLASS_PRIVATEER", UnitType = "UNIT_QASIMI_RAIDER" } }),
		Civilization_BuildingClassOverrides = gtable({ { CivilizationType = "CIVILIZATION_UAE", BuildingClassType = "BUILDINGCLASS_HOTEL", BuildingType = "BUILDING_BURJ" } }),
		Improvements = gtable({}), Civilization_Leaders = gtable({}), Leader_Traits = gtable({}),
	};
end

GameDefines = { MAX_MAJOR_CIVS = 4 };
PreGame = { GetCivilization = function(id) return ({ [0] = 0, [1] = 2 })[id] or -1 end };
local seconds = {
	[0] = { Civ = "CIVILIZATION_UAE", Units = { { Type = "UNIT_QASIMI_RAIDER", Merged = true } }, Buildings = { { Type = "BUILDING_BURJ", Merged = true } } },
	[1] = { Civ = "CIVILIZATION_KILWA", Units = { { Type = "UNIT_DHOW", Merged = false } }, Buildings = { { Type = "BUILDING_CORAL_PORT", Merged = true } } },
};
local on = true;
local function game() return { SecondLeaderOn = function() return on end, SecondLeader = function(id) return seconds[id] end } end

local function collect(it) local out = {}; for r in it do out[#out + 1] = r end; return out end
local function has(rows, civ, typ, col) for _, r in ipairs(rows) do if r.CivilizationType == civ and r[col] == typ then return true end end; return false end

-- 1. GameInfo already there when the chunk runs
GameInfo = db(); Game = game();
dofile(ROWS);
local u = collect(GameInfo.Civilization_UnitClassOverrides());
check(#u == 3 and has(u, "CIVILIZATION_DENMARK", "UNIT_QASIMI_RAIDER", "UnitType"), "Denmark lists the merged Qasimi Raider after the real rows");
check(not has(u, "CIVILIZATION_CHILE", "UNIT_DHOW", "UnitType"), "a unit that did not merge is not listed");
local b = collect(GameInfo.Civilization_BuildingClassOverrides{ CivilizationType = "CIVILIZATION_CHILE" });
check(#b == 1 and b[1].BuildingType == "BUILDING_CORAL_PORT" and b[1].BuildingClassType == "BUILDINGCLASS_WORKSHOP", "a table filter finds Chile's Coral Port");
check(GameInfo.Civilization_BuildingClassOverrides{ CivilizationType = "CIVILIZATION_DENMARK", BuildingClassType = "BUILDINGCLASS_HOTEL" }().BuildingType == "BUILDING_BURJ",
	"the culture overview's one-row read gets the Burj");
local s = collect(GameInfo.Civilization_UnitClassOverrides("UnitType = 'UNIT_QASIMI_RAIDER'"));
check(#s == 2, "a Civilopedia condition finds the Raider for the UAE and for Denmark (got " .. #s .. ")");
check(GameInfo.Civilization_UnitClassOverrides.SecondLeaderRows == true, "the table is wrapped");
dofile(ROWS);
check(#collect(GameInfo.Civilization_UnitClassOverrides()) == 3, "running the chunk twice wraps once");

-- 2. the real tech tree reads Denmark's units through the wrapped table
Locale = { ConvertTextKey = function(k) return k end }; Events = {};
function include() end
dofile(LEK .. "/UI/TechButtonInclude.lua");
GatherInfoAboutUniqueStuff("CIVILIZATION_DENMARK");
local units, buildings;
for i = 1, 20 do
	local name, v = debug.getupvalue(GatherInfoAboutUniqueStuff, i);
	if not name then break end
	if name == "validUnitBuilds" then units = v elseif name == "validBuildingBuilds" then buildings = v end
end
check(units and units.UNITCLASS_PRIVATEER == "UNIT_QASIMI_RAIDER", "the tech tree shows the Qasimi Raider for Denmark");
check(units and units.UNITCLASS_LONGSWORDSMAN == "UNIT_BERSERKER", "and still Denmark's own Berserker");
check(buildings and buildings.BUILDINGCLASS_HOTEL == "BUILDING_BURJ", "and the Burj");

-- 3. option off: only the real rows
on = false;
check(#collect(GameInfo.Civilization_UnitClassOverrides()) == 2, "with the option off only the database's rows");
on = true;

-- 4. a screen's own environment: GameInfo and the base library reached through it, as the plugin runs the chunk
local env = setmetatable({ GameInfo = db() }, { __index = _G });
local chunk = loadfile(ROWS);
setfenv(chunk, env);
local printed = {};
local realPrint = print;
print = function(s) printed[#printed + 1] = s end
chunk();
print = realPrint;
check(env.GameInfo.Civilization_UnitClassOverrides.SecondLeaderRows == true, "the screen's GameInfo is wrapped");
check(#collect(env.GameInfo.Civilization_UnitClassOverrides()) == 3, "and lists the merged unit");
check(printed[1] == "Second Leader: screens list the uniques of seconds", "and says so in Lua.log");
env.GameInfo = 5;
print = function(s) printed[#printed + 1] = s end
chunk();
print = realPrint;
check(printed[2] and printed[2]:find("GameInfo is a number"), "a GameInfo it cannot wrap is reported");

-- 5. a screen's environment as the game gives it: the base library without rawset or rawget (a real game's Bazaar.log:
--    "SecondLeaderRows:74: attempt to call global 'rawset' (a nil value)"), and a GameInfo the screen cannot write to
local base = {};
for k, v in pairs(_G) do if k ~= "rawset" and k ~= "rawget" then base[k] = v end end
local function sealed(t)
	return setmetatable({}, { __index = t, __newindex = function(_, k) error("GameInfo." .. tostring(k) .. " cannot be set", 2) end });
end
local genv = setmetatable({ GameInfo = sealed(db()), Game = game() }, { __index = base });
local gchunk = loadfile(ROWS);
setfenv(gchunk, genv);
local ok, err = pcall(gchunk);
check(ok, "the chunk runs in a screen's environment as the game gives it (" .. tostring(err) .. ")");
if ok then
	check(#collect(genv.GameInfo.Civilization_UnitClassOverrides()) == 3, "and that screen's GameInfo lists the merged unit");
	check(genv.GameInfo.Units.UNIT_BERSERKER.Class == "UNITCLASS_LONGSWORDSMAN", "and still reads every other table");
	local include = loadfile(LEK .. "/UI/TechButtonInclude.lua");
	setfenv(include, genv);
	include();
	genv.GatherInfoAboutUniqueStuff("CIVILIZATION_DENMARK");
	local gunits;
	for i = 1, 20 do
		local name, v = debug.getupvalue(genv.GatherInfoAboutUniqueStuff, i);
		if not name then break end
		if name == "validUnitBuilds" then gunits = v end
	end
	check(gunits and gunits.UNITCLASS_PRIVATEER == "UNIT_QASIMI_RAIDER", "and its tech tree shows the Qasimi Raider for Denmark");
	pcall(gchunk);
	check(#collect(genv.GameInfo.Civilization_UnitClassOverrides()) == 3, "and a second run there lists it once");
end

print(string.format("%d/%d", passes, passes + fails));
os.exit(fails == 0 and 0 or 1);
