-- Second Leader: runs LEKMOD's own scripted civ abilities (Lua/Civilizations/Lekmod_<civ>.lua) for the players who
-- have that civ as their merged second. Those scripts act only for a player whose civilization is theirs
-- (player:GetCivilizationType(), PreGame.GetCivilization(slot)), so a second never got them. This context includes each
-- needed script with Players and PreGame standing in: a player shows its second civ here, and only here. LEKMOD's
-- files stay untouched; its own contexts keep running them for the real owners.

local SECOND_SCRIPT_PREFIX = "Lekmod_";

local realPlayers, realPreGame = Players, PreGame;
local secondCiv = {};   -- player id -> civilization id of a second whose ability merged

local function scriptName(civType)
	return SECOND_SCRIPT_PREFIX .. civType:gsub("^CIVILIZATION_", ""):gsub("_", ""):lower() .. ".lua";
end

-- A player as the scripts see it here: every method reaches the real player, but its civilization is its second.
local function asSecond(player, civ)
	local methods = {};
	return setmetatable({}, { __index = function(_, k)
		if k == "GetCivilizationType" then return function() return civ; end end
		local m = methods[k];
		if m then return m; end
		local v = player[k];
		if type(v) ~= "function" then return v; end
		m = function(_, ...) return v(player, ...); end
		methods[k] = m;
		return m;
	end });
end

local function seconds()
	if not (Game.SecondLeaderOn and Game.SecondLeaderOn()) then return {}; end
	local found = {};
	for id = 0, GameDefines.MAX_MAJOR_CIVS - 1 do
		local s = Game.SecondLeader(id);
		local civ = s and GameInfoTypes[s.Civ];
		local merged = false;
		for _, t in ipairs(s and s.Traits or {}) do merged = merged or t.Merged; end
		if civ and merged then found[id] = { Civ = civ, Type = s.Civ }; end
	end
	return found;
end

local found = seconds();
local stand = {};
for id, p in pairs(realPlayers) do
	stand[id] = found[id] and p and asSecond(p, found[id].Civ) or p;
end
for id, s in pairs(found) do secondCiv[id] = s.Civ; end

if next(found) then
	Players = stand;
	PreGame = setmetatable({ GetCivilization = function(slot)
		return secondCiv[slot] or realPreGame.GetCivilization(slot);
	end }, { __index = realPreGame });
	local done = {};
	for id, s in pairs(found) do
		local file = scriptName(s.Type);
		if not done[file] then
			done[file] = true;
			print("Second Leader: player " .. id .. " runs " .. file .. " as " .. s.Type);
			include(file);
		end
	end
end
