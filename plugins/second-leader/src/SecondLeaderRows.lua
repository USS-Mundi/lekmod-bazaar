-- Second Leader: run by the plugin in each Lua environment, the first time it calls Game.GetActivePlayer (the tech
-- screens call it just before they read their uniques). Screens read a civilization's unique
-- units and buildings from GameInfo.Civilization_UnitClassOverrides and _BuildingClassOverrides (the tech tree, the
-- Civilopedia, the culture overview, trade route tooltips). Here those two tables also list each merged second's units
-- and buildings as rows of its player's own civilization. The database is not changed; only this state's GameInfo.
local KINDS = {
	{ key = "Civilization_UnitClassOverrides", pieces = "Units", info = "Units", class = "Class", classCol = "UnitClassType", typeCol = "UnitType" },
	{ key = "Civilization_BuildingClassOverrides", pieces = "Buildings", info = "Buildings", class = "BuildingClass", classCol = "BuildingClassType", typeCol = "BuildingType" },
};

local function merged(kind)
	local rows = {};
	if not (Game and Game.SecondLeaderOn and Game.SecondLeaderOn()) then return rows; end
	for id = 0, GameDefines.MAX_MAJOR_CIVS - 1 do
		local s = Game.SecondLeader(id);
		local civ = s and GameInfo.Civilizations[PreGame.GetCivilization(id)];
		for _, p in ipairs(civ and s[kind.pieces] or {}) do
			local info = p.Merged and GameInfo[kind.info][p.Type];
			if info then
				rows[#rows + 1] = { CivilizationType = civ.Type, [kind.classCol] = info[kind.class], [kind.typeCol] = p.Type };
			end
		end
	end
	return rows;
end

-- A filter is nil, a table of column values, or an SQL condition; of a condition only its Column = 'value' parts are read.
local function matches(row, filter)
	if filter == nil then return true; end
	if type(filter) == "table" then
		for k, v in pairs(filter) do if row[k] ~= v then return false; end end
		return true;
	end
	if type(filter) == "string" then
		for k, v in filter:gmatch("([%w_]+)%s*=%s*'([^']*)'") do if row[k] ~= v then return false; end end
		return true;
	end
	return false;
end

local function wrap(real, kind)
	return setmetatable({ SecondLeaderRows = true }, {
		__index = real,
		__call = function(_, filter, ...)
			local it, extra, i = real(filter, ...), nil, 0;
			return function()
				if it then
					local row = it();
					if row ~= nil then return row; end
					it = nil;
				end
				if not extra then
					extra = {};
					for _, row in ipairs(merged(kind)) do
						if matches(row, filter) then extra[#extra + 1] = row; end
					end
				end
				i = i + 1;
				return extra[i];
			end
		end,
	});
end

local function install(gi)
	if type(gi) ~= "table" then
		print("Second Leader: GameInfo is a " .. type(gi) .. " here; uniques of seconds left off this screen");
		return;
	end
	local wrapped = 0;
	for _, kind in ipairs(KINDS) do
		local real = gi[kind.key];
		if real ~= nil and not (type(real) == "table" and rawget(real, "SecondLeaderRows")) then
			rawset(gi, kind.key, wrap(real, kind));
			wrapped = wrapped + 1;
		end
	end
	if wrapped > 0 then print("Second Leader: screens list the uniques of seconds"); end
end

install(GameInfo);
