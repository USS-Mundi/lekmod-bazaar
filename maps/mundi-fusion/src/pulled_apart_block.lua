
------------------------------------------------------------------------------
------------------------------------------------------------------------------
-- LEKMAP FUSION (the Pangaea, pulled apart)
-- Everything above is Lekmap v6.3 (Pangaea - Fractal), unchanged except seven lines, each marked
-- "Lekmap Fusion": six in its start picker and its mountain cut-off. This block, added at the end, replaces:
--   GetMapScriptInfo                            the map's name and the Pulled Apart options
--   GeneratePlotTypes                           tears the Pangaea along rifts, joins any landmass big enough
--                                               for a civ to the rest and pulls specks of land onto nearby
--                                               land, before terrain is laid
--   AssignStartingPlots:GenerateRegions         one region per landmass (one landmass once connected), and
--                                               the start rules for this map
--   AssignStartingPlots:PlaceResourcesAndCityStates  accepts the first map, so each map is made once
--   AssignStartingPlots:ExaminePlotForNaturalWondersEligibility  no natural wonder on a bridge
--   GenerateMap                                 checks the bridges and writes the map to the log
-- Lekmap remakes a map until its balance checks pass, up to 300 times; those checks
-- expect one landmass, so here the first map is kept and a map takes one pass.
------------------------------------------------------------------------------
------------------------------------------------------------------------------

