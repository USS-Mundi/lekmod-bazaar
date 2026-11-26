-- Runs mod/MessageCenter.lua against stand-ins for the game's UI and the plugin, and drives it like a player.
-- Run: tests/run_message_center_ui_test.sh (LuaJIT, Lua 5.1 syntax).
local failed, run = 0, 0
local function check(cond, what)
	run = run + 1
	if not cond then failed = failed + 1; print("FAIL " .. what) end
end

-- ===== stand-in controls =====
local Built = {}
local function NewControl(name)
	local c = { _name = name, _hidden = false, _text = "", _cb = {}, _children = {}, _sx = 100, _sy = 20, _ox = 0, _oy = 0 }
	local m = {}
	function m.SetHide(self, h) self._hidden = h end
	function m.IsHidden(self) return self._hidden end
	function m.SetText(self, t) self._text = t end
	function m.GetText(self) return self._text end
	function m.ClearString(self) self._text = "" end
	function m.RegisterCallback(self, a, b)
		if b then self._cb[a] = b else self._cb.default = a end
	end
	function m.GetSizeX(self) return self._sx end
	function m.GetSizeY(self) return self._sy end
	function m.SetSizeX(self, v) self._sx = v end
	function m.SetSizeY(self, v) self._sy = v end
	function m.SetSizeVal(self, x, y) self._sx, self._sy = x, y end
	function m.GetOffsetX(self) return self._ox end
	function m.GetOffsetY(self) return self._oy end
	function m.SetOffsetX(self, v) self._ox = v end
	function m.SetOffsetVal(self, x, y) self._ox, self._oy = x, y end
	function m.DestroyAllChildren(self) self._children = {} end
	function m.SetToolTipString(self, t) self._tip = t end
	function m.SetToolTipType(self, t) self._tipType = t end
	function m.SetToolTipCallback(self, f) self._tipCb = f end
	function m.SetAlpha(self, a) self._alpha = a end
	function m.SetDisabled(self, d) self._disabled = d end
	function m.GetScrollValue() return 1 end
	function m.GetRatio() return 1 end
	m.__index = function(self, k)
		return m[k] or function() end
	end
	return setmetatable(c, m)
end
local function Registry()
	return setmetatable({}, { __index = function(t, k) local c = NewControl(k); rawset(t, k, c); return c end })
end

Controls = Registry()
-- the game's tooltip types: each type's controls, filled by a control's callback when it is hovered
local TipTypes = {}
TTManager = { GetTypeControlTable = function(self, name, tbl)
	setmetatable(tbl, { __index = function(t, k) local c = NewControl(name .. "." .. k); rawset(t, k, c); return c end })
	TipTypes[name] = tbl
end }
-- hovering a control: what its leader tooltip shows, or nil when it has none
local function hover(c)
	if c._tipType ~= "MCLeaderTip" or c._tipCb == nil then return nil end
	c._tipCb(c)
	local t, out = TipTypes.MCLeaderTip, { rows = 0 }
	for i = 1, 2 do
		if not t["TipRow" .. i]:IsHidden() then
			out.rows = i
			out["leader" .. i] = t["TipLeader" .. i]:GetText()
			out["civ" .. i] = t["TipCiv" .. i]:GetText()
			out["pictures" .. i] = not t["TipPortrait" .. i]:IsHidden() and not t["TipEmblem" .. i]:IsHidden()
		end
	end
	out.click = not t.TipClick:IsHidden()
	return out
end
local handlers = {}
ContextPtr = {
	BuildInstanceForControl = function(self, name, tbl, parent)
		setmetatable(tbl, { __index = function(t, k) local c = NewControl(name .. "." .. k); rawset(t, k, c); return c end })
		table.insert(parent._children, { name = name, inst = tbl })
		table.insert(Built, name)
	end,
	SetInputHandler = function(self, f) handlers.input = f end,
	SetUpdate = function(self, f) handlers.update = f end,
}

-- ===== stand-in game =====
local CHATS = {}
local CITYSTATES = { { Player = 22, Name = "Valletta", Speakable = true } }
local TALK = {}
local CS_SENT = {}
local LINES = {}
local SENT = {}
local NAMED = {}
local function chatId(members)
	local m, seen = { "human" }, {}
	for _, x in ipairs(members) do if not seen[x] then seen[x] = true; table.insert(m, x) end end
	table.sort(m)
	return table.concat(m, "+")
end
Game = {
	GetActivePlayer = function() return 0 end,
	GetActiveTeam = function() return 0 end,
	MCSeats = function() return { { Token = "p1", Civ = "Maurya" }, { Token = "p2", Civ = "Turkey" }, { Token = "p3", Civ = "Scotland" }, { Token = "p23", Civ = "Valletta" } } end,
	MCChats = function() return CHATS end,
	MCLines = function(id) return LINES[id] or {} end,
	MCChatId = chatId,
	MCSeen = function() end,
	MCName = function(id, name) NAMED[id] = name; return true end,
	MCSend = function(members, text)
		table.insert(SENT, { members = members, text = text })
		return chatId(members)
	end,
	-- the MCP plugin's city-states: the ones the human has met, and whether the human may speak to each (its ally)
	GetLLMCityStates = function() return CITYSTATES end,
	LLMCityStateTalk = function(p) return TALK[p] or {} end,
	LLMChat = function(p, text) table.insert(CS_SENT, { player = p, text = text }) end,
}
local function color(r, g, b) return { x = r, y = g, z = b, w = 1 } end
local CIVTYPE = { Maurya = 1, Turkey = 2, Scotland = 3, Persia = 5, Rome = 0, Valletta = 22 }
local function player(civ, leader, team, minor, ally)
	return {
		GetCivilizationType = function() return CIVTYPE[civ] end,
		IsAlive = function() return true end, IsBarbarian = function() return false end,
		GetCivilizationShortDescription = function() return civ end, GetName = function() return leader end,
		IsMinorCiv = function() return minor end, GetAlly = function() return ally end, GetTeam = function() return team end,
		GetPlayerColors = function() return color(0.2, 0.2, 0.2), color(0.9, 0.6, 0.3) end,
		GetLeaderType = function() return CIVTYPE[civ] end,
	}
