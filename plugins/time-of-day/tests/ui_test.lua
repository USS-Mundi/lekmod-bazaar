-- Drives mod/TimeOfDay.lua against stand-ins for the game and the plugin. Run: luajit tests/ui_test.lua mod/TimeOfDay.lua
local failed, run = 0, 0
local function check(c, what) run = run + 1; if not c then failed = failed + 1; print("FAIL " .. what) end end
local function NewControl(name)
	local c = { _name = name, _hidden = false, _text = "", _cb = {}, _ox = 0 }
	local m = {}
	function m.SetHide(s, h) s._hidden = h end
	function m.IsHidden(s) return s._hidden end
	function m.SetText(s, t) s._text = t end
	function m.SetOffsetX(s, x) s._ox = x end
	function m.SetValue(s, v) s._value = v end
	function m.RegisterCallback(s, k, f) s._cb[k] = f end
	function m.RegisterSliderCallback(s, f) s._slider = f end
	m.__index = function(s, k) return m[k] or function() end end
	return setmetatable(c, m)
end
Controls = setmetatable({}, { __index = function(t, k) local c = NewControl(k); rawset(t, k, c); return c end })
Controls.Panel:SetHide(true)
local update
ContextPtr = { SetUpdate = function(self, f) update = f end }
local store, saved = {}, {}
local function Db(t) return { GetValue = function(k) return t[k] end, SetValue = function(k, v) t[k] = v end } end
Modding = { OpenUserData = function() return Db(store) end, OpenSaveData = function() return Db(saved) end }
Mouse = { eLClick = "click" }
local onTurn, onEnd, onAI = {}, {}, {}
Events = {
	ActivePlayerTurnStart = { Add = function(f) table.insert(onTurn, f) end },
	ActivePlayerTurnEnd = { Add = function(f) table.insert(onEnd, f) end },
	AIProcessingStartedForPlayer = { Add = function(f) table.insert(onAI, f) end },
}
GameDefines = { MAX_MAJOR_CIVS = 22, MAX_CIV_PLAYERS = 63 }
Players = setmetatable({}, { __index = function(t, i)
	return { IsAlive = function() return true end, IsMinorCiv = function() return i >= 22 and i < 63 end, IsBarbarian = function() return i == 63 end }
end })
local set, target, phase, landed = nil, nil, 0.1, nil
local turn = 5
Game = {
	GetGameTurn = function() return turn end,
	CGSet = function(...) set = { ... } end,
	CGTarget = function(p, jump, seconds) target = { p, jump, seconds } end,
	CGLand = function(s) landed = s end,
	CGState = function() return phase, "Night", "shading the map each frame" end,
	CGSetPhase = function(p) phase = p end,
}
local ok, err = pcall(dofile, arg[1])
check(ok, "loads: " .. tostring(err))
-- the clock: 12:00 is noon (phase 0), 00:00 midnight (0.5); the hour is remembered and every change snaps
local function phaseOf(h) return ((h - 12) / 24) % 1 end
local function near(a, b) return math.abs(a - b) < 1e-6 end
local function at(h) return target and near(target[1], phaseOf(h)) and target[2] == true end
check(set and set[1] == 0 and #set == 8 and set[6] == 1 and set[7] == 1 and set[8] == 1, "starts off until the player picks a mode, eight settings to the plugin, brightness/colour/contrast as made")
check(target == nil, "off: no hour is sent to the plugin")
local click = function(c) c._cb.click() end
click(Controls.OpenButton)
check(not Controls.Panel:IsHidden(), "panel opens")
check(Controls.ModeLabel._text == "OFF", "the panel shows off: " .. tostring(Controls.ModeLabel._text))
click(Controls.ModeButton)
check(Controls.ModeLabel._text == "CYCLE: GAME TURNS" and set[1] == 2, "one click starts the day cycling with the game's turns")
check(at(12), "a game with no hour yet starts at noon, snapped to")
check(Controls.SpeedLabel._text == "Day length: 24 turns (1 h a turn)", "day length label: " .. Controls.SpeedLabel._text)
local function fire(list, ...) for _, f in ipairs(list) do f(...) end end
Controls.SpeedSlider._slider(1)   -- the longest day
check(Controls.SpeedLabel._text == "Day length: 120 turns (12 min a turn)", "120 turns at the top: " .. Controls.SpeedLabel._text)
check(at(12), "a new day length leaves the hour as it is")
Controls.SpeedSlider._slider(0)
check(Controls.SpeedLabel._text == "Day length: 4 turns (6 h a turn)", "4 turns at the bottom: " .. Controls.SpeedLabel._text)
Controls.SpeedSlider._slider((30 - 4) / 116)
check(Controls.SpeedLabel._text == "Day length: 30 turns (48 min a turn)", "30 turns: " .. Controls.SpeedLabel._text)
check(at(12), "still noon")
-- a turn: the next hour snaps in as the barbarians take their turn, the last before the player's
update(0.1)
fire(onEnd); fire(onAI, 3); fire(onAI, 22)
check(at(12), "nothing moves while the civs and city states play")
fire(onAI, 63)
check(at(12 + 24 / 30), "the barbarians' turn snaps to the next turn's hour (12:48)")
fire(onAI, 63)
check(at(12 + 24 / 30), "one step a turn")
turn = 6; fire(onTurn)
check(at(12 + 24 / 30), "the player's turn leaves it there")
check(near(saved.Hour, 12 + 24 / 30) and saved.Turn == 6, "the hour and its turn are kept in the save")
Controls.SpeedSlider._slider(0)
check(at(12 + 24 / 30), "a new day length mid-game keeps the shade")
-- a turn the barbarians did not mark (none left, a missed event): the player's turn catches up
turn = 7; fire(onTurn)
check(at(18 + 24 / 30), "caught up by one 4-turn-day step (6 h) at the player's turn")
turn = 7; fire(onTurn)
check(at(18 + 24 / 30), "the same turn again moves nothing")
click(Controls.ModeButton)
check(Controls.ModeLabel._text == "HOLD: HOUR" and set[1] == 2, "hold an hour")
Controls.SpeedSlider._slider(21 / 23)
check(Controls.SpeedLabel._text == "Hour: 21:00" and math.abs(target[1] - phaseOf(21)) < 1e-6 and target[2] == true, "hold 21:00: " .. Controls.SpeedLabel._text)
Controls.StrengthSlider._slider(0.4)
check(math.abs(set[3] - 0.4) < 1e-6 and Controls.StrengthLabel._text == "Strength: 40%", "strength slider")
Controls.DepthSlider._slider(1)
check(set[4] == 1, "night depth slider")
Controls.WarmthSlider._slider(0)
check(set[5] == 0, "warmth slider")
Controls.BrightnessSlider._slider(1)
check(math.abs(set[6] - 1.5) < 1e-6 and Controls.BrightnessLabel._text == "Brightness: 150%", "brightness slider: " .. Controls.BrightnessLabel._text)
Controls.ColourSlider._slider(0.25)
check(math.abs(set[7] - 0.5) < 1e-6 and Controls.ColourLabel._text == "Colour: 50%", "colour slider: " .. Controls.ColourLabel._text)
Controls.ContrastSlider._slider(0.7)
check(math.abs(set[8] - 1.2) < 1e-6 and Controls.ContrastLabel._text == "Contrast: 120%", "contrast slider: " .. Controls.ContrastLabel._text)
click(Controls.ModeButton)
check(Controls.ModeLabel._text == "OFF" and set[1] == 0, "off")
-- holding: turns pass without moving the hour, and cycling carries on from the held hour
fire(onAI, 63); turn = 8; fire(onTurn)
check(near(saved.Hour, 21) and saved.Turn == 8, "held through a turn")
click(Controls.ModeButton)
check(Controls.ModeLabel._text == "CYCLE: GAME TURNS", "back to turns: no real-time mode in the cycle")
check(at(21), "cycling resumes at the held hour")
fire(onAI, 63)
check(at(21 + 6), "and steps from there")
phase = 0.5
update(0.3)
check(Controls.OpenSun._hidden and not Controls.OpenMoon._hidden, "the moon shows at night")
check(Controls.PathLabel._text:find("shading the map each frame", 1, true) ~= nil, "what the plugin is doing shows")
check(store.Mode == 1 and store.Strength == 0.4 and store.DayTurns == 4 and math.abs(store.Contrast - 1.2) < 1e-6, "settings kept")
print(string.format("%d/%d passed", run - failed, run))
os.exit(failed == 0 and 0 or 1)
