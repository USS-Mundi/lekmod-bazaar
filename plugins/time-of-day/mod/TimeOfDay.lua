-- Time of Day panel: sets the hour the Time of Day plugin draws (Game.CG*). The hour is remembered (in the save):
-- game turns move it on by a day's share each turn, a day being a set number of turns (4 to 120; 24 turns: an hour a
-- turn), so a new day length changes only how far later turns move it; hold keeps it still at a chosen hour; off shows
-- the game as it is, and is how the panel starts until the player picks a mode. Each change snaps; a new turn's hour snaps in as the barbarians take their turn, the last before
-- the player's.

if Game.CGSet == nil then
	Controls.OpenButton:SetHide(true);
	print("TimeOfDay: the Time of Day plugin is not running");
	return;
end

local MODES = { "turns", "hour", "off" };
local MODE_LABELS = { turns = "CYCLE: GAME TURNS", hour = "HOLD: HOUR", off = "OFF" };
local SHORTEST, LONGEST = 4, 120;   -- turns in a day

local g_Settings = Modding.OpenUserData("TimeOfDay", 1);
local function Get(key, default)
	local ok, v = pcall(function() return g_Settings.GetValue(key); end);
	v = ok and tonumber(v) or nil;
	return v ~= nil and v or default;
end
local function Put(key, v) pcall(function() g_Settings.SetValue(key, v); end); end
-- the hour and the turn it belongs to go in the save (the settings file when the game offers no save data)
local g_Save = nil;
pcall(function() g_Save = Modding.OpenSaveData(); end);
local function SaveGet(key)
	if g_Save == nil then return Get(key, nil); end
	local ok, v = pcall(function() return g_Save.GetValue(key); end);
	return ok and tonumber(v) or nil;
end
local function SavePut(key, v)
	if g_Save == nil then Put(key, v); else pcall(function() g_Save.SetValue(key, v); end); end
end

local g = {
	mode = MODES[Get("Mode", 3)] or "off",   -- off until the player picks a mode
	dayTurns = Get("DayTurns", 24),
	hour = SaveGet("Hour") or Get("Hour", 12),   -- the hour now; a new game starts at the last one set
	turn = SaveGet("Turn") or Game.GetGameTurn(),  -- the game turn the hour belongs to
	strength = Get("Strength", 0.85),
	depth = Get("Depth", 0.85),
	warmth = Get("Warmth", 0.8),
	brightness = Get("Brightness", 1),   -- factors, 1 as made
	colour = Get("Colour", 1),
	contrast = Get("Contrast", 1),
};
g.dayTurns = math.max(SHORTEST, math.min(LONGEST, math.floor(g.dayTurns + 0.5)));

-- 12:00 is noon (phase 0 in the plugin), 00:00 midnight (0.5).
local function PhaseOf(hour) return ((hour - 12) / 24) % 1; end
local function Hour() return g.hour; end
local function Keep()
	SavePut("Hour", g.hour); SavePut("Turn", g.turn);
	Put("Hour", g.hour);
end
local function Clock(h)
	local m = math.floor(h * 60 + 0.5) % (24 * 60);
	return string.format("%02d:%02d", math.floor(m / 60), m % 60);
end
local function PerTurn()
	local m = math.floor(24 * 60 / g.dayTurns + 0.5);
	if m % 60 == 0 then return string.format("%d h", m / 60); end
	if m > 60 then return string.format("%d h %d min", math.floor(m / 60), m % 60); end
	return string.format("%d min", m);
end

local function Push()
	local mode = (g.mode == "off") and 0 or 2;
	Game.CGSet(mode, 600, g.strength, g.depth, g.warmth, g.brightness, g.colour, g.contrast);
	if mode == 2 then Game.CGTarget(PhaseOf(Hour()), true); end
end

local function Show()
	Controls.ModeLabel:SetText(MODE_LABELS[g.mode]);
	if g.mode == "turns" then
		Controls.SpeedLabel:SetText(string.format("Day length: %d turns (%s a turn)", g.dayTurns, PerTurn()));
		Controls.SpeedSlider:SetValue((g.dayTurns - SHORTEST) / (LONGEST - SHORTEST));
	else
		Controls.SpeedLabel:SetText(string.format("Hour: %s", Clock(g.hour)));
		Controls.SpeedSlider:SetValue(math.floor(g.hour + 0.5) % 24 / 23);
	end
	Controls.StrengthLabel:SetText(string.format("Strength: %d%%", math.floor(g.strength * 100 + 0.5)));
	Controls.DepthLabel:SetText(string.format("Night depth: %d%%", math.floor(g.depth * 100 + 0.5)));
	Controls.WarmthLabel:SetText(string.format("Dawn and dusk warmth: %d%%", math.floor(g.warmth * 100 + 0.5)));
	Controls.BrightnessLabel:SetText(string.format("Brightness: %d%%", math.floor(g.brightness * 100 + 0.5)));
	Controls.ColourLabel:SetText(string.format("Colour: %d%%", math.floor(g.colour * 100 + 0.5)));
	Controls.ContrastLabel:SetText(string.format("Contrast: %d%%", math.floor(g.contrast * 100 + 0.5)));