end
Players = { [0] = player("Rome", "Augustus", 0, false, -1), [1] = player("Maurya", "Ashoka", 1, false, -1),
	[2] = player("Turkey", "Ataturk (Claude Opus p2)", 2, false, -1), [3] = player("Scotland", "Robert", 0, false, -1),
	[22] = player("Valletta", "Valletta", 22, true, 0), [5] = player("Persia", "Darius", 5, false, -1) }
Teams = { [0] = { IsHasMet = function() return true end } }
GameDefines = { MAX_CIV_PLAYERS = 64, MAX_MAJOR_CIVS = 22 }
Locale = { ConvertTextKey = function(k) return k end }
-- GameInfo tables: rows by ID and by Type, and callable with a filter to iterate the matching rows
local function Table(rows)
	local t = {}
	for i, r in ipairs(rows) do t[r.ID or i] = r; if r.Type then t[r.Type] = r end end
	return setmetatable(t, { __call = function(_, f)
		local out = {}
		for _, r in ipairs(rows) do
			local ok = true
			for k, v in pairs(f or {}) do if r[k] ~= v then ok = false end end
			if ok then table.insert(out, r) end
		end
		local i = 0
		return function() i = i + 1; return out[i] end
	end })
end
local function civ(id, name) return { ID = id, Type = "CIV_" .. name, ShortDescription = name, PortraitIndex = id, AlphaIconAtlas = "CIV_ALPHA_ATLAS" } end
local function leader(id, name) return { ID = id, Type = "LEADER_" .. name, Description = name, PortraitIndex = 3, IconAtlas = "LEADER_ATLAS" } end
GameInfo = {
	Civilizations = Table({ civ(0, "ROME"), civ(1, "INDIA"), civ(2, "TURKEY"), civ(3, "SCOTLAND"), civ(5, "PERSIA"), civ(9, "ENGLAND") }),
	Leaders = Table({ leader(0, "AUGUSTUS"), leader(1, "ASHOKA"), leader(2, "SULEIMAN"), leader(3, "ROBERT"), leader(5, "DARIUS"), leader(9, "ELIZABETH") }),
	Leader_Traits = Table({ { LeaderType = "LEADER_ASHOKA", TraitType = "TRAIT_POPULATION" }, { LeaderType = "LEADER_ELIZABETH", TraitType = "TRAIT_OCEAN" } }),
	Traits = Table({ { ID = 1, Type = "TRAIT_POPULATION", ShortDescription = "Population Growth", Description = "Unhappiness halved" },
		{ ID = 2, Type = "TRAIT_OCEAN", ShortDescription = "Sun Never Sets", Description = "+2 naval moves" } }),
	Civilization_UnitClassOverrides = Table({ { CivilizationType = "CIV_INDIA", UnitClassType = "UNITCLASS_HORSEMAN", UnitType = "UNIT_ELEPHANT" },
		{ CivilizationType = "CIV_INDIA", UnitClassType = "UNITCLASS_SETTLER" } }),
	UnitClasses = Table({ { Type = "UNITCLASS_HORSEMAN", DefaultUnit = "UNIT_HORSEMAN" }, { Type = "UNITCLASS_CROSSBOWMAN", DefaultUnit = "UNIT_CROSSBOWMAN" } }),
	Units = Table({ { Type = "UNIT_ELEPHANT", Description = "Naga-Malla", PortraitIndex = 1, IconAtlas = "U" }, { Type = "UNIT_HORSEMAN", Description = "Horseman" },
		{ Type = "UNIT_LONGBOW", Description = "Longbowman", PortraitIndex = 2, IconAtlas = "U" }, { Type = "UNIT_CROSSBOWMAN", Description = "Crossbowman" } }),
	Civilization_BuildingClassOverrides = Table({}),
	BuildingClasses = Table({}),
	Buildings = Table({}),
	Improvements = Table({}),
}
Modding = { OpenUserData = function() local d = {}; return { GetValue = function(k) return d[k] end, SetValue = function(k, v) d[k] = v end } end }
Mouse = { eLClick = "click", eLDown = "down", eLUp = "up" }
MouseEvents = { MouseMove = 1, LButtonUp = 2, LButtonDown = 3 }
KeyEvents = { KeyDown = 3 }
Keys = { VK_ESCAPE = 27 }
UIManager = { _x = 0, _y = 0, GetMousePos = function(self) return self._x, self._y end }
local sounds = 0
Events = { AudioPlay2DSound = function() sounds = sounds + 1 end }
function include() end
-- LuaEvents: LuaEvents.X(...) calls everything added with LuaEvents.X.Add(f); each call is recorded
local LE_SENT = {}
LuaEvents = setmetatable({}, { __index = function(t, k)
	local fns = {}
	local ev = setmetatable({ Add = function(f) table.insert(fns, f) end }, { __call = function(_, ...)
		table.insert(LE_SENT, { name = k, args = { ... } })
		for _, f in ipairs(fns) do f(...) end
	end })
	rawset(t, k, ev)
	return ev
end })
local function sent(name) local n = 0; for _, e in ipairs(LE_SENT) do if e.name == name then n = n + 1 end end; return n end
local hooked = {}
-- leader portraits come in 64, 128, 256; a civilization's emblem (its alpha atlas) in 16 to 128
function IconHookup(index, size, atlas)
	if atlas == "CIV_ALPHA_ATLAS" then return size <= 128 end
	table.insert(hooked, size)
	return size >= 64