local function PA_Steps(first, last, step)
	local t, v = {}, first
	while v <= last + 0.0001 do
		t[#t + 1] = v
		v = v + step
	end
	return t
end

-- Defaults are the operator's settings from the preview (2026-11-20); islands: fewer and larger (2026-11-20); his style
-- from the generator (2026-11-23): four weak, wide rifts, middle-sea islands of medium size.
PA_OPTIONS = {
	{ key = "on",        name = "Pull Apart",                values = {1, 2}, labels = {"On", "Off (plain Lekmap Pangaea, made once)"}, default = 1 },
	{ key = "rifts",     name = "Rifts",                     values = {1, 2, 3, 4}, default = 4 },
	{ key = "spread",    name = "Rift Angle Spread",         values = {0, 30, 60, 90, 120, 150, 180}, suffix = " degrees", default = 180 },
	{ key = "strength",  name = "Rift Strength",             values = {40, 50, 60, 70, 80, 85, 89, 95, 100}, default = 40 },
	{ key = "vary",      name = "Rift Strength Spread",      values = {0, 25, 50, 75, 100}, suffix = "%", default = 100 },
	{ key = "width",     name = "Rift Width",                values = PA_Steps(2, 10.5, 0.5), suffix = " tiles", default = 6.5 },
	{ key = "rag",       name = "Rift Raggedness",           values = PA_Steps(0, 8, 0.5), suffix = " tiles", default = 5.5 },
	{ key = "grain",     name = "Rift Grain",                values = {1, 2, 3}, labels = {"Coarse", "Medium", "Fine"}, default = 3 },
	{ key = "wave",      name = "Rift Waviness",             values = PA_Steps(0, 10, 1), suffix = " tiles", default = 6 },
	{ key = "bends",     name = "Rift Bends",                values = {1, 2, 3, 4}, default = 4 },
	{ key = "cross",     name = "Rift Crossing Point",       values = {1, 2, 3}, labels = {"Middle of the continent", "Near the middle", "Anywhere on the continent"}, default = 2 },
	{ key = "mid",       name = "Middle-Sea Islands",        values = PA_Steps(0, 8, 1), default = 4 },
	{ key = "midsize",   name = "Middle-Sea Island Size",    values = {1, 2, 3}, labels = {"Small (4-5 tiles)", "Medium (4-7 tiles)", "Large (5-10 tiles)"}, default = 2 },
	{ key = "outer",     name = "Outer Islands",             values = PA_Steps(0, 20, 1), default = 10 },
	{ key = "outersize", name = "Outer Island Size",         values = {1, 2, 3}, labels = {"Small (4-5 tiles)", "Medium (4-6 tiles)", "Large (4-9 tiles)"}, default = 3 },
	{ key = "connect",   name = "Connect all continents",    values = {1, 2, 3}, labels = {"On", "On, with a canal spot on each bridge", "Off"}, default = 2, show = 1.5 },
	{ key = "mountains", name = "Mountains",                 values = {1, 2}, labels = {"Like Lekmap Continents", "Like Lekmap Pangaea"}, default = 1, show = 1.7 },
	{ key = "continents", name = "Continents",               values = {1, 2}, labels = {"At least two", "As the rifts fall"}, default = 1, show = 1.2 },
	{ key = "deep",      name = "Deep sea",                  values = {1, 2}, labels = {"On", "Off (Lekmap's coasts)"}, default = 1, show = 1.6 },
}
-- No island under 4 tiles (operator, 2026-11-21).
local PA_MID_SIZES = { {4, 5}, {4, 7}, {5, 10} }
local PA_OUTER_SIZES = { {4, 5}, {4, 6}, {4, 9} }
local PA_FRACTAL_GRAIN = { 2, 3, 4 } -- Coarse, Medium, Fine
local PA_CROSS_SHARE = { 0, 0.16, 0.4 } -- how far from the middle the rifts may cross, as a share of the map

local function PA_DefaultIndex(opt)
	for v = 1, #opt.values do
		if opt.values[v] == opt.default then return v end
	end
	error("Lekmap Fusion: the default of " .. opt.name .. " is not one of its values")
end

local function PA_Label(opt, v)
	if opt.labels then return opt.labels[v] end
	return tostring(opt.values[v]) .. (opt.suffix or "")
end

-- Lekmap's own options that Fusion starts elsewhere, by name and the label of the choice (operator, 2026-11-23). Lekmap
-- adds the land size to every map size alike: +10 tiles on Small is +10 tiles on Huge.
PA_LEKMAP_DEFAULTS = {
	["TXT_KEY_MAP_OPTION_SEA_LEVEL"] = "TXT_KEY_MAP_OPTION_LOW",
	["Land Size X"] = "Default +10 tiles",
	["Land Size Y"] = "Default +10 tiles",
}

local PA_LekmapGetMapScriptInfo = GetMapScriptInfo
function GetMapScriptInfo()
	local info = PA_LekmapGetMapScriptInfo()
	local pending = {}
	for name in pairs(PA_LEKMAP_DEFAULTS) do pending[name] = true end
	for _, o in ipairs(info.CustomOptions) do
		local want = PA_LEKMAP_DEFAULTS[o.Name]
		if want then
			for v, label in ipairs(o.Values) do if label == want then o.DefaultValue = v end end
			if o.Values[o.DefaultValue] ~= want then error("Lekmap Fusion: Lekmap's " .. o.Name .. " has no choice " .. want) end
			pending[o.Name] = nil
		end
	end
	if next(pending) then error("Lekmap Fusion: Lekmap has no option " .. next(pending)) end
	info.Name = "Lekmap Fusion"
	info.Description = "Lekmap Fusion: Lekmap v6.3's Pangaea, torn by rifts at their own angles and strengths. The sea seeps in along them, high ground survives as necks and peninsulas, and islands rise in the sea between. Each map is made once."
	for k, opt in ipairs(PA_OPTIONS) do
		local values = {}
		for v = 1, #opt.values do values[v] = PA_Label(opt, v) end
		-- Shown above all of Lekmap's own options (theirs sort from -99 up).
		table.insert(info.CustomOptions, { Name = opt.name, Values = values, DefaultValue = PA_DefaultIndex(opt), SortPriority = -1000 + 10 * (opt.show or k) })
	end
	return info
end

-- The Pulled Apart options are read by position: they come after Lekmap's own.
function PA_Settings()
	local base = #PA_LekmapGetMapScriptInfo().CustomOptions
	local s = {}
	for k, opt in ipairs(PA_OPTIONS) do
		local choice = Map.GetCustomOption(base + k)
		if choice == nil or choice < 1 or choice > #opt.values then
			print("Lekmap Fusion: WARNING option '" .. opt.name .. "' read as " .. tostring(choice) .. ", outside its " .. #opt.values .. " values; using its default")
			choice = PA_DefaultIndex(opt)
		end
		s[opt.key] = opt.values[choice]
	end
	return s
end

-- Civ's hex grid: odd rows sit half a tile east of even rows. Plot tables are 1-based: i = y * W + x + 1.
local PA_ODD = { {-1, 0}, {1, 0}, {0, -1}, {1, -1}, {0, 1}, {1, 1} }
local PA_EVEN = { {-1, 0}, {1, 0}, {-1, -1}, {0, -1}, {-1, 1}, {0, 1} }

local function PA_Neighbours(i, W, H, wrap)
	local x = (i - 1) % W
	local y = (i - 1 - x) / W
	local offsets = (y % 2 == 1) and PA_ODD or PA_EVEN
	local out = {}
	for _, d in ipairs(offsets) do
		local nx, ny = x + d[1], y + d[2]
		if ny >= 0 and ny < H then
			if wrap then nx = nx % W end
			if nx >= 0 and nx < W then out[#out + 1] = ny * W + nx + 1 end
		end
	end
	return out
end

function PA_NeighbourList(i, W, H, wrap) return PA_Neighbours(i, W, H, wrap) end

-- A fractal from Civ's own generator, read back as 0..1 per tile.
local function PA_Field(grain, W, H)
	local frac = Fractal.Create(W, H, grain, Map.GetFractalFlags(), -1, -1)
	local lo, hi = nil, nil
	local h = {}
	for y = 0, H - 1 do
		for x = 0, W - 1 do
			local v = frac:GetHeight(x, y)
			h[y * W + x + 1] = v
			if lo == nil or v < lo then lo = v end
			if hi == nil or v > hi then hi = v end
		end
	end
	local span = math.max(1, hi - lo)
	for i = 1, W * H do h[i] = (h[i] - lo) / span end
	return h
end

-- A number in [0, 1). The game's Map.Rand keeps 16 bits: a range above 65536 never comes back above 65535.
function PA_Rand()
	return Map.Rand(65536, "Pulled Apart") / 65536
end

-- Sizes of the landmasses, biggest first.
local function PA_Landmasses(plotTypes, W, H, wrap)
	local seen, sizes = {}, {}
	for i = 1, W * H do
		if plotTypes[i] ~= PlotTypes.PLOT_OCEAN and not seen[i] then
			local stack, size = { i }, 0
			seen[i] = true
			while #stack > 0 do
				local c = table.remove(stack)
				size = size + 1
				for _, n in ipairs(PA_Neighbours(c, W, H, wrap)) do
					if not seen[n] and plotTypes[n] ~= PlotTypes.PLOT_OCEAN then
						seen[n] = true
						stack[#stack + 1] = n
					end
				end
			end
			sizes[#sizes + 1] = size
		end
	end
	table.sort(sizes, function(a, b) return a > b end)
	return sizes
end

-- At least two continents. Where the rifts leave one landmass holding most of the land, the land they cut deepest
-- (the necks of high ground they left standing) sinks too, a few tiles at a time, until the biggest landmass holds
-- PA_BIGGEST_SHARE of the land or less and the second is big enough for a civ (half a civ's share, 20 tiles at least,
-- as the bridges count it). It sinks at most PA_SPLIT_CAP of the land. Returns the tiles sunk, and whether it worked.
PA_BIGGEST_SHARE = 0.6
PA_SPLIT_CAP = 0.15
function PA_TwoContinents(types, depth, W, H, wrap, civs)
	local OCEAN = PlotTypes.PLOT_OCEAN
	local function measure()
		local sizes = PA_Landmasses(types, W, H, wrap)
		local total = 0
		for _, n in ipairs(sizes) do total = total + n end
		local need = math.max(20, math.floor(total / (2 * math.max(1, civs))))
		return (sizes[1] or 0) <= PA_BIGGEST_SHARE * total and (sizes[2] or 0) >= need, total
	end
	local ok, total = measure()
	if ok then return 0, true end
	local order = {}
	for i = 1, W * H do
		if types[i] ~= OCEAN and (depth[i] or 0) > 0 then order[#order + 1] = i end
	end
	table.sort(order, function(a, b) if depth[a] ~= depth[b] then return depth[a] > depth[b] end return a < b end)
	local cap, sunk, k = math.floor(total * PA_SPLIT_CAP), 0, 1
	while not ok and sunk < cap and k <= #order do
		for _ = 1, 4 do
			local i = order[k]
			if i == nil then break end
			k = k + 1
			types[i] = OCEAN
			if BETA_TECTONIC_LANDS ~= nil and BETA_TECTONIC_LANDS[i] ~= nil and BETA_TECTONIC_LANDS[i] > 0 then BETA_TECTONIC_LANDS[i] = 0 end
			sunk = sunk + 1
		end
		ok = measure()
	end
	return sunk, ok
end

PA_METHOD = nil -- the start placement method once the land is torn: 1 = one landmass, 2 = one region per landmass
PA_DEPTH = nil  -- how deep the rifts cut each tile, 0..1, once the land is torn

function PA_PullApart(plotTypes)
	PA_METHOD = nil
	PA_DEPTH = nil
	PA_CONNECT = false
	PA_CANAL = false
	PA_DEEP = false
	local S = PA_Settings()
	if S.on ~= 1 then
		print("Lekmap Fusion: off; plain Lekmap Pangaea")
		return
	end
	local W, H = Map.GetGridSize()
	local wrap = Map:IsWrapX()
	local RY = math.sqrt(3) / 2
	local OCEAN = PlotTypes.PLOT_OCEAN
	local function isLand(i) return plotTypes[i] ~= OCEAN end
	local function hx(x, y) if y % 2 == 1 then return x + 0.5 end return x end
	local function wrapDX(v)
		if not wrap then return v end
		v = v % W
		if v > W / 2 then v = v - W end
		return v
	end

	-- Where the continent's middle is (its x measured around the wrap).
	local land, sumC, sumS, sumY = {}, 0, 0, 0
	for i = 1, W * H do
		if isLand(i) then
			local x = (i - 1) % W
			local y = (i - 1 - x) / W
			land[#land + 1] = i
			local a = 2 * math.pi * hx(x, y) / W
			sumC, sumS, sumY = sumC + math.cos(a), sumS + math.sin(a), sumY + y
		end
	end
	if #land == 0 then
		print("Lekmap Fusion: WARNING no land to pull apart")
		return
	end
	local cenX = (math.atan2(sumS, sumC) / (2 * math.pi) * W) % W
	local cenY = sumY / #land

	-- The rifts: wavy lines through the middle, each at its own angle, width and strength.
	local cross = PA_CROSS_SHARE[S.cross]
	local rifts = {}
	for r = 1, S.rifts do
		local ang = math.pi / 2 + (PA_Rand() - 0.5) * S.spread * math.pi / 180
		rifts[r] = {
			px = cenX + (PA_Rand() - 0.5) * W * cross,
			py = (cenY + (PA_Rand() - 0.5) * H * cross) * RY,
			dx = math.cos(ang), dy = math.sin(ang),
			S = (S.strength / 100 * 1.25) * ((r == 1) and 1 or (1 - (S.vary / 100) * PA_Rand())),
			R = S.width * ((r == 1) and 1 or (0.75 + 0.25 * PA_Rand())),
			p1 = PA_Rand() * 2 * math.pi, p2 = PA_Rand() * 2 * math.pi,
		}
	end
	local cyc, LEN = S.bends * 0.5, H * RY
	local function waveAt(rf, along)
		return S.wave * (0.8 * math.sin(2 * math.pi * cyc * along / LEN + rf.p1) + 0.2 * math.sin(2 * math.pi * cyc * 2.3 * along / LEN + rf.p2))
	end

	-- How deep the rifts cut at each tile: 0 untouched, 1 open sea.
	local ragX, ragY = PA_Field(3, W, H), PA_Field(3, W, H)
	local T = {}
	for y = 0, H - 1 do
		for x = 0, W - 1 do
			local i = y * W + x + 1
			local qx = hx(x, y) + (ragX[i] - 0.5) * 2 * S.rag
			local qy = y * RY + (ragY[i] - 0.5) * 2 * S.rag
			local keep = 1
			for _, rf in ipairs(rifts) do
				local ox, oy = wrapDX(qx - rf.px), qy - rf.py
				local along = ox * rf.dx + oy * rf.dy
				local across = -ox * rf.dy + oy * rf.dx
				local dist = math.abs(across - waveAt(rf, along))
				if dist < rf.R then keep = keep * (1 - math.min(1, rf.S * (1 - dist / rf.R) ^ 1.3)) end
			end
			T[i] = 1 - keep
		end
	end

	-- Land sinks where the rift cuts deeper than the ground is high; high ground stays as necks.
	local ground = PA_Field(PA_FRACTAL_GRAIN[S.grain], W, H)
	table.sort(land, function(a, b) return ground[a] < ground[b] end)
	local carved = 0
	for rank, i in ipairs(land) do
		local u = (#land > 1) and ((rank - 1) / (#land - 1)) or 1
		if u <= T[i] then
			plotTypes[i] = OCEAN
			if BETA_TECTONIC_LANDS ~= nil and BETA_TECTONIC_LANDS[i] ~= nil and BETA_TECTONIC_LANDS[i] > 0 then BETA_TECTONIC_LANDS[i] = 0 end
			carved = carved + 1
		end
	end
	local splitNote = ""
	if S.continents == 1 then
		local sunk, split = PA_TwoContinents(plotTypes, T, W, H, wrap, PA_CivCount())
		carved = carved + sunk
		if sunk > 0 then splitNote = split and ("; " .. sunk .. " more sank to leave two continents") or ("; WARNING " .. sunk .. " more sank and still one continent") end
	end

	-- Islands: first in the sea the rifts opened, then in the ocean around; two tiles clear of other land.
	local function landNear(i, blob)
		for _, a in ipairs(PA_Neighbours(i, W, H, wrap)) do
			if isLand(a) and not blob[a] then return true end
			for _, b in ipairs(PA_Neighbours(a, W, H, wrap)) do
				if isLand(b) and not blob[b] then return true end
			end
		end
		return false
	end
	local function usable(i)
		local x = (i - 1) % W
		local y = (i - 1 - x) / W
		if y < 3 or y > H - 4 or isLand(i) then return false end
		if BETA_TECTONIC_LANDS ~= nil and BETA_TECTONIC_LANDS[i] == 2 then return false end -- Lekmap turns these to sea afterwards
		return true
	end
	local function raise(candidates, want, sizes)
		local made = 0
		for k = 1, want do
			for tries = 1, 60 do
				if #candidates == 0 then break end
				local s = candidates[Map.Rand(#candidates, "Pulled Apart island") + 1]
				if usable(s) and not landNear(s, {}) then
					local size = sizes[1] + Map.Rand(sizes[2] - sizes[1] + 1, "Pulled Apart island size")
					local blob, members = { [s] = true }, { s }
					plotTypes[s] = PlotTypes.PLOT_LAND
					local guard = 0
					while #members < size and guard < 60 do
						guard = guard + 1
						local from = members[Map.Rand(#members, "Pulled Apart island") + 1]
						local nbs = PA_Neighbours(from, W, H, wrap)
						local c = nbs[Map.Rand(#nbs, "Pulled Apart island") + 1]
						if usable(c) and not landNear(c, blob) then
							plotTypes[c] = (Map.Rand(4, "Pulled Apart island hills") == 0) and PlotTypes.PLOT_HILLS or PlotTypes.PLOT_LAND
							blob[c] = true
							members[#members + 1] = c
						end
					end
					if #members >= sizes[1] then
						made = made + 1
						break
					end
					for _, c in ipairs(members) do plotTypes[c] = PlotTypes.PLOT_OCEAN end -- too small to keep: try elsewhere
				end
			end
		end
		return made
	end
	local midCand, outCand = {}, {}
	for i = 1, W * H do
		if not isLand(i) then
			if T[i] >= 0.45 then midCand[#midCand + 1] = i elseif T[i] < 0.05 then outCand[#outCand + 1] = i end
		end
	end
	local midMade = raise(midCand, S.mid, PA_MID_SIZES[S.midsize])
	local outerMade = raise(outCand, S.outer, PA_OUTER_SIZES[S.outersize])

	-- Starts: civs are shared out over the landmasses by their land (the game's continental
	-- method), so a landmass big enough for a civ gets one; Lekmap's own method puts every civ
	-- on the biggest landmass.
	local sizes = PA_Landmasses(plotTypes, W, H, wrap)
	local total = 0
	for _, n in ipairs(sizes) do total = total + n end
	PA_METHOD = 2
	PA_DEPTH = T
	PA_CONNECT = (S.connect ~= 3)
	PA_CANAL = (S.connect == 2)
	PA_DEEP = (S.deep == 1)
	local shares = {}
	for k = 1, math.min(4, #sizes) do shares[k] = math.floor(100 * sizes[k] / total + 0.5) .. "%" end
	print("Lekmap Fusion: " .. S.rifts .. " rifts, " .. carved .. " tiles of land became sea, " ..
		midMade .. " of " .. S.mid .. " middle-sea islands, " .. outerMade .. " of " .. S.outer .. " outer islands; " ..
		#sizes .. " landmasses, biggest shares " .. table.concat(shares, " ") .. splitNote .. "; start placement method " .. PA_METHOD)
end

------------------------------------------------------------------------------
-- Connect all continents. Every landmass big enough for a civ, cut off by the sea or by a wall of
-- mountains, gets one bridge of land to the biggest, where the rifts cut shallowest. A bridge is two
-- to three tiles wide with no mountain on it, so no single city or peak can close it, and every civ
-- starts on the joined land. With canal spots, each bridge that crosses water instead narrows to one
-- flat tile with the sea on both sides: a city there is a canal, and only its owner's units cross it; a bridge with
-- no such spot on any way across is built solid.
-- A bridge joins only land not yet joined, so no ring of land closes a sea in; of the ways across,
-- one that walls a sea off is taken only when no other is found.

local function PA_Heap()
	local h, n = {}, 0
	local heap = {}
	function heap.push(cost, i)
		n = n + 1; h[n] = { cost, i }
		local k = n
		while k > 1 do
			local up = math.floor(k / 2)
			if h[up][1] <= h[k][1] then break end
			h[up], h[k] = h[k], h[up]; k = up
		end
	end
	function heap.pop()
		if n == 0 then return nil end
		local top = h[1]
		h[1] = h[n]; h[n] = nil; n = n - 1
		local k = 1
		while true do
			local l, r, m = 2 * k, 2 * k + 1, k
			if l <= n and h[l][1] < h[m][1] then m = l end
			if r <= n and h[r][1] < h[m][1] then m = r end
			if m == k then break end
			h[m], h[k] = h[k], h[m]; k = m
		end
		return top[1], top[2]
	end
	return heap
end

-- types: plot types by plot index, changed in place; depth: the rifts' depth per tile, or nil;
-- canal: true to leave one canal spot on each bridge that crosses water.
-- Returns the bridges made, the tiles raised, the canal spots, and every tile of every bridge.
function PA_Bridge(types, depth, W, H, wrap, civs, canal)
	local LAND, HILLS, MOUNTAIN, OCEAN = PlotTypes.PLOT_LAND, PlotTypes.PLOT_HILLS, PlotTypes.PLOT_MOUNTAIN, PlotTypes.PLOT_OCEAN
	local nbCache = {}
	local function nb(i)
		local list = nbCache[i]
		if list == nil then list = PA_Neighbours(i, W, H, wrap); nbCache[i] = list end
		return list
	end
	local function walkable(i) return types[i] == LAND or types[i] == HILLS end
	local function polar(i) local y = math.floor((i - 1) / W); return y < 2 or y > H - 3 end
	local function landmasses()
		local id, sizes, first, total = {}, {}, {}, 0
		for i = 1, W * H do
			if not id[i] and walkable(i) then
				local k = #sizes + 1
				local stack, n = { i }, 0
				id[i] = k
				first[k] = i
				while #stack > 0 do
					local c = table.remove(stack)
					n = n + 1
					for _, j in ipairs(nb(c)) do
						if not id[j] and walkable(j) then id[j] = k; stack[#stack + 1] = j end
					end
				end
				sizes[k] = n
				total = total + n
			end
		end
		return id, sizes, first, total
	end
	-- Whether two tiles are joined by walkable land, with one tile taken out.
	local function joined(a, b, without)
		local seen, stack = { [a] = true }, { a }
		while #stack > 0 do
			local c = table.remove(stack)
			if c == b then return true end
			for _, j in ipairs(nb(c)) do
				if not seen[j] and j ~= without and walkable(j) then seen[j] = true; stack[#stack + 1] = j end
			end
		end
		return false
	end
	-- The six neighbours of a tile in order around it (odd rows sit half a tile east).
	local RING = { {1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1} }
	local function ring(i)
		local x = (i - 1) % W
		local y = (i - 1 - x) / W
		local q = x - (y - y % 2) / 2
		local out = {}
		for k, d in ipairs(RING) do
			local r = y + d[2]
			local j = nil
			if r >= 0 and r < H then
				local nx = q + d[1] + (r - r % 2) / 2
				if wrap then nx = nx % W end
				if nx >= 0 and nx < W then j = r * W + nx + 1 end
			end
			out[k] = j
		end
		return out
	end
	-- A canal spot: land on the path at both ends, sea on both sides between them.
	local function canalSpot(c, prev, nxt)
		local around, a, b = ring(c), nil, nil
		for k = 1, 6 do
			if around[k] == prev then a = k end
			if around[k] == nxt then b = k end
		end
		if a == nil or b == nil then return false end
		local function seaBetween(from, to)
			local k = from % 6 + 1
			while k ~= to do
				local j = around[k]
				if j ~= nil and types[j] == OCEAN and not polar(j) then return true end
				k = k % 6 + 1
			end
			return false
		end
		return seaBetween(a, b) and seaBetween(b, a)
	end

	local bridges, raised, canals, tiles, skip = 0, 0, {}, {}, {}
	for round = 1, 40 do
		local id, sizes, first, total = landmasses()
		local main, target = nil, nil
		for k, n in ipairs(sizes) do if main == nil or n > sizes[main] then main = k end end
		if main == nil then break end
		local need = math.max(20, math.floor(total / (2 * math.max(1, civs))))
		for k, n in ipairs(sizes) do
			if k ~= main and n >= need and not skip[first[k]] and (target == nil or n > sizes[target]) then target = k end
		end
		if target == nil then break end

		local blocked, built, ATTEMPTS = {}, false, 4
		for attempt = 1, ATTEMPTS do
			-- The cheapest way across, from every tile of the biggest landmass.
			local cost, from, heap = {}, {}, PA_Heap()
			for i = 1, W * H do
				if id[i] == main then cost[i] = 0; heap.push(0, i) end
			end
			local reached = nil
			while true do
				local c, i = heap.pop()
				if c == nil then break end
				if c <= cost[i] then
					if id[i] == target then reached = i; break end
					for _, j in ipairs(nb(i)) do
						if not polar(j) and not blocked[j] then
							local step
							if id[j] ~= nil then step = 0.1
							elseif types[j] == MOUNTAIN then step = 2
							else step = 1 + 4 * ((depth and depth[j]) or 0) + 0.5 * PA_Rand() end
							local nc = c + step
							if cost[j] == nil or nc < cost[j] then cost[j] = nc; from[j] = i; heap.push(nc, j) end
						end
					end
				end
			end
			if reached == nil then break end
			local path, i = {}, reached
			while i ~= nil do path[#path + 1] = i; if id[i] == main then break end; i = from[i] end

			-- Raise the path: sea and mountains become land, so nothing on a bridge stops a unit.
			local changed, wet = {}, {}
			local function raise(j, flat)
				if types[j] == OCEAN then
					changed[j] = OCEAN
					types[j] = (not flat and Map.Rand(5, "Pulled Apart bridge hills") == 0) and HILLS or LAND
				elseif types[j] == MOUNTAIN then
					changed[j] = MOUNTAIN
					types[j] = HILLS
				end
			end
			for n, j in ipairs(path) do
				if types[j] == OCEAN then wet[#wet + 1] = n end
				raise(j, true)
			end
			local spot = nil
			if canal and #wet > 0 then
				local mid = wet[math.ceil(#wet / 2)]
				local order = {}
				for _, n in ipairs(wet) do order[#order + 1] = n end
				table.sort(order, function(x, y) return math.abs(x - mid) < math.abs(y - mid) end)
				for _, n in ipairs(order) do
					if n > 1 and n < #path and canalSpot(path[n], path[n - 1], path[n + 1]) then spot = path[n]; break end
				end
			end
			local near = {}
			if spot ~= nil then
				near[spot] = true
				for _, a in ipairs(nb(spot)) do near[a] = true; for _, b in ipairs(nb(a)) do near[b] = true end end
			end
			local onPath = {}
			for _, j in ipairs(path) do onPath[j] = true end
			for _, j in ipairs(path) do
				for _, k in ipairs(nb(j)) do
					if not polar(k) and not near[k] and not onPath[k] and PA_Rand() < 0.5 then raise(k, false) end
				end
			end
			-- No single tile may cut a bridge, so no one city on it closes the way: widen it wherever one would
			-- (a canal spot is the one tile left on purpose).
			if spot == nil then
				for pass = 1, 3 do
					local thin = false
					for _, j in ipairs(path) do
						if j ~= path[1] and j ~= path[#path] and not joined(path[1], path[#path], j) then
							thin = true
							for _, k in ipairs(nb(j)) do if not polar(k) then raise(k, false) end end
						end
					end
					if not thin then break end
				end
			end
			local placed = {}
			for j, was in pairs(changed) do if was == OCEAN then placed[j] = true end end
			-- Without a spot for its canal a bridge tries another way; on the last try it is built solid instead.
			local unfit = canal and #wet > 0 and spot == nil and attempt < ATTEMPTS
			-- A way that walls a sea off is tried last: on the final attempt the land is joined anyway.
			if unfit or (attempt < ATTEMPTS and PA_SplitsSea(types, placed, W, H, wrap)) then
				for j, was in pairs(changed) do types[j] = was end
				for _, n in ipairs(wet) do blocked[path[n]] = true end
			else
				for j in pairs(changed) do raised = raised + 1; tiles[#tiles + 1] = j end
				for _, j in ipairs(path) do if not changed[j] then tiles[#tiles + 1] = j end end
				bridges = bridges + 1
				if spot ~= nil then canals[#canals + 1] = spot end
				built = true
				break
			end
		end
		if not built then skip[first[target]] = true end
	end
	return bridges, raised, canals, tiles
end

function PA_CivCount()
	local n = 0
	if Players ~= nil and GameDefines ~= nil then
		for p = 0, GameDefines.MAX_MAJOR_CIVS - 1 do
			local player = Players[p]
			if player ~= nil and player:IsEverAlive() then n = n + 1 end
		end
	end
	return (n > 0) and n or 8
end

-- Runs on the map itself, after Lekmap's own channels (PinchTectonics) are cut.
-- Mountains beside open sea become hills, as Lekmap's feature step makes them later when its cliffs are off (they
-- are): done before the bridges are planned, so the land they join is the land the game ends with. Open sea is
-- water in a body of MIN_WATER_SIZE_FOR_OCEAN (10) tiles or more. Returns how many changed.
function PA_FlattenCoastalMountains(types, W, H, wrap)
	local OCEAN, MOUNTAIN = PlotTypes.PLOT_OCEAN, PlotTypes.PLOT_MOUNTAIN
	local body, size = {}, {}
	for i = 1, W * H do
		if types[i] == OCEAN and not body[i] then
			local k = #size + 1
			local stack, n = { i }, 0
			body[i] = k
			while #stack > 0 do
				local c = table.remove(stack)
				n = n + 1
				for _, j in ipairs(PA_Neighbours(c, W, H, wrap)) do
					if types[j] == OCEAN and not body[j] then body[j] = k; stack[#stack + 1] = j end
				end
			end
			size[k] = n
		end
	end
	local open = (GameDefines and GameDefines.MIN_WATER_SIZE_FOR_OCEAN) or 10
	local flat = {}
	for i = 1, W * H do
		if types[i] == MOUNTAIN then
			for _, j in ipairs(PA_Neighbours(i, W, H, wrap)) do
				if body[j] and size[body[j]] >= open then flat[#flat + 1] = i; break end
			end
		end
	end
	for _, i in ipairs(flat) do types[i] = PlotTypes.PLOT_HILLS end
	return #flat
end

PA_CANALS = {}  -- the canal spots of the map being made, kept clear of moved specks
PA_BRIDGE = {}  -- every bridge tile of the map being made, by plot index: no natural wonder or mountain goes there

function PA_ConnectLandmasses()
	PA_CANALS = {}
	PA_BRIDGE = {}
	if PA_DEPTH == nil or not PA_CONNECT then return end
	local W, H = Map.GetGridSize()
	local types, before = {}, {}
	for y = 0, H - 1 do
		for x = 0, W - 1 do
			local i = y * W + x + 1
			types[i] = Map.GetPlot(x, y):GetPlotType()
			before[i] = types[i]
		end
	end
	if beta_cliffs == 1 then PA_FlattenCoastalMountains(types, W, H, Map:IsWrapX()) end
	local bridges, raised, canals, tiles = PA_Bridge(types, PA_DEPTH, W, H, Map:IsWrapX(), PA_CivCount(), PA_CANAL)
	PA_CANALS = canals
	for _, i in ipairs(tiles) do PA_BRIDGE[i] = true end
	for i = 1, W * H do
		if types[i] ~= before[i] then
			local x = (i - 1) % W
			Map.GetPlot(x, (i - 1 - x) / W):SetPlotType(types[i])
			if BETA_TECTONIC_LANDS ~= nil and BETA_TECTONIC_LANDS[i] == 2 then BETA_TECTONIC_LANDS[i] = 0 end
		end
	end
	if bridges > 0 then Map.RecalculateAreas() end
	-- Every civ starts on the joined land: the game's one-landmass method puts them all on the biggest.
	PA_METHOD = 1
	local spots = {}
	for _, i in ipairs(canals) do
		local x = (i - 1) % W
		spots[#spots + 1] = x .. "," .. ((i - 1 - x) / W)
	end
	print("Lekmap Fusion: connect all continents: " .. bridges .. " bridges (" .. raised .. " tiles raised)" ..
		(PA_CANAL and ("; canal spots at " .. ((#spots > 0) and table.concat(spots, " ") or "none")) or "") .. "; every civ starts on the joined land")
end

------------------------------------------------------------------------------
-- Specks. A landmass under PA_MIN_ISLAND tiles stays where it is only when a city on a big
-- landmass could work it, PA_SPECK_REACH tiles or nearer. Any other speck is moved whole, its
-- shape kept, the shortest way that makes it touch one other landmass, and joins it: specks near
-- each other gather into one island. A speck with no land within PA_SPECK_PULL tiles, or no room
-- to move, sinks. No land is made. (Operator, 2026-11-21: no dotty specks; pull them together.)
PA_MIN_ISLAND = 4
PA_BIG_LANDMASS = 10
PA_SPECK_REACH = 3
PA_SPECK_PULL = 6
PA_MIN_SEA = 10 -- the game's MIN_WATER_SIZE_FOR_OCEAN: less water than this is a lake

-- Whether land just placed walls water off: the water around it now lies in two or more separate
-- bodies of PA_MIN_SEA tiles or more, so a coast that reached the ocean may no longer reach it.
-- A smaller pocket it closes off becomes a lake. placed: set of plot indices, already land in types.
function PA_SplitsSea(types, placed, W, H, wrap)
	local OCEAN = PlotTypes.PLOT_OCEAN
	local seen, seas = {}, 0
	for t in pairs(placed) do
		for _, a in ipairs(PA_Neighbours(t, W, H, wrap)) do
			if types[a] == OCEAN and not seen[a] then
				local n, stack = 0, { a }
				seen[a] = true
				while #stack > 0 do
					local c = table.remove(stack)
					n = n + 1
					for _, j in ipairs(PA_Neighbours(c, W, H, wrap)) do
						if types[j] == OCEAN and not seen[j] then seen[j] = true; stack[#stack + 1] = j end
					end
				end
				if n >= PA_MIN_SEA then
					seas = seas + 1
					if seas > 1 then return true end
				end
			end
		end
	end
	return false
end

-- types: plot types by plot index, changed in place; canals: canal spots whose sea must stay open.
-- Returns the specks moved and the specks sunk.
function PA_Magnetize(types, W, H, wrap, canals)
	local OCEAN = PlotTypes.PLOT_OCEAN
	local keep = {}
	for _, c in ipairs(canals or {}) do
		keep[c] = true
		for _, a in ipairs(PA_Neighbours(c, W, H, wrap)) do
			keep[a] = true
			for _, b in ipairs(PA_Neighbours(a, W, H, wrap)) do keep[b] = true end
		end
	end
	local P = PA_SPECK_PULL
	local function isLand(i) return types[i] ~= OCEAN end
	-- Axial hex coordinates (odd rows sit half a tile east), so a move keeps a speck's shape.
	local function axial(i)
		local x = (i - 1) % W
		local y = (i - 1 - x) / W
		return x - (y - y % 2) / 2, y
	end
	local function index(q, r)
		if r < 3 or r > H - 4 then return nil end -- polar rows stay sea
		local x = q + (r - r % 2) / 2
		if wrap then x = x % W elseif x < 0 or x >= W then return nil end
		return r * W + x + 1
	end
	local function landmasses()
		local id, members = {}, {}
		for i = 1, W * H do
			if isLand(i) and not id[i] then
				local k = #members + 1
				local list, stack = {}, { i }
				id[i] = k
				while #stack > 0 do
					local c = table.remove(stack)
					list[#list + 1] = c
					for _, j in ipairs(PA_Neighbours(c, W, H, wrap)) do
						if isLand(j) and not id[j] then id[j] = k; stack[#stack + 1] = j end
					end
				end
				members[k] = list
			end
		end
		return id, members
	end
	local function within(tiles, limit)
		local d, queue, head = {}, {}, 1
		for _, t in ipairs(tiles) do d[t] = 0; queue[#queue + 1] = t end
		while head <= #queue do
			local c = queue[head]
			head = head + 1
			if d[c] < limit then
				for _, j in ipairs(PA_Neighbours(c, W, H, wrap)) do
					if d[j] == nil then d[j] = d[c] + 1; queue[#queue + 1] = j end
				end
			end
		end
		return d
	end

	local moved, sunk = 0, 0
	while true do
		local id, members = landmasses()
		local big = {}
		for _, list in ipairs(members) do
			if #list >= PA_BIG_LANDMASS then for _, t in ipairs(list) do big[#big + 1] = t end end
		end
		local reach = within(big, PA_SPECK_REACH)
		local speck = nil
		for k, list in ipairs(members) do
			if #list < PA_MIN_ISLAND and (speck == nil or #list < #members[speck]) then
				local near = false
				for _, t in ipairs(list) do if reach[t] then near = true; break end end
				if not near then speck = k end
			end
		end
		if speck == nil then break end

		-- Lift the speck, then find the shortest move that sets it down touching one landmass only.
		local tiles, kinds, from = members[speck], {}, {}
		for n, t in ipairs(tiles) do
			kinds[n] = types[t]
			types[t] = OCEAN
			from[n] = { axial(t) }
		end
		local spots = {}
		for dr = -P, P do
			for dq = -P, P do
				local len = math.max(math.abs(dq), math.abs(dr), math.abs(dq + dr))
				if len >= 1 and len <= P then
					local spot, set = {}, {}
					for n = 1, #tiles do
						local j = index(from[n][1] + dq, from[n][2] + dr)
						if j == nil or isLand(j) or keep[j] then spot = nil; break end
						spot[n] = j
						set[j] = true
					end
					if spot ~= nil then
						local touch, count = {}, 0
						for _, j in ipairs(spot) do
							for _, a in ipairs(PA_Neighbours(j, W, H, wrap)) do
								if not set[a] and isLand(a) and not touch[id[a]] then touch[id[a]] = true; count = count + 1 end
							end
						end
						if count == 1 then spots[#spots + 1] = { len = len, tie = Map.Rand(1000, "Lekmap Fusion speck"), spot = spot, set = set } end
					end
				end
			end
		end
		table.sort(spots, function(a, b) if a.len ~= b.len then return a.len < b.len end return a.tie < b.tie end)
		local placed = false
		for _, s in ipairs(spots) do
			for n, j in ipairs(s.spot) do types[j] = kinds[n] end
			if not PA_SplitsSea(types, s.set, W, H, wrap) then placed = true; break end
			for _, j in ipairs(s.spot) do types[j] = OCEAN end
		end
		if placed then moved = moved + 1 else sunk = sunk + 1 end
	end
	return moved, sunk
end

-- Runs on the map itself, after the necks.
function PA_PullInSpecks()
	if PA_DEPTH == nil then return end
	local W, H = Map.GetGridSize()
	local types, before = {}, {}
	for y = 0, H - 1 do
		for x = 0, W - 1 do
			local i = y * W + x + 1
			types[i] = Map.GetPlot(x, y):GetPlotType()
			before[i] = types[i]
		end
	end
	local moved, sunk = PA_Magnetize(types, W, H, Map:IsWrapX(), PA_CANALS)
	for i = 1, W * H do
		if types[i] ~= before[i] then
			local x = (i - 1) % W
			Map.GetPlot(x, (i - 1 - x) / W):SetPlotType(types[i])
			if BETA_TECTONIC_LANDS ~= nil and BETA_TECTONIC_LANDS[i] == 2 then BETA_TECTONIC_LANDS[i] = 0 end
		end
	end
	if moved + sunk > 0 then Map.RecalculateAreas() end
	print("Lekmap Fusion: specks under " .. PA_MIN_ISLAND .. " tiles: " .. moved .. " pulled onto nearby land, " .. sunk .. " sunk")
end

------------------------------------------------------------------------------
-- Deep sea. Only water beside land stays shallow, so ships and embarked units without Astronomy follow the coasts and
-- cross where a channel is narrow, as near a bridge. An island PA_ISLAND_REACH tiles or nearer to other land keeps a
-- shallow way to it, the shortest one. Lakes keep their water.
PA_ISLAND_REACH = 4

-- types: plot types by plot index. Returns the set of water tiles that stay shallow.
function PA_DeepWater(types, W, H, wrap, civs)
	local OCEAN = PlotTypes.PLOT_OCEAN
	local shallow = {}
	for i = 1, W * H do
		if types[i] == OCEAN then
			for _, j in ipairs(PA_Neighbours(i, W, H, wrap)) do
				if types[j] ~= OCEAN then shallow[i] = true; break end
			end
		end
	end
	local id, members, total = {}, {}, 0
	for i = 1, W * H do
		if types[i] ~= OCEAN and not id[i] then
			local k = #members + 1
			local list, stack = {}, { i }
			id[i] = k
			while #stack > 0 do
				local c = table.remove(stack)
				list[#list + 1] = c
				for _, j in ipairs(PA_Neighbours(c, W, H, wrap)) do
					if types[j] ~= OCEAN and not id[j] then id[j] = k; stack[#stack + 1] = j end
				end
			end
			members[k] = list
			total = total + #list
		end
	end
	local need = math.max(20, math.floor(total / (2 * math.max(1, civs))))
	for k, list in ipairs(members) do
		if #list < need then
			-- The shortest way over water to other land, if it is near enough.
			local dist, from, queue, head, reached = {}, {}, {}, 1, nil
			for _, t in ipairs(list) do dist[t] = 0; queue[#queue + 1] = t end
			while head <= #queue and reached == nil do
				local c = queue[head]
				head = head + 1
				if dist[c] < PA_ISLAND_REACH then
					for _, j in ipairs(PA_Neighbours(c, W, H, wrap)) do
						if dist[j] == nil then
							dist[j] = dist[c] + 1
							from[j] = c
							if types[j] ~= OCEAN and id[j] ~= k then reached = j; break end
							if types[j] == OCEAN then queue[#queue + 1] = j end
						end
					end
				end
			end
			local c = reached and from[reached]
			while c ~= nil and types[c] == OCEAN do shallow[c] = true; c = from[c] end
		end
	end
	return shallow
end

-- Runs on the map itself, after the game's coasts are laid.
function PA_DeepSea()
	if PA_DEPTH == nil or not PA_DEEP then return end
	local W, H = Map.GetGridSize()
	local types = {}
	for y = 0, H - 1 do
		for x = 0, W - 1 do types[y * W + x + 1] = Map.GetPlot(x, y):GetPlotType() end
	end
	Map.RecalculateAreas()
	local shallow = PA_DeepWater(types, W, H, Map:IsWrapX(), PA_CivCount())
	local deepened = 0
	for i = 1, W * H do
		if types[i] == PlotTypes.PLOT_OCEAN then
			local x = (i - 1) % W
			local plot = Map.GetPlot(x, (i - 1 - x) / W)
			if not plot:IsLake() then
				local want = shallow[i] and GameDefines.SHALLOW_WATER_TERRAIN or GameDefines.DEEP_WATER_TERRAIN
				if plot:GetTerrainType() ~= want then
					if want == GameDefines.DEEP_WATER_TERRAIN then deepened = deepened + 1 end
					plot:SetTerrainType(want, false, false)
				end
			end
		end
	end
	print("Lekmap Fusion: deep sea: " .. deepened .. " shallow tiles made deep; water beside land stays shallow, and islands within " ..
		PA_ISLAND_REACH .. " tiles of other land keep a shallow way to it")
end

function GeneratePlotTypes()
	print("Generating Plot Types (Lua Pangaea, Lekmap Fusion) ...");
	local fractal_world = PangaeaFractalWorld.Create();
	local plotTypes = fractal_world:GeneratePlotTypes();
	PA_PullApart(plotTypes);
	SetPlotTypes(plotTypes);
	if beta_tectonic_mounts == 2 then
		PinchTectonics();
	end
	PA_ConnectLandmasses();
	PA_PullInSpecks();
	GenerateCoasts();
	PA_DeepSea();
end

------------------------------------------------------------------------------
-- Starts. Lekmap's start picker is kept, with these rules changed in its marked lines, so the
-- civs spread evenly over the land. Lekmap keeps an inland civ 3 or more tiles from any sea;
-- on a torn map that leaves only a few solid cores, and the civs crowd into them.
PA_START_MIN_LAND = 22            -- usable land (not mountains) among the 36 tiles within 3 of a start
PA_INLAND_ON_COAST = 0.05         -- weight of a coastal spot for an inland civ; Lekmap: never
PA_INLAND_NEXT_TO_COAST = 0.35    -- two tiles from the sea; Lekmap: 0.003
PA_INLAND_THREE_FROM_COAST = 0.7  -- three tiles from the sea; Lekmap: 0.01
PA_INLAND_PROX_PENALTY = false    -- Lekmap: no inland start within 2 tiles of the sea
PA_START_GAP = 0                  -- fewest tiles between two civ starts, set for each map
PA_LAND3 = {}                     -- land within 3 tiles, by plot index, for this map

local function PA_IsLand(plot)
	local t = plot:GetPlotType()
	return t == PlotTypes.PLOT_LAND or t == PlotTypes.PLOT_HILLS
end

local function PA_XY(i)
	local W = Map.GetGridSize()
	local x = (i - 1) % W
	return x, (i - 1 - x) / W
end

-- Tiles between two plots on the hex grid (odd rows sit half a tile east), around the wrap.
function PA_HexDistance(x1, y1, x2, y2)
	local W = Map.GetGridSize()
	local function q(x, y) return x - (y - y % 2) / 2 end
	local best = math.huge
	for _, shift in ipairs(Map:IsWrapX() and { -W, 0, W } or { 0 }) do
		local dq, dr = q(x1 + shift, y1) - q(x2, y2), y1 - y2
		best = math.min(best, math.max(math.abs(dq), math.abs(dr), math.abs(dq + dr)))
	end
	return best
end

function PA_Land3(i)
	if PA_LAND3[i] ~= nil then return PA_LAND3[i] end
	local W, H = Map.GetGridSize()
	local x, y = PA_XY(i)
	local n = 0
	for ny = math.max(0, y - 3), math.min(H - 1, y + 3) do
		for dx = -4, 4 do
			local nx = x + dx
			if Map:IsWrapX() then nx = nx % W end
			if nx >= 0 and nx < W then
				local d = PA_HexDistance(x, y, nx, ny)
				if d >= 1 and d <= 3 and PA_IsLand(Map.GetPlot(nx, ny)) then n = n + 1 end
			end
		end
	end
	PA_LAND3[i] = n
	return n
end

-- The gap is 80% of the spacing the civs would have if each held an equal hexagon of the land.
function PA_StartGap(civs, usable)
	if civs < 2 then return 0 end
	local per = usable / civs
	local r = (-3 + math.sqrt(math.max(0, 9 - 12 * (1 - per)))) / 6 -- a hexagon of radius r holds 3r^2+3r+1 tiles
	return math.max(5, math.floor(0.8 * (2 * r + 1)))
end

function PA_NearestStart(i, region, starts)
	local x, y = PA_XY(i)
	local nearest = math.huge
	for r, j in pairs(starts) do
		if r ~= region and j ~= nil and j > 0 then
			local sx, sy = PA_XY(j)
			nearest = math.min(nearest, PA_HexDistance(x, y, sx, sy))
		end
	end
	return nearest
end

local function PA_OwnArea(iAreaID, iBiggestAreaID)
	if iAreaID ~= nil and iAreaID >= 0 then return iAreaID end
	return iBiggestAreaID
end

-- Called by Lekmap's picker for every spot it scores: false scores the spot 0.
function PA_StartAllowed(plot, i, iAreaID, iBiggestAreaID, region, starts)
	if plot:GetArea() ~= PA_OwnArea(iAreaID, iBiggestAreaID) then return false end
	if not PA_OnJoinedLand(i) then return false end
	if PA_Land3(i) < PA_START_MIN_LAND then return false end
	return PA_NearestStart(i, region, starts) >= PA_START_GAP
end

-- Called with the picker's best spot; when no spot scored, the start goes to the spot in the
-- region farthest from the other civs (counting up to the gap), then the one with the most land.
function PA_PickStart(iBest, scoreBest, region, iWestX, iSouthY, iWidth, iHeight, iAreaID, iBiggestAreaID, starts)
	if scoreBest > 0 then return iBest end
	local W, H = Map.GetGridSize()
	local own = PA_OwnArea(iAreaID, iBiggestAreaID)
	local pick, pickKey = iBest, -1
	for dy = 0, iHeight - 1 do
		for dx = 0, iWidth - 1 do
			local x, y = (iWestX + dx) % W, (iSouthY + dy) % H
			local plot = Map.GetPlot(x, y)
			if PA_IsLand(plot) and plot:GetArea() == own and PA_OnJoinedLand(y * W + x + 1) then
				local i = y * W + x + 1
				local land = PA_Land3(i)
				local key = math.min(PA_NearestStart(i, region, starts), PA_START_GAP) * 10000 + math.min(land, PA_START_MIN_LAND) * 100 + land
				if key > pickKey then pick, pickKey = i, key end
			end
		end
	end
	local x, y = PA_XY(pick)
	local nearest = PA_NearestStart(pick, region, starts)
	print(string.format("Lekmap Fusion: no spot in region %d met every start rule; it starts at %d,%d with %d land tiles within 3, nearest civ %s tiles",
		region, x, y, PA_Land3(pick), (nearest == math.huge) and "none" or tostring(nearest)))
	return pick
end

-- With Connect all continents, a civ starts only on the biggest walkable landmass: the one the bridges joined.
PA_WALK, PA_WALK_MAIN = nil, nil
function PA_OnJoinedLand(i)
	if not PA_CONNECT or PA_WALK == nil then return true end
	return PA_WALK[i] == PA_WALK_MAIN
end

-- Walkable landmasses of the map as it is now: plot index -> landmass number, and the biggest one's number.
function PA_WalkableLand()
	local W, H = Map.GetGridSize()
	local wrap = Map:IsWrapX()
	local id, sizes = {}, {}
	local function walk(i)
		local x = (i - 1) % W
		return PA_IsLand(Map.GetPlot(x, (i - 1 - x) / W))
	end
	for i = 1, W * H do
		if id[i] == nil and walk(i) then
			local k = #sizes + 1
			local stack, n = { i }, 0
			id[i] = k
			while #stack > 0 do
				local c = table.remove(stack)
				n = n + 1
				for _, j in ipairs(PA_Neighbours(c, W, H, wrap)) do
					if id[j] == nil and walk(j) then id[j] = k; stack[#stack + 1] = j end
				end
			end
			sizes[k] = n
		end
	end
	local main = nil
	for k, n in ipairs(sizes) do if main == nil or n > sizes[main] then main = k end end
	return id, main
end

function PA_SetStartRules(regions)
	local W, H = Map.GetGridSize()
	if PA_CONNECT then PA_WALK, PA_WALK_MAIN = PA_WalkableLand() else PA_WALK, PA_WALK_MAIN = nil, nil end
	local areas, anyArea = {}, false
	for _, r in ipairs(regions) do
		if r[5] ~= nil and r[5] >= 0 then areas[r[5]] = true else anyArea = true end
	end
	local usable = 0
	for y = 0, H - 1 do
		for x = 0, W - 1 do
			local plot = Map.GetPlot(x, y)
			if PA_IsLand(plot) and (anyArea or areas[plot:GetArea()]) then usable = usable + 1 end
		end
	end
	PA_LAND3 = {}
	PA_START_GAP = PA_StartGap(#regions, usable)
	print("Lekmap Fusion starts: " .. #regions .. " civs on " .. usable .. " usable tiles; at least " .. PA_START_GAP ..
		" tiles apart, with " .. PA_START_MIN_LAND .. "+ land tiles within 3 of each start")
end

local PA_LekmapGenerateRegions = AssignStartingPlots.GenerateRegions
function AssignStartingPlots:GenerateRegions(args)
	args = args or {}
	if PA_METHOD ~= nil then args.method = PA_METHOD end
	local result = PA_LekmapGenerateRegions(self, args)
	PA_SetStartRules(self.regionData)
	return result
end

local PA_LekmapPlaceResourcesAndCityStates = AssignStartingPlots.PlaceResourcesAndCityStates
function AssignStartingPlots:PlaceResourcesAndCityStates()
	PA_LekmapPlaceResourcesAndCityStates(self)
	return true
end

-- After the map is made, it is written to Lua.log row by row with every start marked, so a map
-- can be looked at outside the game (one line per row, north first).
function PA_DumpMap()
	local W, H = Map.GetGridSize()
	local marks, letters = {}, "ABCDEFGHIJKLMNOPQRSTUV"
	for p = 0, GameDefines.MAX_CIV_PLAYERS - 1 do
		local player = Players[p]
		if player ~= nil and player:IsEverAlive() then
			local plot = player:GetStartingPlot()
			if plot ~= nil then
				marks[plot:GetY() * W + plot:GetX() + 1] = (p < GameDefines.MAX_MAJOR_CIVS) and string.sub(letters, p + 1, p + 1) or "*"
			end
		end
	end
	print("Lekmap Fusion map: " .. W .. "x" .. H .. ", north first; ~ ocean, - coast, o lake, # land, n hills, ^ mountains, A-V civ starts by player number, * city-state starts")
	for y = H - 1, 0, -1 do
		local row = {}
		for x = 0, W - 1 do
			local plot = Map.GetPlot(x, y)
			local c = marks[y * W + x + 1]
			if c == nil then
				if plot:IsLake() then c = "o"
				elseif plot:IsWater() then c = (plot:GetTerrainType() == TerrainTypes.TERRAIN_COAST) and "-" or "~"
				elseif plot:IsMountain() then c = "^"
				elseif plot:IsHills() then c = "n"
				else c = "#" end
			end
			row[#row + 1] = c
		end
		print(string.format("Lekmap Fusion map %02d: %s", y, table.concat(row)))
	end
end

-- Mountains. Lekmap's Pangaea makes peaks of the top 15% of its ridge field, Lekmap Continents of the
-- top 30%: ranges where Pangaea leaves specks. Called from Lekmap's mountain line, marked "Lekmap Fusion".
function PA_MountainPercentile(p)
	local S = PA_Settings()
	if S.on == 1 and S.mountains == 1 then return p - 15 end
	return p
end

-- No natural wonder on a bridge: some turn their tile into a mountain.
local PA_LekmapWonderEligible = AssignStartingPlots.ExaminePlotForNaturalWondersEligibility
function AssignStartingPlots:ExaminePlotForNaturalWondersEligibility(x, y)
	local W = Map.GetGridSize()
	if PA_BRIDGE[y * W + x + 1] then return false end
	return PA_LekmapWonderEligible(self, x, y)
end

-- Once the whole map is made: any bridge tile a later step made a mountain is made hills again, and
-- every civ start is checked to be on one walkable landmass. The log says which, either way.
function PA_CheckBridges()
	if not PA_CONNECT or PA_DEPTH == nil then return end
	local W, H = Map.GetGridSize()
	local fixed = 0
	for i in pairs(PA_BRIDGE) do
		local x = (i - 1) % W
		local plot = Map.GetPlot(x, (i - 1 - x) / W)
		if plot:IsMountain() and plot:GetFeatureType() == FeatureTypes.NO_FEATURE then
			plot:SetPlotType(PlotTypes.PLOT_HILLS, false, true)
			fixed = fixed + 1
		end
	end
	local id, main = PA_WalkableLand()
	local apart = {}
	for p = 0, GameDefines.MAX_MAJOR_CIVS - 1 do
		local player = Players[p]
		if player ~= nil and player:IsEverAlive() then
			local plot = player:GetStartingPlot()
			if plot ~= nil and id[plot:GetY() * W + plot:GetX() + 1] ~= main then apart[#apart + 1] = string.sub("ABCDEFGHIJKLMNOPQRSTUV", p + 1, p + 1) end
		end
	end
	print("Lekmap Fusion: bridges checked: " .. fixed .. " mountains on bridges made hills; " ..
		((#apart == 0) and "every civ can walk to every other" or ("WARNING civs " .. table.concat(apart, " ") .. " cannot walk to the rest")))
end

local PA_LekmapGenerateMap = GenerateMap
function GenerateMap()
	PA_LekmapGenerateMap()
	local checked, why = pcall(PA_CheckBridges)
	if not checked then print("Lekmap Fusion: WARNING could not check the bridges: " .. tostring(why)) end
	local ok, err = pcall(PA_DumpMap)
	if not ok then print("Lekmap Fusion: WARNING could not write the map to the log: " .. tostring(err)) end
end
