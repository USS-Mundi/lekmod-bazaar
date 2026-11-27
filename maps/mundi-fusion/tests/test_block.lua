-- Run from the mundi-fusion folder: luajit tests/test_block.lua [seed] [show|map]
BLOCK = "src/pulled_apart_block.lua"
-- Stand-ins for Civ's map API, enough to run the Pulled Apart block outside the game.
math.randomseed(tonumber(arg[1] or "7"))
RAND_STATE = tonumber(arg[1] or "7")
PlotTypes = { PLOT_MOUNTAIN = 0, PLOT_HILLS = 1, PLOT_LAND = 2, PLOT_OCEAN = 3 }
local W, H = 64, 58
local LEKMAP_OPTIONS = 30
CHOICES = {}
Map = {
  GetGridSize = function() return W, H end,
  IsWrapX = function() return true end,
  -- The game's Map.Rand (CvRandom::get as LEKMOD builds it): a 32-bit product, kept to 16 bits, so a range above
  -- 65536 never comes back above 65535.
  Rand = function(n)
    RAND_STATE = (214013 * RAND_STATE + 2531011) % 4294967296
    local hi = math.floor(RAND_STATE / 65536) % 65536
    return math.floor(((hi * n) % 4294967296) / 65536) % 65536
  end,
  GetCustomOption = function(i) return CHOICES[i] end,
  GetFractalFlags = function() return 1 end,
}
local function smooth(t) return t * t * (3 - 2 * t) end
Fractal = { Create = function(w, h, grain)
  local cells = 2 ^ grain
  local g = {}
  for j = 0, cells + 1 do g[j] = {} for k = 0, cells do g[j][k] = math.random() end end
  return { GetHeight = function(self, x, y)
    local fx, fy = x / w * cells, y / h * cells
    local i0, j0 = math.floor(fx), math.floor(fy)
    local tx, ty = smooth(fx - i0), smooth(fy - j0)
    local a, b = i0 % cells, (i0 + 1) % cells
    local v = (g[j0][a] * (1 - tx) + g[j0][b] * tx) * (1 - ty) + (g[j0 + 1][a] * (1 - tx) + g[j0 + 1][b] * tx) * ty
    return math.floor(v * 255)
  end }
end }
function GetMapScriptInfo()
  local o = {}
  for k = 1, LEKMAP_OPTIONS do o[k] = { Name = "lek" .. k } end
  -- three of Lekmap's own, where Lekmap v6.3 has them, with its values and defaults
  local size = { "Default -8 tiles", "Default -6 tiles", "Default -4 tiles", "Default -2 tiles", "Default", "Default +2 tiles",
    "Default +4 tiles", "Default +6 tiles", "Default +8 tiles", "Default +10 tiles", "Default +12 tiles" }
  o[13] = { Name = "TXT_KEY_MAP_OPTION_SEA_LEVEL", Values = { "TXT_KEY_MAP_OPTION_LOW", "TXT_KEY_MAP_OPTION_MEDIUM", "TXT_KEY_MAP_OPTION_HIGH", "TXT_KEY_MAP_OPTION_RANDOM" }, DefaultValue = 2 }
  o[20] = { Name = "Land Size X", Values = size, DefaultValue = 5 }
  o[21] = { Name = "Land Size Y", Values = size, DefaultValue = 5 }
  return { Name = "Lekmap v6.3", CustomOptions = o }
end
AssignStartingPlots = { GenerateRegions = function(self, args) return args.method end, PlaceResourcesAndCityStates = function() return false end }
dofile(BLOCK)