end
function CivIconHookup() end
local emblems = {}
function SimpleCivIconHookup(player, size, control) table.insert(emblems, { player = player, size = size, control = control }); return true end

local function click(c, kind) local f = c._cb[kind or "click"]; assert(f, "no callback on " .. c._name); f() end
local function children(parent, name)
	local n = 0
	for _, ch in ipairs(parent._children) do if name == nil or ch.name == name then n = n + 1 end end
	return n
end
local function find(parent, name, pred)
	for _, ch in ipairs(parent._children) do if ch.name == name and pred(ch.inst) then return ch.inst end end
end

-- ===== drive it =====
for _, hidden in ipairs({ "Window", "NewGroup", "MembersPop", "LeaderCard", "SettingsPop", "Toast", "OpenPill", "OpenGlow", "ExistsNote", "Empty" }) do
	Controls[hidden]:SetHide(true)
end
local ok, err = pcall(dofile, (arg and arg[1]) or "mod/MessageCenter.lua")
check(ok, "the script loads: " .. tostring(err))

CHATS = {
	{ Id = "human+p1+p2", Members = { "human", "p1", "p2" }, Name = "Eastern Front", StartedBy = "p1", StartedTurn = 70,
	  Lines = 2, Unread = 1, LastTurn = 74, Stale = false, Last = { Time = "1", Turn = 74, From = "p2", Text = "Their pikes are thin." } },
	{ Id = "human+p1", Members = { "human", "p1" }, Name = "", StartedBy = "p1", StartedTurn = 60,
	  Lines = 1, Unread = 0, LastTurn = 60, Stale = true, Last = { Time = "1", Turn = 60, From = "p1", Text = "old" } },
}
LINES["human+p1+p2"] = { { Time = "1", Turn = 74, From = "p1", Text = "Scotland has one army." },
	{ Time = "2", Turn = 74, From = "p2", Text = "Their pikes are thin." } }

handlers.update(3)
check(not Controls.OpenPill:IsHidden() and Controls.OpenCount:GetText() == "1", "the opener shows one unread")

click(Controls.OpenButton)
check(not Controls.Window:IsHidden(), "the window opens")
check(children(Controls.ListStack, "SectionInstance") == 6, "sections: you, allies, civilizations, groups, allied city-states, bots")
check(children(Controls.ListStack, "RowInstance") == 7, "rows: you, Scotland, Valletta, Maurya, Turkey, Eastern Front, and Persia the bot")

-- you head the list from the first turn, met or not: your leader, your civilization, your card on a click
local first, second = Controls.ListStack._children[1], Controls.ListStack._children[2]
check(first and first.name == "SectionInstance" and first.inst.Text:GetText():find("YOU"), "the list opens with you")
local you = second and second.name == "RowInstance" and second.inst
check(you and you.Name:GetText():find("You") and you.Sub:GetText():find("ROME"), "your row: you and your civilization")
local h = you and hover(you.Root)
check(h and h.rows == 1 and h.leader1:find("AUGUSTUS") and h.civ1:find("ROME") and h.pictures1 and h.click,
	"your row's hover: your portrait, leader, emblem and civilization, and click to show more")
if you then click(you.Root) end
check(not Controls.LeaderCard:IsHidden() and Controls.CardTitle:GetText():find("AUGUSTUS"), "a click on your row opens your card")
check(find(Controls.ListStack, "RowInstance", function(r) return not r.Selected:IsHidden() end) == nil, "your row opens no chat")
Controls.LeaderCard:SetHide(true)