end

-- The first slider is the day's length in game turns, and the hour itself when holding.
Controls.SpeedSlider:RegisterSliderCallback(function(v)
	if g.mode == "turns" then
		g.dayTurns = math.floor(SHORTEST + v * (LONGEST - SHORTEST) + 0.5);
		Put("DayTurns", g.dayTurns);
	else
		g.hour = math.floor(v * 23 + 0.5);
		Keep();
	end
	Show();
	Push();
end);

-- low and high: the setting at the slider's two ends
local function Slider(control, key, field, low, high)
	low, high = low or 0, high or 1;
	control:SetValue((g[field] - low) / (high - low));
	control:RegisterSliderCallback(function(v)
		g[field] = low + v * (high - low);
		Put(key, g[field]);
		Show();
		Push();
	end);
end
Slider(Controls.StrengthSlider, "Strength", "strength");
Slider(Controls.DepthSlider, "Depth", "depth");
Slider(Controls.WarmthSlider, "Warmth", "warmth");
Slider(Controls.BrightnessSlider, "Brightness", "brightness", 0.5, 1.5);
Slider(Controls.ColourSlider, "Colour", "colour", 0, 2);
Slider(Controls.ContrastSlider, "Contrast", "contrast", 0.5, 1.5);

Controls.ModeButton:RegisterCallback(Mouse.eLClick, function()
	local i = 1;
	for k, m in ipairs(MODES) do if m == g.mode then i = k; end end
	i = i % #MODES + 1;
	g.mode = MODES[i];
	Put("Mode", i);
	Show();
	Push();
end);

Controls.OpenButton:RegisterCallback(Mouse.eLClick, function()
	Controls.Panel:SetHide(not Controls.Panel:IsHidden());
	Show();
end);
Controls.CloseButton:RegisterCallback(Mouse.eLClick, function() Controls.Panel:SetHide(true); end);

-- A new turn's hour: game turns move the hour on by a day's share for each turn since the one it belongs to. It snaps
-- in as the barbarians take their turn (the last before the player's); the player's turn catches up any turn that
-- was not marked so (no barbarians, a missed event). Hold and off keep the hour, following the turn.
local function MoveTo(turn)
	if turn == g.turn then return; end
	if g.mode == "turns" and turn > g.turn then
		g.hour = (g.hour + (turn - g.turn) * 24 / g.dayTurns) % 24;
		g.turn = turn; Keep();
		Game.CGTarget(PhaseOf(g.hour), true);
	else
		g.turn = turn; Keep();
	end
end
if Events.AIProcessingStartedForPlayer ~= nil then
	Events.AIProcessingStartedForPlayer.Add(function(i)
		local p = Players[i];
		if p ~= nil and p:IsBarbarian() then MoveTo(Game.GetGameTurn() + 1); end
	end);
end
Events.ActivePlayerTurnStart.Add(function()
	local t = Game.GetGameTurn();
	if t < g.turn and g.turn ~= t + 1 then g.turn = t; Keep(); end   -- a game from before the hour's turn: keep the hour
	MoveTo(math.max(t, g.turn));
end);

local g_Since = 0;
ContextPtr:SetUpdate(function(dt)
	g_Since = g_Since + dt;
	if g_Since < 0.25 then return; end
	g_Since = 0;
	local phase, part, path = Game.CGState();
	local night = phase > 0.30 and phase < 0.70;
	Controls.OpenSun:SetHide(night);
	Controls.OpenMoon:SetHide(not night);
	if not Controls.Panel:IsHidden() then
		Controls.Marker:SetOffsetX(math.floor(phase * 255));
		local now = g.mode == "off" and "Off" or string.format("%s, %s", Clock(Hour()), part);
		Controls.NowLabel:SetText("[COLOR:226:201:138:255]Now:[ENDCOLOR] " .. now);
		Controls.PathLabel:SetText("[COLOR:169:179:172:255]" .. path .. "[ENDCOLOR]");
	end
end);

Show();
Push();