-- The setup screen's view of the options.
local info = GetMapScriptInfo()
assert(info.Name == "Mundi Fusion")
for k = LEKMAP_OPTIONS + 1, #info.CustomOptions do
  local o = info.CustomOptions[k]
  CHOICES[k] = o.DefaultValue
  if arg[2] == "show" then print(string.format("%-26s default: %s  (%d values)", o.Name, o.Values[o.DefaultValue], #o.Values)) end
end
assert(info.CustomOptions[LEKMAP_OPTIONS + 6].Values[#info.CustomOptions[LEKMAP_OPTIONS + 6].Values] == "10.5 tiles", "width tops out at 10.5")
assert(AssignStartingPlots:PlaceResourcesAndCityStates() == true)

-- A stand-in Pangaea: one blob of land, 33% of the map.
local plotTypes, vals = {}, {}
local f = Fractal.Create(W, H, 2)
for y = 0, H - 1 do for x = 0, W - 1 do
  local ex, ey = (x - W / 2) / (W * 0.4), (y - H / 2) / (H * 0.43)
  vals[#vals + 1] = { i = y * W + x + 1, v = f:GetHeight(x, y) / 255 - 0.55 * (ex * ex + ey * ey) ^ 1.2 - ((y < 2 or y > H - 3) and 9 or 0) }
end end
table.sort(vals, function(a, b) return a.v > b.v end)
for k, e in ipairs(vals) do plotTypes[e.i] = (k <= math.floor(W * H * 0.33)) and PlotTypes.PLOT_LAND or PlotTypes.PLOT_OCEAN end
PA_PullApart(plotTypes)
-- After the specks pass, no landmass under 4 tiles is left out of a city's reach of a big landmass.
PA_Magnetize(plotTypes, W, H, true)
do
  local id, sizes, n = {}, {}, 0
  for i = 1, W * H do
    if plotTypes[i] ~= PlotTypes.PLOT_OCEAN and not id[i] then
      n = n + 1
      local stack = { i }
      id[i] = n; sizes[n] = 0
      while #stack > 0 do
        local c = table.remove(stack)
        sizes[n] = sizes[n] + 1
        for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
          if plotTypes[j] ~= PlotTypes.PLOT_OCEAN and not id[j] then id[j] = n; stack[#stack + 1] = j end
        end
      end
    end
  end
  local near, queue, head = {}, {}, 1
  for i = 1, W * H do if id[i] and sizes[id[i]] >= PA_BIG_LANDMASS then near[i] = 0; queue[#queue + 1] = i end end
  while head <= #queue do
    local c = queue[head]; head = head + 1
    if near[c] < PA_SPECK_REACH then
      for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do if near[j] == nil then near[j] = near[c] + 1; queue[#queue + 1] = j end end
    end
  end
  local reached = {}
  for i = 1, W * H do if id[i] and near[i] then reached[id[i]] = true end end
  for k = 1, n do assert(sizes[k] >= PA_MIN_ISLAND or reached[k], "a speck of " .. sizes[k] .. " left out of reach") end
end
if arg[2] == "map" then
  for y = H - 1, 0, -1 do
    local row = (y % 2 == 1) and " " or ""
    for x = 0, W - 1 do row = row .. ((plotTypes[y * W + x + 1] == PlotTypes.PLOT_OCEAN) and "~" or "#") end
    print(row)
  end
end

-- The map dump, against a stand-in map with two starts.
GameDefines = { MAX_MAJOR_CIVS = 22, MAX_CIV_PLAYERS = 63 }
TerrainTypes = { TERRAIN_COAST = 5 }
local function plotAt(x, y)
  local t = plotTypes[y * W + x + 1]
  return { GetX = function() return x end, GetY = function() return y end,
    IsLake = function() return false end, IsWater = function() return t == PlotTypes.PLOT_OCEAN end,
    GetTerrainType = function() return 6 end, IsMountain = function() return t == PlotTypes.PLOT_MOUNTAIN end,
    IsHills = function() return t == PlotTypes.PLOT_HILLS end }
end
Map.GetPlot = plotAt
Players = { [0] = { IsEverAlive = function() return true end, GetStartingPlot = function() return plotAt(30, 30) end },
            [30] = { IsEverAlive = function() return true end, GetStartingPlot = function() return plotAt(10, 10) end } }
local printed = {}
local realPrint = print
print = function(s) printed[#printed + 1] = s end
GenerateMap = function() end
dofile(BLOCK)  -- re-load so GenerateMap wraps the stand-in
GenerateMap()
print = realPrint
assert(#printed == H + 1, "a header and one line per row, got " .. #printed)
assert(printed[2]:find("map 57: ", 1, true), "north row first")
assert(printed[H + 1 - 30]:sub(-64):sub(31, 31) == "A", "player 0's start marked A")
assert(printed[H + 1 - 10]:sub(-64):sub(11, 11) == "*", "a city-state start marked *")
print("map dump: ok (" .. #printed .. " lines)")

-- The start rules, on a stand-in world: landmass 1 is the block x 10..40, y 10..40, landmass 2 the
-- block x 50..55, y 10..15, everything else ocean.
PlotTypes.PLOT_HILLS = 1
local function areaOf(x, y)
  if x >= 10 and x <= 40 and y >= 10 and y <= 40 then return 1 end
  if x >= 50 and x <= 55 and y >= 10 and y <= 15 then return 2 end
  return -1
end
Map.GetPlot = function(x, y)
  local a = areaOf(x, y)
  return { GetArea = function() return a end,
    GetPlotType = function() return (a > 0) and PlotTypes.PLOT_LAND or PlotTypes.PLOT_OCEAN end }
end
local function idx(x, y) return y * W + x + 1 end

assert(PA_HexDistance(0, 0, 1, 0) == 1 and PA_HexDistance(0, 0, 0, 1) == 1 and PA_HexDistance(0, 1, 1, 0) == 1, "neighbours are 1 apart")
assert(PA_HexDistance(0, 0, 0, 6) == 6, "six rows north is 6")
assert(PA_HexDistance(0, 10, 63, 10) == 1, "the map wraps east to west")
assert(PA_StartGap(8, 791) == 9, "8 civs on 791 tiles start 9+ apart, got " .. PA_StartGap(8, 791))
assert(PA_StartGap(8, 120) == 5, "never under 5")

-- The game's own region division runs first; the rules are set from the land the regions hold.
AssignStartingPlots.GenerateRegions = function(self, args) self.regionData = { {10, 10, 31, 31, 1}, {50, 10, 6, 6, 2} } return args.method end
dofile(BLOCK)
local printed2 = {}
print = function(s) printed2[#printed2 + 1] = s end
assert(AssignStartingPlots:GenerateRegions({ method = 1 }) == 1, "the game's method passes through when the land is whole")
print = realPrint
assert(PA_START_GAP == PA_StartGap(2, 31 * 31 + 36), "gap from both landmasses' land, got " .. PA_START_GAP)
assert(printed2[1]:find("2 civs on 997 usable tiles", 1, true), printed2[1])

assert(PA_Land3(idx(25, 25)) == 36, "inland: all 36 tiles are land")
assert(PA_Land3(idx(10, 25)) < 36, "on the west shore some are sea")

PA_START_GAP = 9
local starts = { [1] = idx(20, 20) }
local function allowed(x, y, areaID, region) return PA_StartAllowed(Map.GetPlot(x, y), idx(x, y), areaID, 1, region, starts) end
assert(allowed(30, 20, 1, 2), "far enough, enough land, its own landmass")
assert(not allowed(24, 20, 1, 2), "4 tiles from another civ")
assert(allowed(24, 20, 1, 1), "a region's own earlier start does not count")
assert(not allowed(52, 12, 1, 2), "not its landmass")
assert(PA_Land3(idx(10, 10)) < 22 and not allowed(10, 10, 1, 2), "a corner spot with too little land around it")
assert(allowed(30, 20, -1, 2), "a region with no landmass of its own uses the biggest")

-- When nothing passed, the start goes to the spot farthest from the other civs (up to the gap), then the one with the most land.
assert(PA_PickStart(idx(30, 30), 5, 2, 10, 10, 31, 31, 1, 1, starts) == idx(30, 30), "a scored spot is kept")
print = function() end
local pick = PA_PickStart(idx(10, 10), 0, 2, 18, 18, 8, 8, 1, 1, starts)
print = realPrint
local px = (pick - 1) % W
local py = (pick - 1 - px) / W
assert(PA_HexDistance(px, py, 20, 20) >= 5 and PA_Land3(pick) == 36, "fallback moved off the other civ to full land, got " .. px .. "," .. py)
print("start rules: ok")

-- Necks of land: a landmass big enough for a civ is joined to the biggest one; a small island is not.
local OCEAN_T, LAND_T = PlotTypes.PLOT_OCEAN, PlotTypes.PLOT_LAND
local function world(thread)
  local t = {}
  for i = 1, W * H do t[i] = OCEAN_T end
  for y = 20, 35 do for x = 10, 25 do t[idx(x, y)] = LAND_T end end   -- the biggest landmass
  for y = 20, 30 do for x = 35, 45 do t[idx(x, y)] = LAND_T end end   -- big enough for a civ
  for y = 40, 41 do for x = 55, 56 do t[idx(x, y)] = LAND_T end end   -- a small island
  if thread then for x = 26, 34 do t[idx(x, 27)] = LAND_T end end     -- joined only by a one-tile strip
  return t
end
local function reach(t, from, to, solidOnly)
  local function ok(i)
    if t[i] ~= LAND_T and t[i] ~= PlotTypes.PLOT_HILLS then return false end
    if not solidOnly then return true end
    local n = 0
    for _, j in ipairs(PA_NeighbourList(i, W, H, true)) do if t[j] == LAND_T or t[j] == PlotTypes.PLOT_HILLS then n = n + 1 end end
    return n >= 3
  end
  local seen, stack = { [from] = true }, { from }
  while #stack > 0 do
    local c = table.remove(stack)
    if c == to then return true end
    for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
      if not seen[j] and ok(j) then seen[j] = true; stack[#stack + 1] = j end
    end
  end
  return false
end
local function sea() local t = {} for i = 1, W * H do t[i] = OCEAN_T end return t end
local function fill(t, x1, x2, y1, y2) for y = y1, y2 do for x = x1, x2 do t[idx(x, y)] = LAND_T end end end
local function seaCount(tt)
  local seen, n = {}, 0
  for i = 1, W * H do
    if tt[i] == OCEAN_T and not seen[i] then
      local stack, size = { i }, 0
      seen[i] = true
      while #stack > 0 do
        local c = table.remove(stack)
        size = size + 1
        for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
          if tt[j] == OCEAN_T and not seen[j] then seen[j] = true; stack[#stack + 1] = j end
        end
      end
      if size >= PA_MIN_SEA then n = n + 1 end
    end
  end
  return n
end
do
  local t = world(false)
  local bridges, raised, canals, tiles = PA_Bridge(t, nil, W, H, true, 4, false)
  assert(bridges == 1 and raised > 0 and #canals == 0, "one solid bridge, got " .. bridges .. " bridges, " .. raised .. " tiles")
  assert(reach(t, idx(15, 25), idx(40, 25), false), "the two landmasses are joined by land")
  for _, j in ipairs(tiles) do
    assert(t[j] == LAND_T or t[j] == PlotTypes.PLOT_HILLS, "no mountain or sea on a bridge")
    local was = t[j]; t[j] = OCEAN_T
    assert(reach(t, idx(15, 25), idx(40, 25), false), "no single tile cuts the bridge")
    t[j] = was
  end
  assert(not reach(t, idx(15, 25), idx(55, 40), false), "the small island stays apart")
  assert(seaCount(t) == 1, "the sea stays one body")
  for x = 0, W - 1 do for _, y in ipairs({ 0, 1, H - 2, H - 1 }) do assert(t[idx(x, y)] == OCEAN_T, "polar rows stay sea") end end
end
do
  local t = world(false)
  local bridges, raised, canals = PA_Bridge(t, nil, W, H, true, 4, true)
  assert(bridges == 1 and #canals == 1, "with canal spots: one bridge, one spot")
  local c = canals[1]
  assert(t[c] == LAND_T, "the canal spot is flat land, for a city")
  local wet = 0
  for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do if t[j] == OCEAN_T then wet = wet + 1 end end
  assert(wet >= 2, "the sea touches the canal spot")
  t[c] = OCEAN_T
  assert(not reach(t, idx(15, 25), idx(40, 25), false), "the bridge is one tile wide there: a city on it is a canal")
end
-- A wall of mountains that cuts a landmass in two gets a pass of hills, so no civ is shut in.
do
  local t = sea()
  fill(t, 10, 40, 20, 35)
  for y = 20, 35 do t[idx(25, y)] = PlotTypes.PLOT_MOUNTAIN; t[idx(26, y)] = PlotTypes.PLOT_MOUNTAIN end
  assert(not reach(t, idx(15, 25), idx(35, 25), false))
  local bridges, raised, canals, tiles = PA_Bridge(t, nil, W, H, true, 4, false)
  assert(bridges == 1 and reach(t, idx(15, 25), idx(35, 25), false), "a pass joins the two halves")
  for _, j in ipairs(tiles) do assert(t[j] ~= PlotTypes.PLOT_MOUNTAIN, "no mountain on the pass") end
end
print("bridges: ok")

-- Shallow fords: the same crossing left as water, which land units walk and ships sail; the sea stays one body.
local function walkReach(t, ford, from, to, without)
  local function ok(i) return i ~= without and (t[i] == LAND_T or t[i] == PlotTypes.PLOT_HILLS or ford[i]) end
  local seen, stack = { [from] = true }, { from }
  while #stack > 0 do
    local c = table.remove(stack)
    if c == to then return true end
    for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
      if not seen[j] and ok(j) then seen[j] = true; stack[#stack + 1] = j end
    end
  end
  return false
end
local function clusters(set)
  local seen, n = {}, 0
  for i in pairs(set) do
    if not seen[i] then
      n = n + 1
      local stack = { i }
      seen[i] = true
      while #stack > 0 do
        local c = table.remove(stack)
        for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do if set[j] and not seen[j] then seen[j] = true; stack[#stack + 1] = j end end
      end
    end
  end
  return n
end
do
  local t = world(false)
  local land = 0
  for i = 1, W * H do if t[i] ~= OCEAN_T then land = land + 1 end end
  local crossings, raised, canals, tiles, fords = PA_Bridge(t, nil, W, H, true, 4, true, true, 1)
  assert(crossings == 1 and raised == 0 and #canals == 0 and #fords > 0, "one ford, nothing raised, no canal spot: got " .. crossings .. ", " .. raised .. ", " .. #canals .. ", " .. #fords)
  local ford, after = {}, 0
  for _, j in ipairs(fords) do ford[j] = true; assert(t[j] == OCEAN_T, "a ford stays water") end
  for i = 1, W * H do if t[i] ~= OCEAN_T then after = after + 1 end end
  assert(after == land, "no land is made")
  for _, j in ipairs(tiles) do assert(ford[j], "a ford's tiles are its shallows only") end
  assert(walkReach(t, ford, idx(15, 25), idx(40, 25)), "the two landmasses are joined over the shallows")
  for _, j in ipairs(fords) do assert(walkReach(t, ford, idx(15, 25), idx(40, 25), j), "no single tile cuts the ford") end
  assert(not walkReach(t, ford, idx(15, 25), idx(55, 40)), "the small island stays apart")
  assert(seaCount(t) == 1, "the sea stays one body")
end
-- Two crossings for each continent, apart from each other; the default is one.
do
  local t = sea()
  fill(t, 10, 25, 8, 48); fill(t, 31, 45, 8, 48)               -- two tall continents across a channel 5 tiles wide
  local one = PA_Bridge(t, nil, W, H, true, 4, false, true, 1)
  assert(one == 1, "one ford by default, got " .. one)
  t = sea()
  fill(t, 10, 25, 8, 48); fill(t, 31, 45, 8, 48)
  local two, _, _, _, fords = PA_Bridge(t, nil, W, H, true, 4, false, true, 2)
  local ford = {}
  for _, j in ipairs(fords) do ford[j] = true end
  assert(two == 2 and clusters(ford) == 2, "two fords, apart: got " .. two .. " in " .. clusters(ford) .. " groups")
  -- with land bridges, the second one would wall the channel between them off: it is not built
  t = sea()
  fill(t, 10, 25, 8, 48); fill(t, 31, 45, 8, 48)
  local bridges = PA_Bridge(t, nil, W, H, true, 4, false, false, 2)
  assert(bridges == 1 and seaCount(t) == 1, "a second bridge that walls water off is not built, got " .. bridges)
end
print("fords: ok")

-- Inland seas: a sea the land shuts in gets a channel to the ocean, the shortest way, never through a canal spot.
do
  local t = sea()
  fill(t, 15, 45, 15, 45)
  for y = 19, 41 do for x = 19, 41 do t[idx(x, y)] = OCEAN_T end end -- a sea shut in by a ring of land 4 tiles thick
  for x = 42, 45 do t[idx(x, 30)] = PlotTypes.PLOT_MOUNTAIN end         -- mountains on one of the ways east
  assert(seaCount(t) == 2)
  local cut, opened, left = PA_OpenSeaPlan(t, W, H, true, {})
  assert(opened == 1 and left == 0 and #cut >= 1 and #cut <= PA_CHANNEL_MAX, "one channel, " .. #cut .. " tiles")
  assert(seaCount(t) == 1, "the sea reaches the ocean")
  for _, j in ipairs(cut) do assert(t[j] == OCEAN_T, "the channel is water") end
  local thick = sea()
  fill(thick, 5, 55, 8, 50)
  for y = 27, 31 do for x = 27, 33 do thick[idx(x, y)] = OCEAN_T end end -- 35 tiles of sea, 20 tiles of land from the ocean
  local cut2, opened2, left2 = PA_OpenSeaPlan(thick, W, H, true, {})
  assert(#cut2 == 0 and opened2 == 0 and left2 == 1, "a sea far inland stays shut")
end
print("inland seas: ok")

-- In the game: after Lekmap's features, each ford still open water gets the shallows feature on shallow water.
do
  local saved = { Map.GetPlot, FeatureTypes, GameDefines, AddFeatures }
  FeatureTypes = { NO_FEATURE = -1, FEATURE_SHALLOWS = 7 }
  GameDefines = { SHALLOW_WATER_TERRAIN = 5, DEEP_WATER_TERRAIN = 6, MAX_MAJOR_CIVS = 22, MAX_CIV_PLAYERS = 63 }
  local plots = {}
  local function plot(x, y)
    local i = y * W + x + 1
    if not plots[i] then
      local water = (x ~= 3)
      plots[i] = { terrain = 6, feature = -1, IsWater = function() return water end, IsLake = function() return x == 4 end,
        GetTerrainType = function(self) return self.terrain end, SetTerrainType = function(self, t) self.terrain = t end,
        SetFeatureType = function(self, f) self.feature = f end, GetFeatureType = function(self) return self.feature end }
    end
    return plots[i]
  end
  Map.GetPlot = plot
  local order = {}
  local lekmapAddFeatures = function() order[#order + 1] = "lekmap" end
  PA_FORD = { [idx(1, 10)] = true, [idx(2, 10)] = true, [idx(3, 10)] = true, [idx(4, 10)] = true }
  PA_LayFords()
  assert(plot(1, 10).feature == 7 and plot(1, 10).terrain == 5 and plot(2, 10).feature == 7, "open water gets shallows, on shallow water")
  assert(plot(3, 10).feature == -1 and plot(4, 10).feature == -1, "land and lakes do not")
  assert(PA_IsShallows(plot(1, 10)) and not PA_IsShallows(plot(3, 10)), "shallows are told apart")
  PA_FORD = {}
  Map.GetPlot, FeatureTypes, GameDefines, AddFeatures = saved[1], saved[2], saved[3], saved[4]
end
print("fords in the game: ok")

-- Specks: a landmass under 4 tiles that no city on a big landmass could work is moved whole onto the
-- nearest land; specks near each other gather into one island; one with no land near sinks; no land is made.
local function landCount(t) local n = 0 for i = 1, W * H do if t[i] ~= OCEAN_T then n = n + 1 end end return n end
local function massSizes(t)
  local seen, out = {}, {}
  for i = 1, W * H do
    if t[i] ~= OCEAN_T and not seen[i] then
      local stack, n = { i }, 0
      seen[i] = true
      while #stack > 0 do
        local c = table.remove(stack)
        n = n + 1
        for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
          if t[j] ~= OCEAN_T and not seen[j] then seen[j] = true; stack[#stack + 1] = j end
        end
      end
      out[#out + 1] = n
    end
  end
  table.sort(out)
  return out
end

local t = sea(); fill(t, 10, 25, 20, 35); fill(t, 50, 51, 45, 45)
assert(select(2, PA_Magnetize(t, W, H, true)) == 1 and landCount(t) == 16 * 16, "a lone speck with no land near sinks")

t = sea(); fill(t, 10, 25, 20, 35); fill(t, 28, 28, 27, 27)
local copy = {} for i = 1, W * H do copy[i] = t[i] end
assert(PA_Magnetize(t, W, H, true) == 0, "a speck a city on the big landmass could work stays")
for i = 1, W * H do assert(t[i] == copy[i], "and nothing else moves") end

t = sea(); fill(t, 10, 25, 20, 35); fill(t, 31, 33, 27, 27)
assert(PA_Magnetize(t, W, H, true) == 1 and landCount(t) == 16 * 16 + 3, "a speck out of reach is moved, no land made")
assert(t[idx(32, 27)] == OCEAN_T and reach(t, idx(15, 25), idx(26, 27), false) or reach(t, idx(15, 25), idx(26, 26), false) or reach(t, idx(15, 25), idx(26, 28), false),
  "it now touches the big landmass")
assert(#massSizes(t) == 1, "and is part of it")

t = sea(); fill(t, 44, 44, 40, 40); fill(t, 47, 47, 40, 40); fill(t, 45, 46, 43, 43)
local moved, sunk = PA_Magnetize(t, W, H, true)
assert(sunk == 0 and landCount(t) == 4, "specks near each other: none sunk, no land made")
assert(#massSizes(t) == 1 and massSizes(t)[1] == 4, "they gather into one island of 4, got " .. table.concat(massSizes(t), ","))

-- A speck must not wall water off: placed between two islands two tiles apart it would join them and close the sea between.
t = sea(); fill(t, 30, 34, 30, 34); fill(t, 37, 41, 30, 34); fill(t, 35, 36, 40, 40)
PA_Magnetize(t, W, H, true)
local function seaBodies(tt)
  local seen, n = {}, 0
  for i = 1, W * H do
    if tt[i] == OCEAN_T and not seen[i] then
      local stack, size = { i }, 0
      seen[i] = true
      while #stack > 0 do
        local c = table.remove(stack)
        size = size + 1
        for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
          if tt[j] == OCEAN_T and not seen[j] then seen[j] = true; stack[#stack + 1] = j end
        end
      end
      if size >= PA_MIN_SEA then n = n + 1 end
    end
  end
  return n
end
assert(seaBodies(t) == 1, "the sea stays one body")
local ring = sea(); fill(ring, 20, 40, 20, 40)
for y = 23, 37 do for x = 23, 37 do ring[idx(x, y)] = OCEAN_T end end
ring[idx(30, 20)] = OCEAN_T; ring[idx(30, 21)] = OCEAN_T; ring[idx(30, 22)] = OCEAN_T -- a channel into the inner sea
assert(seaBodies(ring) == 1)
ring[idx(30, 21)] = LAND_T
assert(PA_SplitsSea(ring, { [idx(30, 21)] = true }, W, H, true), "closing the channel walls the inner sea off")
ring[idx(30, 21)] = OCEAN_T
ring[idx(10, 10)] = LAND_T
assert(not PA_SplitsSea(ring, { [idx(10, 10)] = true }, W, H, true), "land in open sea walls nothing off")
for x = 0, W - 1 do for _, y in ipairs({ 0, 1, 2, H - 3, H - 2, H - 1 }) do assert(t[idx(x, y)] == OCEAN_T, "polar rows stay sea") end end
print("specks: ok")

-- PA_Rand spans [0, 1) through the game's Map.Rand: rift angles, places and strengths depend on it.
do
  local lo, hi = 1, 0
  for _ = 1, 2000 do local r = PA_Rand(); lo = math.min(lo, r); hi = math.max(hi, r) end
  assert(lo >= 0 and hi < 1 and lo < 0.05 and hi > 0.95, string.format("PA_Rand spans %.3f to %.3f", lo, hi))
end
print("random: ok")

-- At least two continents: land the rifts cut deepest sinks until a second landmass holds a quarter of the land.
do
  local t = sea()
  fill(t, 10, 25, 15, 35); fill(t, 30, 45, 15, 35); fill(t, 26, 29, 24, 26) -- two halves and a neck
  local depth = {}
  for i = 1, W * H do depth[i] = 0 end
  for y = 24, 26 do for x = 26, 29 do depth[idx(x, y)] = 0.9 end end
  local sunk, ok = PA_TwoContinents(t, depth, W, H, true, 6)
  assert(ok and sunk > 0 and sunk <= 12, "the neck sinks, got " .. tostring(sunk))
  local sizes = massSizes(t)
  assert(#sizes == 2 and sizes[1] >= 0.25 * (sizes[1] + sizes[2]), "two continents of a fair size")
  assert(PA_TwoContinents(t, depth, W, H, true, 6) == 0, "two continents already: nothing more sinks")
  -- Three continents of 54%, 23% and 23% are enough, though only the first holds a quarter; one landmass with
  -- nothing the rifts cut stays one, and says so.
  local three = sea()
  fill(three, 5, 24, 10, 30); fill(three, 30, 44, 10, 21); fill(three, 30, 44, 26, 37) -- 54%, 23%, 23%
  local s3, ok3 = PA_TwoContinents(three, depth, W, H, true, 6)
  assert(s3 == 0 and ok3, "three continents count as at least two, with nothing more sunk")
  local one = sea()
  fill(one, 5, 40, 10, 40); fill(one, 50, 54, 10, 14)
  local zero = {}
  for i = 1, W * H do zero[i] = 0 end
  local s2, ok2 = PA_TwoContinents(one, zero, W, H, true, 6)
  assert(s2 == 0 and not ok2, "one landmass with nothing to cut reports that it stays one")
end
print("two continents: ok")

-- Deep sea: water beside land is shallow; an island within reach of other land keeps a shallow way to it.
do
  local t = sea()
  fill(t, 5, 20, 10, 45); fill(t, 27, 45, 10, 45)          -- two continents, a channel 6 tiles wide
  fill(t, 12, 13, 5, 6)                                      -- an island 3 water tiles off the coast, out of its shallows
  fill(t, 55, 57, 25, 26)                                    -- an island far out in the ocean
  local shallow = PA_DeepWater(t, W, H, true, 6)
  assert(shallow[idx(21, 30)] and shallow[idx(26, 30)], "the coasts are shallow")
  assert(not shallow[idx(23, 30)] and not shallow[idx(24, 30)], "the middle of a wide channel is deep")
  local function reachable(from, to)
    local seen, stack = { [from] = true }, { from }
    while #stack > 0 do
      local c = table.remove(stack)
      if c == to then return true end
      for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
        if not seen[j] and (shallow[j] or t[j] ~= OCEAN_T) then seen[j] = true; stack[#stack + 1] = j end
      end
    end
    return false
  end
  assert(reachable(idx(12, 5), idx(15, 10)), "the island near the coast keeps a shallow way to it")
  assert(not reachable(idx(56, 25), idx(15, 10)), "the island far out needs deep water to reach")
end
print("deep sea: ok")

-- Defaults: canal spots on, continents as the rifts fall, deep sea on; the operator's style of 2026-11-23 (four wide
-- rifts on a bigger, lower-sea map, middle-sea islands of medium size); his pick of 2026-11-27: strength 85, spread 50%,
-- raggedness 6, three shallow fords for each continent, shut-in seas opened to the ocean.
do
  local info = GetMapScriptInfo()
  local want = { ["Connect all continents"] = "On, with a canal spot on each bridge", ["Continents"] = "As the rifts fall", ["Deep sea"] = "On",
    ["Rifts"] = "4", ["Rift Strength"] = "85", ["Rift Strength Spread"] = "50%", ["Rift Width"] = "6.5 tiles", ["Rift Raggedness"] = "6 tiles",
    ["Middle-Sea Island Size"] = "Medium (4-7 tiles)", ["TXT_KEY_MAP_OPTION_SEA_LEVEL"] = "TXT_KEY_MAP_OPTION_LOW",
    ["Land Size X"] = "Default +10 tiles", ["Land Size Y"] = "Default +10 tiles",
    ["Crossings"] = "Shallow fords", ["Crossings per Continent"] = "3", ["Rift Water"] = "Deep in the middle", ["Inland Seas"] = "Opened to the ocean" }
  for _, o in ipairs(info.CustomOptions) do
    if want[o.Name] then assert(o.Values[o.DefaultValue] == want[o.Name], o.Name .. " defaults to " .. tostring(o.Values[o.DefaultValue])); want[o.Name] = nil end
  end
  assert(next(want) == nil, "every new option is offered")
end
print("defaults: ok")

-- With canal spots, every landmass big enough for a civ ends joined to the biggest: a bridge that finds no spot for
-- its canal is built without one rather than left out.
do
  local t = {}
  for i = 1, W * H do t[i] = plotTypes[i] end
  PA_Bridge(t, PA_DEPTH, W, H, true, 6, true)
  local seen, sizes, total = {}, {}, 0
  for i = 1, W * H do
    if (t[i] == LAND_T or t[i] == PlotTypes.PLOT_HILLS) and not seen[i] then
      local stack, n = { i }, 0
      seen[i] = true
      while #stack > 0 do
        local c = table.remove(stack); n = n + 1
        for _, j in ipairs(PA_NeighbourList(c, W, H, true)) do
          if not seen[j] and (t[j] == LAND_T or t[j] == PlotTypes.PLOT_HILLS) then seen[j] = true; stack[#stack + 1] = j end
        end
      end
      sizes[#sizes + 1] = n; total = total + n
    end
  end
  table.sort(sizes, function(a, b) return a > b end)
  local need = math.max(20, math.floor(total / 12))
  assert(not sizes[2] or sizes[2] < need, "a landmass of " .. tostring(sizes[2]) .. " tiles was left unjoined")
end
print("canal bridges join everything: ok")

-- Coastal mountains become hills before the bridges are planned, as Lekmap's feature step later makes them anyway:
-- a mountain beside open sea (10 tiles or more), not one beside a pocket of water.
do
  local t = sea()
  fill(t, 10, 30, 10, 30)
  t[idx(10, 20)] = PlotTypes.PLOT_MOUNTAIN                  -- on the open coast
  t[idx(20, 20)] = PlotTypes.PLOT_MOUNTAIN                  -- inland
  t[idx(21, 21)] = OCEAN_T                                  -- a one-tile pocket of water beside it
  t[idx(22, 22)] = PlotTypes.PLOT_MOUNTAIN
  local n = PA_FlattenCoastalMountains(t, W, H, true)
  assert(n == 1 and t[idx(10, 20)] == PlotTypes.PLOT_HILLS, "the coastal mountain becomes hills")
  assert(t[idx(20, 20)] == PlotTypes.PLOT_MOUNTAIN and t[idx(22, 22)] == PlotTypes.PLOT_MOUNTAIN, "inland mountains stay, a pocket of water is no coast")
end
print("coastal mountains: ok")