local ef = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Eastern Front") end)
check(ef ~= nil, "the group has a row")
click(ef.Root)
handlers.update(1)
local heads = {}
for _, ch in ipairs(Controls.HeadPortraits._children) do if ch.name == "HeadInstance" then table.insert(heads, ch.inst) end end
check(#heads == 3, "the header shows you and both members")
table.insert(LINES["human+p1+p2"], { Time = "3", Turn = 74, From = "human-ai", Text = "Rome holds the river." })
click(ef.Root)
handlers.update(1)
local aiLine = find(Controls.ChatStack, "TheirsInstance", function(m) return m.Who:GetText():find("Your AI") end)
check(aiLine ~= nil, "a line from the model on your own civilization shows as Your AI, among the others' lines")
table.remove(LINES["human+p1+p2"])
click(ef.Root)
handlers.update(1)
local function headText(i) return heads[i] and heads[i].Name:GetText() or "" end
check(headText(1):find("You") and headText(2):find("Ashoka") and headText(3):find("Ataturk"), "each with its name, you first")
check(heads[3] and not headText(3):find("Claude"), "a leader goes by the leader's name, not the seat")
check(heads[2] and not heads[2].Name:IsHidden(), "the names show when they fit")
local ataturk = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Ataturk") end)
check(ataturk and not ataturk.Name:GetText():find("Claude"), "the list shows the leader's name only")
check(children(Controls.ChatStack, "TheirsInstance") == 2 and children(Controls.ChatStack, "TurnInstance") == 1, "two lines under one turn")

Controls.ChatEntry:SetText("  Hold the river.  ")
Controls.ChatEntry._cb.default()
check(#SENT == 1 and SENT[1].text == "Hold the river." and #SENT[1].members == 2, "Enter sends to both members")

-- a stale chat sorts below and dims
local maurya = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Ashoka") end)
check(maurya and maurya.Sub:GetText():find("Quiet since turn 60"), "a stale chat says when it went quiet")

-- New group with the same members opens the existing chat
local plus = find(Controls.ListStack, "SectionInstance", function(s) return s.Text:GetText():find("GROUPS") end)
click(plus.NewButton)
check(not Controls.NewGroup:IsHidden(), "New group opens")
local function member(leader) return find(Controls.MemberStack, "MemberInstance", function(r) return r.Name:GetText():find(leader) end) end
click(member("Ashoka").Root)
click(member("Ataturk").Root)
check(not Controls.ExistsNote:IsHidden(), "the same members say the chat exists")
check(Controls.StartLabel:GetText() == "OPEN EASTERN FRONT", "the button opens it")
click(member("Robert").Root)
check(Controls.ExistsNote:IsHidden() and Controls.StartLabel:GetText() == "START GROUP", "a new set starts a group")
Controls.GroupName:SetText("North")
click(Controls.StartGroup)
check(NAMED["human+p1+p2+p3"] == "North", "the new group is named")
check(Controls.NewGroup:IsHidden(), "New group closes")

-- Members pop-out
click(Controls.MembersButton)
check(not Controls.MembersPop:IsHidden() and children(Controls.PopStack, "PopMemberInstance") == 4, "members: you and three")
local mine = false
for _, e in ipairs(emblems) do if e.player == 0 and e.size == 45 then mine = true end end
check(mine, "you show as your civilization's emblem")
for _, size in ipairs(hooked) do check(size >= 64, "no leader portrait is asked for at a size the game lacks (" .. size .. ")") end

-- resizing from the grip: the window grows by the mouse's move, its top-left corner stays put
check(Controls.Window._sy == 900, "the window opens a quarter taller than before (900, was 720)")
local w0, h0 = Controls.Window._sx, Controls.Window._sy
local ox0, oy0 = Controls.Window:GetOffsetX(), Controls.Window:GetOffsetY()
UIManager._x, UIManager._y = 0, 0
click(Controls.Grip, "down")
UIManager._x, UIManager._y = 100, 60
handlers.input(MouseEvents.MouseMove, 0, 0)
check(Controls.Window._sx == w0 + 100 and Controls.Window._sy == h0 + 60, "the window grows by as much as the mouse moves")
check(Controls.Window:GetOffsetX() == ox0 + 50 and Controls.Window:GetOffsetY() == oy0 + 30, "its top-left corner stays where it was")
handlers.input(MouseEvents.LButtonUp, 0, 0)
-- resize within limits
UIManager._x, UIManager._y = 0, 0
click(Controls.Grip, "down")
UIManager._x, UIManager._y = 5000, 5000
handlers.input(MouseEvents.MouseMove, 0, 0)
handlers.input(MouseEvents.LButtonUp, 0, 0)
check(Controls.Window._sx == 1400 and Controls.Window._sy == 1125, "resizing stops at 1400x1125")
local function headNames()
	local shown, all = 0, 0
	for _, ch in ipairs(Controls.HeadPortraits._children) do
		if ch.name == "HeadInstance" then all = all + 1; if not ch.inst.Name:IsHidden() then shown = shown + 1 end end
	end
	return shown, all
end
local shownWide, allWide = headNames()
check(allWide == 4 and shownWide == 4, "wide: the new group's four, each named")
click(Controls.Grip, "down")
UIManager._x, UIManager._y = -5000, -5000
handlers.input(MouseEvents.MouseMove, 0, 0)
handlers.input(MouseEvents.LButtonUp, 0, 0)
local shownNarrow, allNarrow = headNames()
check(allNarrow == 4 and shownNarrow == 0, "narrow: names that would not fit on one row go, icons stay")
UIManager._x, UIManager._y = 0, 0
UIManager.GetScreenSizeVal = function() return 1280, 800 end
click(Controls.Grip, "down")
UIManager._x, UIManager._y = 5000, 5000
handlers.input(MouseEvents.MouseMove, 0, 0)
handlers.input(MouseEvents.LButtonUp, 0, 0)
check(Controls.Window._sx == 1240 and Controls.Window._sy == 760, "and never past the screen")
UIManager.GetScreenSizeVal = nil

-- moving: a click drops the window where it is, even when the button-up never arrives
UIManager._x, UIManager._y = 0, 0
click(Controls.DragBar, "down")
UIManager._x, UIManager._y = 100, 50
handlers.input(MouseEvents.MouseMove, 0, 0)
local ox, oy = Controls.Window:GetOffsetX(), Controls.Window:GetOffsetY()
check(handlers.input(MouseEvents.LButtonDown, 0, 0) == true, "the dropping click is used up, not passed to the map")
UIManager._x, UIManager._y = 400, 300
handlers.input(MouseEvents.MouseMove, 0, 0)
check(Controls.Window:GetOffsetX() == ox and Controls.Window:GetOffsetY() == oy, "a click drops the window")
check(handlers.input(MouseEvents.LButtonDown, 0, 0) == false, "after the drop, clicks pass through again")
click(Controls.DragBar, "down")
click(Controls.DragBar, "up")
UIManager._x, UIManager._y = 0, 0
handlers.input(MouseEvents.MouseMove, 0, 0)
check(Controls.Window:GetOffsetX() == ox, "letting go on the bar drops it too")

-- a new message while closed: a ding and a lit opener, no pop-up
handlers.input(KeyEvents.KeyDown, Keys.VK_ESCAPE, 0)
handlers.input(KeyEvents.KeyDown, Keys.VK_ESCAPE, 0)
check(Controls.Window:IsHidden(), "Escape closes the window")
CHATS[1].Unread = 2
CHATS[1].Last = { Time = "3", Turn = 75, From = "p1", Text = "We march." }
local before = sounds
handlers.update(3)
check(sounds == before + 1, "a new message dings")
check(not Controls.OpenGlow:IsHidden(), "the Messages button lights up")
check(Controls.Toast:IsHidden(), "no pop-up")
click(Controls.OpenButton)
check(not Controls.Window:IsHidden(), "the button opens the window")
local fresh = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Eastern Front") end)
check(fresh and not fresh.Fresh:IsHidden(), "the chat with the new message is lit")
local quiet = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Ashoka") end)
check(quiet and quiet.Fresh:IsHidden(), "a chat with nothing new is not")
CHATS[1].Unread = 0
handlers.update(1)
check(Controls.OpenGlow:IsHidden(), "the button goes back once everything is read")

-- a live chat beside civs you have not written to yet: the whole list still builds, live first, then by name, the
-- stale one last (a row with no chat and a row with a live chat once each sorted ahead of the other, and the list
-- stopped after Allies)
Players[4] = player("Mali", "Mansa Musa", 4, false, -1)
Players[5] = player("Ethiopia", "Haile Selassie", 5, false, -1)
Players[6] = player("Macedonia", "Alexander", 6, false, -1)
Players[7] = player("Persia", "Darius", 7, false, -1)
local seats = Game.MCSeats()
for i, civ in ipairs({ "Mali", "Ethiopia", "Macedonia", "Persia" }) do table.insert(seats, { Token = "p" .. (i + 3), Civ = civ }) end
Game.MCSeats = function() return seats end
local function civRows()
	local out, inCivs = {}, false
	for _, ch in ipairs(Controls.ListStack._children) do
		if ch.name == "SectionInstance" then inCivs = ch.inst.Text:GetText():find("CIVILIZATIONS") ~= nil
		elseif inCivs then table.insert(out, (ch.inst.Name:GetText():gsub("%b[]", ""):gsub("%s%s.*$", ""))) end
	end
	return table.concat(out, ", ")
end
local base = #CHATS
for _, live in ipairs({ { "p4", "Mansa Musa" }, { "p5", "Haile Selassie" }, { "p6", "Alexander" }, { "p7", "Darius" }, { "p2", "Ataturk" } }) do
	CHATS[base + 1] = { Id = "human+" .. live[1], Members = { "human", live[1] }, Name = "", StartedBy = "human", StartedTurn = 80,
		Lines = 1, Unread = 0, LastTurn = 80, Stale = false, Last = { Time = "1", Turn = 80, From = "human", Text = "Peace?" } }
	local okList, errList = pcall(handlers.update, 1)
	local names = {}
	for _, n in ipairs({ "Alexander", "Ataturk", "Darius", "Haile Selassie", "Mansa Musa" }) do if n ~= live[2] then table.insert(names, n) end end
	local want = live[2] .. ", " .. table.concat(names, ", ") .. ", Ashoka"
	check(okList and civRows() == want, "with a live chat to " .. live[2] .. ": " .. want .. " (got " .. civRows() .. (okList and "" or ("; " .. tostring(errList))) .. ")")
	check(find(Controls.ListStack, "SectionInstance", function(s) return s.Text:GetText():find("GROUPS") end) ~= nil, "the sections after it still show")
end

-- leaders: a civilization the game's AI plays is in the whole table's strip, never a chat row
Players[4], Players[6], Players[7] = nil, nil, nil
Players[5] = player("Persia", "Darius", 5, false, -1)
Game.MCSeats = function() return { { Token = "p1", Civ = "Maurya" }, { Token = "p2", Civ = "Turkey" }, { Token = "p3", Civ = "Scotland" }, { Token = "p23", Civ = "Valletta" } } end
table.insert(CHATS, { Id = "all", Members = { "all" }, Name = "", StartedBy = "p1", StartedTurn = 1, Lines = 1, Unread = 0, LastTurn = 80, Stale = false,
	Last = { Time = "1", Turn = 80, From = "p1", Text = "hello all" } })
handlers.update(1)
-- a civilization the game's AI plays is a bot: under BOTS, after everything else, its card on a click, never a chat
local kids = Controls.ListStack._children
local botsAt
for i, ch in ipairs(kids) do if ch.name == "SectionInstance" and ch.inst.Text:GetText():find("BOTS") then botsAt = i end end
check(botsAt ~= nil and kids[#kids].name == "RowInstance" and kids[botsAt + 1] == kids[#kids], "BOTS is the last section, one row under it")
local darius = botsAt and kids[botsAt + 1] and kids[botsAt + 1].inst
check(darius and darius.Name:GetText():find("Darius") and darius.Sub:GetText():find("PERSIA"), "a bot's row: its leader and civilization")
h = darius and hover(darius.Root)
check(h and h.leader1:find("DARIUS") and h.civ1:find("PERSIA") and h.click, "a bot's hover: what it is, and click to show more")
local sentBefore = #SENT
if darius then click(darius.Root) end
check(not Controls.LeaderCard:IsHidden() and Controls.CardTitle:GetText():find("DARIUS"), "a click on a bot opens its card")
check(find(Controls.ListStack, "RowInstance", function(r) return not r.Selected:IsHidden() end) == nil and #SENT == sentBefore, "a bot opens no chat")
Controls.LeaderCard:SetHide(true)
check(find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Ashoka") end) ~= nil, "a civilization an agent plays stays in its own section")
local everyone = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Everyone") end)
check(everyone ~= nil, "the whole table has a row")
click(everyone.Root)
handlers.update(1)
local darius
for _, ch in ipairs(Controls.HeadPortraits._children) do
	if ch.name == "HeadInstance" and ch.inst.Name:GetText():find("Darius") then darius = ch.inst end
end
check(darius ~= nil, "the whole table's strip shows every civilization met, the game AI's too")

-- hovering a leader is a snippet, the leader and civilization; the card holds the rest
local ashokaRow = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Ashoka") end)
h = ashokaRow and hover(ashokaRow.Root)
check(h and h.rows == 1 and h.leader1:find("ASHOKA") and h.civ1:find("INDIA") and not h.click,
	"a chat row's hover: the leader and civilization, nothing more")
local valletta = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Valletta") end)
check(valletta and hover(valletta.Root) == nil, "a city-state's row has no leader hover")

-- clicking a leader opens its card; with a second leader merged, both civilizations, the clashing piece faded
Game.SecondLeader = function(id)
	if id ~= 1 then return nil end
	return { Civ = "CIV_ENGLAND", Leader = "LEADER_ELIZABETH", Traits = { { Type = "TRAIT_OCEAN", Merged = true, LeftOut = "its ban on training Settler (bans never pass on)" } },
		Units = { { Type = "UNIT_LONGBOW", Replaces = "UNIT_CROSSBOWMAN", Merged = true }, { Type = "UNIT_HORSEMAN", Replaces = "UNIT_HORSEMAN", Merged = false } }, Buildings = {},
		Improvements = { { Type = "IMPROVEMENT_CASTLE", Merged = true } } }
end
-- Second Leader's rows on screens: the merged Longbow also listed as India's, marked as the second's
local realUnitOverrides, realImprovements = GameInfo.Civilization_UnitClassOverrides, GameInfo.Improvements
GameInfo.Civilization_UnitClassOverrides = Table({ { CivilizationType = "CIV_INDIA", UnitClassType = "UNITCLASS_HORSEMAN", UnitType = "UNIT_ELEPHANT" },
	{ CivilizationType = "CIV_INDIA", UnitClassType = "UNITCLASS_SETTLER" },
	{ CivilizationType = "CIV_INDIA", UnitClassType = "UNITCLASS_CROSSBOWMAN", UnitType = "UNIT_LONGBOW", SecondLeaderFrom = "CIV_ENGLAND" } })
-- the merged Castle is also listed as India's, marked as the second's, as SecondLeaderRows.lua lists it on screens
GameInfo.Improvements = Table({ { Type = "IMPROVEMENT_CASTLE", CivilizationType = "CIV_ENGLAND", Description = "Castle", PortraitIndex = 4, IconAtlas = "I" },
	{ Type = "IMPROVEMENT_CASTLE", CivilizationType = "CIV_INDIA", SecondLeaderFrom = "CIV_ENGLAND", Description = "Castle", PortraitIndex = 4, IconAtlas = "I" } })
local ashokaHead
for _, ch in ipairs(Controls.HeadPortraits._children) do
	if ch.name == "HeadInstance" and ch.inst.Name:GetText():find("Ashoka") then ashokaHead = ch.inst end
end
h = ashokaHead and hover(ashokaHead.Root)
check(h and h.rows == 2 and h.leader2:find("ELIZABETH") and h.civ2:find("ENGLAND") and h.pictures2 and h.click,
	"a header portrait's hover: both leaders, each on its own row, and click to show more")
click(ashokaHead.Root)
check(not Controls.LeaderCard:IsHidden(), "a click opens the leader card")
check(children(Controls.CardStack, "CardCivInstance") == 2, "the card shows both civilizations")
check(Controls.CardNote:IsHidden(), "a merged card has no note at the bottom")
check(children(Controls.CardStack._children[1].inst.Uniques, "CardUniqueInstance") == 1,
	"the first shows its own uniques only: not a forbidden class, not the second's merged Longbow")
local second = Controls.CardStack._children[2].inst
check(second.Name:GetText():find("SECOND") and second.Name:GetText():find("ELIZABETH"), "the second is named as the second")
check(children(second.Uniques, "CardUniqueInstance") == 3, "the second's uniques, merged or not, its improvement too")
local castle = second.Uniques._children[3].inst
check((castle.Root._alpha or 1) == 1 and castle.Root._tip:find("Castle") and not castle.Root._tip:find("Not merged"),
	"the second's unique improvement is merged: shown plainly, under the second")
check(second.Ability:GetText():find("without its ban on training Settler", 1, true) and not second.Ability:GetText():find("not merged", 1, true),
	"a merged ability names what of it stays out")
local faded = second.Uniques._children[2].inst
check(faded.Root._alpha < 1 and faded.Root._tip:find("Not merged"), "a unique left out is faded and says why")
handlers.input(KeyEvents.KeyDown, Keys.VK_ESCAPE, 0)
check(Controls.LeaderCard:IsHidden() and not Controls.Window:IsHidden(), "Escape closes the card first")
Game.SecondLeader = nil
GameInfo.Civilization_UnitClassOverrides, GameInfo.Improvements = realUnitOverrides, realImprovements

-- ===== city-states: every one met, from the MCP plugin; its ally may speak, the rest are greyed and read-only =====
Players[23] = player("Kabul", "Kabul", 23, true, 1)
CITYSTATES = { { Player = 22, Name = "Valletta", Speakable = true }, { Player = 23, Name = "Kabul", Speakable = false } }
TALK[22] = { { Turn = 81, Mine = true, Text = "Send your archers north." }, { Turn = 81, Mine = false, Text = "They march at dawn." } }
TALK[23] = { { Turn = 40, Mine = true, Text = "Hold the pass." } }
handlers.update(1)
local function section(name)
	local out, inIt = {}, false
	for _, ch in ipairs(Controls.ListStack._children) do
		if ch.name == "SectionInstance" then inIt = ch.inst.Text:GetText():gsub("%[[^%]]*%]", ""):find("^" .. name) ~= nil
		elseif inIt then table.insert(out, ch.inst) end
	end
	return out
end
local function named(rows, n) for _, r in ipairs(rows) do if r.Name:GetText():find(n) then return r end end end
local vRow, kRow = named(section("ALLIED CITY%-STATES"), "Valletta"), named(section("CITY%-STATES"), "Kabul")
check(vRow ~= nil, "a city-state you lead is under Allied city-states")
check(named(section("ALLIES"), "Valletta") == nil, "and not under Allies, which is for teammates")
check(kRow ~= nil, "a city-state you do not lead is under City-states")
local order = {}
for _, ch in ipairs(Controls.ListStack._children) do if ch.name == "SectionInstance" then table.insert(order, (ch.inst.Text:GetText():gsub("%[[^%]]*%]", ""))) end end
local function at(n) for i, t in ipairs(order) do if t:find("^" .. n) then return i end end end
check(at("GROUPS") and at("ALLIED CITY%-STATES") == at("GROUPS") + 1, "Allied city-states come right under Groups")
check(at("CITY%-STATES") == at("ALLIED CITY%-STATES") + 1, "then the other city-states")
check(at("BOTS") == #order, "Bots stay last, after City-states")
local vCount = 0
for _, ch in ipairs(Controls.ListStack._children) do if ch.name == "RowInstance" and ch.inst.Name:GetText():find("Valletta") then vCount = vCount + 1 end end
check(vCount == 1, "a city-state with an agent seat is listed once")
check(vRow and (vRow.Root._alpha or 1) == 1, "a city-state you may speak to is not greyed")
check(kRow and kRow.Root._alpha ~= nil and kRow.Root._alpha < 1, "a city-state you may not speak to is greyed")
check(kRow and kRow.Root._tip and kRow.Root._tip:find("Only its ally"), "its hover says only its ally can speak to it")
check(vRow and vRow.Sub:GetText():find("They march at dawn"), "a city-state's row shows its last line")

click(vRow.Root)
handlers.update(1)
check(children(Controls.ChatStack, "MineInstance") == 1 and children(Controls.ChatStack, "TheirsInstance") == 1, "its conversation shows both sides")
check(not Controls.Field:IsHidden() and not Controls.SendButton:IsHidden() and Controls.ReadOnly:IsHidden(), "you can write to the city-state you lead")
local mcBefore = #SENT
Controls.ChatEntry:SetText("Take the hill.")
Controls.ChatEntry._cb.default()
check(#CS_SENT == 1 and CS_SENT[1].player == 22 and CS_SENT[1].text == "Take the hill." and #SENT == mcBefore, "it goes through the game's city-state channel, not a chat file")
check(Controls.MembersButton:IsHidden(), "a city-state's chat has no Members button")
check(not Controls.DismissButton:IsHidden(), "a city-state a model plays for you has a Dismiss button")
click(Controls.DismissButton)
check(#CS_SENT == 2 and CS_SENT[2].player == 22 and CS_SENT[2].text == "dismiss", "Dismiss switches it off through the game's channel, as writing dismiss does")
local csSent = #CS_SENT

click(kRow.Root)
handlers.update(1)
check(children(Controls.ChatStack, "MineInstance") == 1, "a greyed city-state still shows your past conversation")
check(Controls.Field:IsHidden() and Controls.SendButton:IsHidden() and not Controls.ReadOnly:IsHidden(), "a greyed city-state is read-only")
check(Controls.ReadOnly:GetText():find("Only its ally"), "and says why")
check(Controls.DismissButton:IsHidden(), "a city-state you do not lead has no Dismiss button")
Controls.ChatEntry:SetText("Let me in.")
Controls.ChatEntry._cb.default()
check(#CS_SENT == csSent, "nothing is sent to a city-state you do not lead")

-- a city-state's new line counts as unread and dings
Controls.Window:SetHide(true)
local dings = sounds
table.insert(TALK[22], { Turn = 82, Mine = false, Text = "The hill is ours." })
handlers.update(3)
check(sounds == dings + 1 and not Controls.OpenPill:IsHidden(), "a city-state's new line dings and lights the opener")
click(Controls.OpenButton)
check(not Controls.Window:IsHidden(), "the window opens again")

-- New group: a city-state an agent plays (Valletta holds seat p23) can be a member, one the game plays cannot
click(find(Controls.ListStack, "SectionInstance", function(s) return s.Text:GetText():find("GROUPS") end).NewButton)
check(member("Valletta") ~= nil, "New group lists a city-state an agent plays")
check(member("Kabul") == nil, "and not one the game plays: nobody would read it")
click(member("Valletta").Root)
check(Controls.StartGroup._disabled == true, "a city-state alone starts no group: alone it is reached through the game")
check(not Controls.ExistsNote:IsHidden() and Controls.ExistsText:GetText():find("another member"), "and it says to add another member")
click(member("Ashoka").Root)
check(Controls.StartGroup._disabled == false, "with a civilization beside it the group can start")
click(Controls.StartGroup)
local sentBefore = #SENT
Controls.ChatEntry:SetText("Hold the strait, both of you.")
Controls.ChatEntry._cb.default()
local toCs = SENT[#SENT]
check(#SENT == sentBefore + 1 and toCs and table.concat(toCs.members, ","):find("p23") and table.concat(toCs.members, ","):find("p1"),
	"the group is written to by seat: the city-state's and the civilization's")
check(#CS_SENT == csSent, "and nothing more goes down the ally's own line")
-- such a group in the list and in the chat: the city-state by its name
table.insert(CHATS, { Id = "p23+human+p1", Members = { "p23", "human", "p1" }, Name = "", StartedBy = "human", StartedTurn = 82, Lines = 1, Unread = 0,
	LastTurn = 82, Stale = false, Last = { Time = "1", Turn = 82, From = "p23", Text = "The strait is held." } })
LINES["p23+human+p1"] = { { Time = "1", Turn = 82, From = "p23", Text = "The strait is held." } }
handlers.update(3)
local grow = find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Valletta") and r.Name:GetText():find("Ashoka") end)
check(grow ~= nil, "a group with a city-state lists it by name")
local vCount2 = 0
for _, ch in ipairs(Controls.ListStack._children) do if ch.name == "RowInstance" and ch.inst.Name:GetText():find("Valletta") then vCount2 = vCount2 + 1 end end
check(vCount2 == 2, "the city-state's own row stays, beside the group")

-- the whole table's strip is the players at the table: no city-state
click(find(Controls.ListStack, "RowInstance", function(r) return r.Name:GetText():find("Everyone") end).Root)
handlers.update(1)
local csHead = false
for _, ch in ipairs(Controls.HeadPortraits._children) do
	if ch.name == "HeadInstance" and (ch.inst.Name:GetText():find("Valletta") or ch.inst.Name:GetText():find("Kabul")) then csHead = true end
end
check(not csHead, "the whole table's strip shows no city-state")
check(not Controls.Field:IsHidden() and Controls.ReadOnly:IsHidden(), "a chat file is writable again")

-- an agent the human has not met: its messages arrive, and nothing in the window says who it is
Players[6] = player("Hittites", "Muwatallis (Claude Opus p6)", 6, false, -1)
CIVTYPE.Hittites = 6
local seats = Game.MCSeats
Game.MCSeats = function() local s = seats(); table.insert(s, { Token = "p6", Civ = "Hittites" }); return s end
Teams[0].IsHasMet = function(_, team) return team ~= 6 end
table.insert(CHATS, { Id = "human+p6", Members = { "human", "p6" }, Name = "", StartedBy = "p6", StartedTurn = 1,
	Lines = 1, Unread = 1, LastTurn = 1, Stale = false, Last = { Time = "1", Turn = 1, From = "p6", Text = "A new crown takes its seat." } })
LINES["human+p6"] = { { Time = "1", Turn = 1, From = "p6", Text = "A new crown takes its seat." } }
click(Controls.CloseButton)
handlers.update(3)
click(Controls.OpenButton)
local function texts(parent, acc)
	acc = acc or {}
	for _, ch in ipairs(parent._children) do
		for _, c in pairs(ch.inst) do if type(c) == "table" and c.GetText then table.insert(acc, c:GetText()) end end
	end
	return acc
end
local function tells(list) for _, s in ipairs(list) do if s:find("Hittites") or s:find("Muwatallis") then return true end end return false end
local stranger = find(Controls.ListStack, "RowInstance", function(r) return r.Sub:GetText():find("A new crown") end)
check(stranger ~= nil, "an unmet agent's message has a row")
check(stranger and stranger.Name:GetText():find("Unmet ruler"), "the row names an unmet ruler")
check(not tells(texts(Controls.ListStack)), "the list never names the unmet civilization or its leader")
local unknownFace, hookup = false, IconHookup
IconHookup = function(index, size, atlas, ...) if index == 22 and atlas == "LEADER_ATLAS" then unknownFace = true end return hookup(index, size, atlas, ...) end
if stranger then click(stranger.Root) end
handlers.update(1)
IconHookup = hookup
check(not tells(texts(Controls.ChatStack)) and not tells(texts(Controls.HeadPortraits)), "nor does the chat or its header")
check(unknownFace, "the unmet ruler shows the game's unknown leader")
local head = find(Controls.HeadPortraits, "HeadInstance", function(r) return r.Name:GetText():find("Unmet ruler") end)
Controls.LeaderCard:SetHide(true)
if head and head.Root._cb.click then head.Root._cb.click() end
check(head ~= nil and Controls.LeaderCard:IsHidden(), "an unmet ruler has no leader card to open")
Teams[0].IsHasMet = function() return true end
click(Controls.CloseButton)
handlers.update(3)
click(Controls.OpenButton)
local known = find(Controls.ListStack, "RowInstance", function(r) return r.Sub:GetText():find("A new crown") end)
check(known and known.Name:GetText():find("Muwatallis"), "once met, the same chat shows who it was")
click(Controls.CloseButton)
click(Controls.OpenButton)

-- the cog: switches other mods offer, each a row to click
local asks = sent("MCToggleAsk")
click(Controls.SettingsButton)
check(not Controls.SettingsPop:IsHidden(), "the cog opens the settings")
check(sent("MCToggleAsk") == asks + 1, "opening them asks the other mods for their switches")
check(children(Controls.SettingsStack, "SettingInstance") == 0 and Controls.SettingsNote:GetText():find("Nothing to switch"), "with no mod offering one, it says so")
local perfOn = false
LuaEvents.MCToggleSet.Add(function(id, on)   -- a stand-in mod: it switches, then offers its new state
	if id == "perf" then perfOn = on; LuaEvents.MCToggleOffer("perf", "Frame-time tracker", "Logs frame times.", on) end
end)
LuaEvents.MCToggleOffer("perf", "Frame-time tracker", "Logs frame times.", false)
local prow = find(Controls.SettingsStack, "SettingInstance", function(r) return r.Name:GetText():find("Frame%-time tracker") end)
check(prow ~= nil, "an offered switch shows as a row")
check(prow and not prow.Check:IsHidden() and prow.CheckOn:IsHidden(), "it shows off")
check(prow and prow.Root._tip == "Logs frame times.", "its hover says what it does")
click(prow.Root)
check(perfOn == true, "clicking the row switches it on in its mod")
prow = find(Controls.SettingsStack, "SettingInstance", function(r) return r.Name:GetText():find("Frame%-time tracker") end)
check(prow and prow.Check:IsHidden() and not prow.CheckOn:IsHidden(), "the row shows on once the mod says it is")
check(children(Controls.SettingsStack, "SettingInstance") == 1, "one row per switch, however often it is offered")
click(prow.Root)
check(perfOn == false, "clicking again switches it off")
handlers.input(KeyEvents.KeyDown, Keys.VK_ESCAPE)
check(Controls.SettingsPop:IsHidden() and not Controls.Window:IsHidden(), "Escape closes the settings first")
click(Controls.SettingsButton)
click(Controls.CloseButton)
check(Controls.SettingsPop:IsHidden(), "closing the window closes the settings")

print(string.format("%d/%d passed", run - failed, run))
os.exit(failed == 0 and 0 or 1)
