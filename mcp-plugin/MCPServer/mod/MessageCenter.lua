-- Message Center: the window where the human writes to AI players, one or several at once. The MCP Server plugin keeps
-- the chats (Game.MC*); agents speak in them with the say tool.
include("IconSupport");

if Game.MCChats == nil then
	Controls.OpenButton:SetHide(true);
	print("MessageCenter: the MCP server plugin is not running, so the Message Center is off");
	return;
end

local MIN_W, MIN_H, MAX_W, MAX_H = 760, 480, 1400, 900;
local LIST_W = 320;
local MAX_LINES_SHOWN = 200;
local GOLD = "[COLOR:226:201:138:255]";
local DIM = "[COLOR:169:179:172:255]";
local LIGHT = "[COLOR:236:231:216:255]";
local END = "[ENDCOLOR]";

local g_Settings = Modding.OpenUserData("MessageCenter", 1);
-- Lists are rebuilt from fresh instances, so a stack shows its children in the order they are made.
local function Build(name, parent)
	local inst = {};
	ContextPtr:BuildInstanceForControl(name, inst, parent);
	return inst;
end
local g_ListKey = nil;

local g_W, g_H = 1080, 720;
local g_Chats = {};        -- id -> chat from Game.MCChats()
local g_People = {};       -- token -> { Player, Leader, Civ, Ally, Minor, CityState, Speakable }
local g_CsSeen = {};       -- city-state player -> the last line of its conversation the human has seen
local g_Sel = nil;         -- { Id, Members = {tokens} }
local g_Rendered = { Id = nil, Count = -1, Width = -1 };
local g_LastUnread = nil;
local g_Chosen = {};       -- New group: token -> true
local g_Drag = nil;        -- { Kind = "move"|"size", X, Y, W, H, OffX, OffY }

-- ===== who is who =====

local function Brighten(c)
	local r, g, b = c.x * 255, c.y * 255, c.z * 255;
	local lum = 0.3 * r + 0.59 * g + 0.11 * b;
	local mix = lum < 140 and 0.45 or 0.15;
	return math.floor(r + (255 - r) * mix), math.floor(g + (255 - g) * mix), math.floor(b + (255 - b) * mix);
end

local function NameColor(player)
	local primary, secondary = player:GetPlayerColors();
	local function lum(c) return 0.3 * c.x + 0.59 * c.y + 0.11 * c.z; end
	local c = (lum(primary) > lum(secondary)) and primary or secondary;
	return string.format("[COLOR:%d:%d:%d:255]", Brighten(c));
end

local function Norm(s)
	return string.lower((s or ""):gsub("[^%w]", ""));
end

-- A leader's own name: the game's player name, less what a seat adds after it ("Suleiman (Claude Opus seat-51)").
local function LeaderName(p)
	return (p:GetName():gsub("%s*%b()%s*$", ""));
end

-- The plugin names each civilization a model plays (Game.MCSeats: its token and civilization); match them to the game's players.
local function RefreshPeople()
	local byCiv = {};
	for _, seat in ipairs(Game.MCSeats() or {}) do
		byCiv[Norm(seat.Civ)] = seat.Token;
	end
	local me = Game.GetActivePlayer();
	local myTeam = Game.GetActiveTeam();
	-- A city-state an agent plays is known two ways: by its row's token ("cs:<player>", its talk with its ally through
	-- the game) and by its seat's token (a member of group chats). Both name one person; only the
	-- row's token is listed.
	local bySeat = {};
	g_People = setmetatable({}, { __index = function(t, k) local row = bySeat[k]; return row and rawget(t, row) or nil; end });
	-- An agent the human has not met can still write to the human; until they meet it is an unmet ruler, lettered by
	-- its place among the agents' civilizations so the letter holds while others are met: no name, colour or face.
	local letter = 0;
	for i = 0, GameDefines.MAX_CIV_PLAYERS - 1 do
		local p = Players[i];
		if p ~= nil and p:IsAlive() and i ~= me and not p:IsBarbarian() then
			local civ = p:GetCivilizationShortDescription();
			local token = byCiv[Norm(civ)];
			if token ~= nil and not p:IsMinorCiv() then
				local minor = p:IsMinorCiv();
				local ally = (p:GetTeam() == myTeam) or (minor and p:GetAlly() == me);
				local met = (p:GetTeam() == myTeam) or Teams[myTeam]:IsHasMet(p:GetTeam());
				letter = letter + 1;
				if met then
					g_People[token] = {
						Player = i, Civ = civ, Minor = minor, Ally = ally,
						Leader = minor and civ or LeaderName(p),
						Met = true,
						Color = NameColor(p),
					};
				else
					g_People[token] = {
						Player = i, Civ = "", Minor = false, Ally = false, Unknown = true,
						Leader = "Unmet ruler " .. string.char(64 + ((letter - 1) % 26) + 1),
						Met = false, Color = LIGHT,
					};
				end
			end
		end
	end
	-- city-states, from the MCP plugin: every one the human has met, and whether the human may speak to it (only its
	-- ally may). Their talk goes through the game, not the chats.
	for _, cs in ipairs((Game.GetLLMCityStates and Game.GetLLMCityStates()) or {}) do
		local p = Players[cs.Player];
		local seat = byCiv[Norm(cs.Name)];
		if seat ~= nil then bySeat[seat] = "cs:" .. cs.Player; end
		g_People["cs:" .. cs.Player] = {
			Player = cs.Player, Civ = cs.Name, Leader = cs.Name, Minor = true, CityState = true, Seat = seat,
			Speakable = cs.Speakable == true, Ally = cs.Speakable == true, Met = true,
			Color = p and NameColor(p) or LIGHT,
		};
	end
	-- the other civilizations met, played by the game's AI: in the whole table's strip, with their leader card, but no
	-- chat (no agent reads it)
	local held = {};
	for _, person in pairs(g_People) do held[person.Player] = true; end
	for i = 0, GameDefines.MAX_MAJOR_CIVS - 1 do
		local p = Players[i];
		if p ~= nil and p:IsAlive() and i ~= me and not held[i] and not p:IsMinorCiv() and Teams[myTeam]:IsHasMet(p:GetTeam()) then
			g_People["player-" .. i] = {
				Player = i, Civ = p:GetCivilizationShortDescription(), Minor = false, Ally = p:GetTeam() == myTeam,
				Leader = LeaderName(p), Met = true, Color = NameColor(p), NoChat = true,
			};
		end
	end
end

local function Label(token)
	if token == "human" then return "You"; end
	-- a model playing your own civilization speaks in your chats as itself
	if token == "human-ai" then return "Your AI"; end
	local person = g_People[token];
	return person and person.Leader or token;
end

local function LabelWithCiv(token)
	local person = g_People[token];
	if person == nil then return Label(token); end
	if person.Unknown then return LIGHT .. person.Leader .. END; end
	if person.Minor then return person.Color .. person.Civ .. END; end
	return person.Color .. person.Leader .. END .. DIM .. "  " .. person.Civ .. END;
end

-- A member's picture on an Image (and its emblem controls when there are any): the leader's portrait at 64 and up,
-- the civilization's emblem below that (the game draws leaders at 64, 128 and 256 only, and an Image crops, never
-- scales), a city-state's emblem always. "human" is the active player.
local function Portrait(token, image, size, iconBG, icon, iconShadow)
	local person = g_People[token];
	local id = (token == "human" or token == "human-ai") and Game.GetActivePlayer() or (person and person.Player);
	if iconBG then iconBG:SetHide(true); end
	image:SetHide(true);
	if id == nil then return; end
	if person and person.Unknown then   -- the game's own faces for a player not met
		if size >= 64 then
			if IconHookup(22, 64, "LEADER_ATLAS", image) then image:SetHide(false); end
		elseif iconBG then
			iconBG:SetHide(false);
			CivIconHookup(-1, 45, icon, iconBG, iconShadow, false, true);
		end
		return;
	end
	if person and person.Minor and iconBG then
		iconBG:SetHide(false);
		CivIconHookup(id, (size >= 64) and 64 or 45, icon, iconBG, iconShadow, false, true);
		return;
	end
	if size < 64 then
		if SimpleCivIconHookup(id, size, image) then image:SetHide(false); end
		return;
	end
	local leader = GameInfo.Leaders[Players[id]:GetLeaderType()];
	if leader ~= nil and IconHookup(leader.PortraitIndex, 64, leader.IconAtlas, image) then image:SetHide(false); end
end

-- ===== what a leader brings: abilities and uniques, a second leader's too (Second Leader's option) =====

local function TextOf(key) return key and Locale.ConvertTextKey(key) or "" end

-- A civilization's uniques: { Kind, Info (its row), Replaces (the standard row it replaces) }.
local function Uniques(civType)
	local out = {};
	for row in GameInfo.Civilization_UnitClassOverrides{ CivilizationType = civType } do
		local unit = row.UnitType and GameInfo.Units[row.UnitType];
		local cls = GameInfo.UnitClasses[row.UnitClassType];
		if unit then table.insert(out, { Kind = "unit", Info = unit, Replaces = cls and GameInfo.Units[cls.DefaultUnit] }); end
	end
	for row in GameInfo.Civilization_BuildingClassOverrides{ CivilizationType = civType } do
		local b = row.BuildingType and GameInfo.Buildings[row.BuildingType];
		local cls = GameInfo.BuildingClasses[row.BuildingClassType];
		if b then table.insert(out, { Kind = "building", Info = b, Replaces = cls and GameInfo.Buildings[cls.DefaultBuilding] }); end
	end
	for imp in GameInfo.Improvements{ CivilizationType = civType } do
		table.insert(out, { Kind = "improvement", Info = imp });
	end
	return out;
end

-- Each civilization a player is: its own, then its second when the option merged one. Every ability and unique carries
-- Merged (false: the second's, left out: for an ability, only when the game does not have it), and for a second's
-- ability LeftOut: what of it stays out (a slot the first's fills, its bans), in words.
function LeaderParts(playerID)
	local p = Players[playerID];
	if p == nil then return {}; end
	local civ = GameInfo.Civilizations[p:GetCivilizationType()];
	local leader = GameInfo.Leaders[p:GetLeaderType()];
	if civ == nil or leader == nil then return {}; end
	local own = { Civ = civ, Leader = leader, Abilities = {}, Uniques = Uniques(civ.Type) };
	for row in GameInfo.Leader_Traits{ LeaderType = leader.Type } do
		local t = GameInfo.Traits[row.TraitType];
		if t then table.insert(own.Abilities, { Info = t, Merged = true }); end
	end
	for _, u in ipairs(own.Uniques) do u.Merged = true; end
	local parts = { own };
	local s = Game.SecondLeader and Game.SecondLeader(playerID);
	if s then
		local second = { Civ = GameInfo.Civilizations[s.Civ], Leader = GameInfo.Leaders[s.Leader], Abilities = {}, Uniques = {}, Second = true };
		for _, t in ipairs(s.Traits or {}) do
			local info = GameInfo.Traits[t.Type];
			if info then table.insert(second.Abilities, { Info = info, Merged = t.Merged, LeftOut = t.LeftOut, Clash = t.Clash }); end
		end
		local lists = { { s.Units or {}, "unit", GameInfo.Units }, { s.Buildings or {}, "building", GameInfo.Buildings } };
		for _, l in ipairs(lists) do
			for _, u in ipairs(l[1]) do
				local info = l[3][u.Type];
				if info then table.insert(second.Uniques, { Kind = l[2], Info = info, Replaces = u.Replaces and l[3][u.Replaces], Merged = u.Merged }); end
			end
		end
		if second.Civ and second.Leader then table.insert(parts, second); end
	end
	return parts;
end

local function UniqueLine(u)
	local s = TextOf(u.Info.Description);
	if u.Replaces then s = s .. " (replaces " .. TextOf(u.Replaces.Description) .. ")"; end
	return s;
end

-- The hover for a leader: a row for each civilization it is (its own, then a second one), each the leader's portrait
-- and name over the civilization's emblem and name; "Click to show more" where a click opens the card.
local g_Tip = {};
TTManager:GetTypeControlTable("MCLeaderTip", g_Tip);

local function FillLeaderTip(playerID, withClick)
	local parts = LeaderParts(playerID);
	local width = 0;
	for i = 1, 2 do
		local part = parts[i];
		g_Tip["TipRow" .. i]:SetHide(part == nil);
		if part then
			local leader, civ = g_Tip["TipLeader" .. i], g_Tip["TipCiv" .. i];
			leader:SetText(LIGHT .. TextOf(part.Leader.Description) .. END);
			civ:SetText(DIM .. TextOf(part.Civ.ShortDescription) .. END);
			local portrait, emblem = g_Tip["TipPortrait" .. i], g_Tip["TipEmblem" .. i];
			portrait:SetHide(not IconHookup(part.Leader.PortraitIndex, 64, part.Leader.IconAtlas, portrait));
			emblem:SetHide(not IconHookup(part.Civ.PortraitIndex, 24, part.Civ.AlphaIconAtlas, emblem));
			width = math.max(width, 76 + leader:GetSizeX(), 104 + civ:GetSizeX());
		end
	end
	g_Tip.TipClick:SetHide(not withClick);
	if withClick then width = math.max(width, g_Tip.TipClick:GetSizeX()); end
	for i = 1, 2 do g_Tip["TipRow" .. i]:SetSizeX(width); end
	g_Tip.TipLine:SetSizeX(width);
	g_Tip.TipStack:CalculateSize();
	g_Tip.TipStack:ReprocessAnchoring();
	g_Tip.TipFrame:SetSizeVal(g_Tip.TipStack:GetSizeX() + 28, g_Tip.TipStack:GetSizeY() + 24);
end

local function SetLeaderTip(control, playerID, withClick)
	control:SetToolTipType("MCLeaderTip");
	control:SetToolTipCallback(function() FillLeaderTip(playerID, withClick); end);
end

local function Others(chat)
	local out = {};
	for _, m in ipairs(chat.Members or {}) do
		if m ~= "human" then table.insert(out, m); end
	end
	return out;
end

local function ChatTitle(id, members)
	local chat = g_Chats[id];
	if id == "all" then return "Everyone"; end
	if chat and chat.Name ~= "" then return chat.Name; end
	if #members == 1 then return Label(members[1]); end
	local names = {};
	for _, m in ipairs(members) do table.insert(names, Label(m)); end
	return table.concat(names, ", ");
end

-- ===== city-states' conversations (the MCP plugin keeps them) =====

local NOT_ALLY = "Only its ally can speak to it. You are not its ally now.";

local function CityState(token)
	local person = token and g_People[token];
	return person and person.CityState and person or nil;
end

-- The conversation as chat lines: { Turn, From ("human" or the city-state's token), Text }.
local function CsLines(token)
	local person = CityState(token);
	local out = {};
	if person == nil or Game.LLMCityStateTalk == nil then return out; end
	for _, l in ipairs(Game.LLMCityStateTalk(person.Player) or {}) do
		table.insert(out, { Turn = l.Turn, From = l.Mine and "human" or token, Text = l.Text });
	end
	return out;
end

local function LineKey(l) return l and (l.Turn .. "|" .. l.From .. "|" .. l.Text) or ""; end

-- The plugin keeps the latest lines only, so what is unread counts back from the end to the last line seen.
local function CsUnread(token, lines)
	local seen = g_CsSeen[token];
	if seen == nil then g_CsSeen[token] = LineKey(lines[#lines]); return 0; end
	for i = #lines, 1, -1 do
		if LineKey(lines[i]) == seen then return #lines - i; end
	end
	return #lines;
end

local function AddCityStateChats()
	for token, person in pairs(g_People) do
		if person.CityState then
			local lines = CsLines(token);
			if #lines > 0 then
				local last = lines[#lines];
				g_Chats[token] = { Id = token, Members = { "human", token }, Name = "", Lines = #lines, Unread = CsUnread(token, lines),
					LastTurn = last.Turn, Stale = false, Last = last, CityState = true };
			end
		end
	end
end

-- ===== the list =====

local function SelectChat(id, members)
	g_Sel = { Id = id, Members = members };
	g_Rendered.Id = nil;
	g_ListKey = nil;
end

local function AddRow(token, chat, selected, group)
	local row = Build("RowInstance", Controls.ListStack);
	row.Portrait:SetHide(true);
	row.IconBG:SetHide(true);
	row.Portrait1:SetHide(true);
	row.Portrait2:SetHide(true);
	local members, id, title;
	if group then
		members = (chat.Id == "all") and { "all" } or Others(chat);
		id = chat.Id;
		title = ChatTitle(id, members);
		if members[1] and members[1] ~= "all" then Portrait(members[1], row.Portrait1, 45); end
		if members[2] then Portrait(members[2], row.Portrait2, 45); end
	else
		members = { token };
		id = CityState(token) and token or Game.MCChatId(members);
		title = Label(token);
		Portrait(token, row.Portrait, 64, row.IconBG, row.Icon, row.IconShadow);
	end
	row.Name:SetText(group and (LIGHT .. title .. END) or LabelWithCiv(token));
	row.Name:SetTruncateWidth(LIST_W - 78 - 30);
	local person = token and g_People[token];
	if person and not person.Minor and not person.Unknown then SetLeaderTip(row.Root, person.Player, false); end
	row.Ally:SetHide(not (person and person.Ally));
	row.Ally:SetOffsetX(78 + row.Name:GetSizeX() + 6);
	local sub;
	if chat == nil then
		sub = (person and person.Minor) and "City-state" or ((person and person.Unknown) and "Not met yet" or (person and person.Civ or ""));
	elseif chat.Stale then
		sub = string.format("Quiet since turn %d", chat.LastTurn);
	else
		local who = (chat.Last.From == "human") and "You" or Label(chat.Last.From);
		sub = (group and (who .. ": ") or ((chat.Last.From == "human") and "You: " or "")) .. chat.Last.Text;
	end
	row.Sub:SetText(DIM .. sub .. END);
	row.Sub:SetTruncateWidth(LIST_W - 78 - 44);
	local unread = chat and chat.Unread or 0;
	row.Pill:SetHide(unread == 0);
	if unread > 0 then row.Count:SetText(tostring(unread)); end
	row.Selected:SetHide(not selected);
	row.Fresh:SetHide(selected or unread == 0);   -- a chat with unread lines is lit
	local silent = person and person.CityState and not person.Speakable;
	row.Root:SetAlpha(silent and 0.45 or ((chat and chat.Stale) and 0.55 or 1));
	if silent then row.Root:SetToolTipString(NOT_ALLY); end
	row.Root:RegisterCallback(Mouse.eLClick, function()
		SelectChat(id, members);
		RefreshAll();
	end);
end

local function AddSection(label, withNew)
	local s = Build("SectionInstance", Controls.ListStack);
	s.Text:SetText(LIGHT .. string.upper(label) .. END);
	s.NewButton:SetHide(not withNew);
	if withNew then s.NewButton:RegisterCallback(Mouse.eLClick, function() OpenNewGroup(); end); end
end

-- A row that opens a leader card, not a chat: you, at the head of the list from the first turn, and each bot (a
-- civilization the game's AI plays). It names the leader and every civilization it is (a second one with Merge Random
-- Second Leader).
local function AddCardRow(playerID, token, name)
	local parts = LeaderParts(playerID);
	local row = Build("RowInstance", Controls.ListStack);
	row.IconBG:SetHide(true);
	row.Portrait1:SetHide(true);
	row.Portrait2:SetHide(true);
	Portrait(token, row.Portrait, 64, row.IconBG, row.Icon, row.IconShadow);
	local civs = {};
	for _, part in ipairs(parts) do table.insert(civs, TextOf(part.Civ.ShortDescription)); end
	row.Name:SetText(name);
	row.Name:SetTruncateWidth(LIST_W - 78 - 30);
	row.Sub:SetText(DIM .. table.concat(civs, " + ") .. END);
	row.Sub:SetTruncateWidth(LIST_W - 78 - 44);
	row.Ally:SetHide(true);
	row.Pill:SetHide(true);
	row.Selected:SetHide(true);
	row.Fresh:SetHide(true);
	SetLeaderTip(row.Root, playerID, true);
	row.Root:RegisterCallback(Mouse.eLClick, function() OpenLeaderCard(playerID); end);
end

local function AddYouRow()
	local me = Game.GetActivePlayer();
	local parts = LeaderParts(me);
	AddCardRow(me, "human", GOLD .. "You" .. END .. ((#parts > 0) and (DIM .. "  " .. TextOf(parts[1].Leader.Description) .. END) or ""));
end

local function SortRows(a, b)
	local sa, sb = (a.Chat ~= nil and a.Chat.Stale) == true, (b.Chat ~= nil and b.Chat.Stale) == true;   -- no chat counts as not stale
	if sa ~= sb then return not sa; end
	local ta, tb = a.Chat and a.Chat.LastTurn or -2, b.Chat and b.Chat.LastTurn or -2;
	if ta ~= tb then return ta > tb; end
	return a.Title < b.Title;
end

local function BuildList()
	Controls.ListStack:DestroyAllChildren();
	local allies, civs, allyCities, cities, groups, bots = {}, {}, {}, {}, {}, {};
	for token, person in pairs(g_People) do
		if person.NoChat and person.Met and not person.Minor then table.insert(bots, token); end
		local id = person.CityState and token or Game.MCChatId({ token });
		local chat = g_Chats[id];
		if not person.NoChat and (person.Met or chat) then
			local entry = { Token = token, Chat = chat, Title = Label(token), Id = id };
			if person.Ally and person.Minor then table.insert(allyCities, entry);
			elseif person.Ally then table.insert(allies, entry);
			elseif person.Minor then table.insert(cities, entry);
			else table.insert(civs, entry); end
		end
	end
	for id, chat in pairs(g_Chats) do
		if id == "all" or #chat.Members >= 3 then
			table.insert(groups, { Chat = chat, Title = ChatTitle(id, Others(chat)), Id = id });
		end
	end
	local function section(label, entries, isGroup, withNew)
		if #entries == 0 and not withNew then return; end
		table.sort(entries, SortRows);
		AddSection(label, withNew);
		for _, e in ipairs(entries) do
			AddRow(e.Token, e.Chat, g_Sel ~= nil and g_Sel.Id == e.Id, isGroup);
		end
	end
	AddSection("You", false);
	AddYouRow();
	section("Allies", allies, false, false);
	section("Civilizations", civs, false, false);
	section("Groups", groups, true, true);
	section("Allied city-states", allyCities, false, false);
	section("City-states", cities, false, false);
	if #bots > 0 then
		table.sort(bots, function(x, y) return Label(x) < Label(y); end);
		AddSection("Bots", false);
		for _, t in ipairs(bots) do
			local person = g_People[t];
			AddCardRow(person.Player, t, (person.Color or LIGHT) .. person.Leader .. END);
		end
	end
	Controls.ListStack:CalculateSize();
	Controls.ListStack:ReprocessAnchoring();
	Controls.ListScroll:CalculateInternalSize();
end

-- ===== the chat =====

-- The strip over a chat: everyone in it, you first, as pictures with names on one row; when the names would not fit,
-- the pictures alone. The whole table shows every player met.
local function SetHeader()
	Controls.HeadPortraits:DestroyAllChildren();
	if g_Sel == nil then
		Controls.MembersButton:SetHide(true);
		return;
	end
	local tokens = { "human" };
	if g_Sel.Id == "all" then
		local others = {};
		for token, person in pairs(g_People) do if person.Met and not person.CityState then table.insert(others, token); end end
		table.sort(others, function(a, b) return Label(a) < Label(b); end);
		for _, t in ipairs(others) do table.insert(tokens, t); end
	else
		for _, m in ipairs(g_Sel.Members) do if m ~= "human" then table.insert(tokens, m); end end
	end
	local room = Controls.ChatHeader:GetSizeX() - 16 - 120;   -- left of the Members button
	local items, named = {}, 0;
	for _, t in ipairs(tokens) do
		local inst = Build("HeadInstance", Controls.HeadPortraits);
		Portrait(t, inst.Portrait, 64, inst.IconBG, inst.Icon, inst.IconShadow);
		local person = g_People[t];
		local pid = (t == "human") and Game.GetActivePlayer() or (person and not person.Minor and not person.Unknown and person.Player);
		if pid then
			SetLeaderTip(inst.Root, pid, true);
			inst.Root:RegisterCallback(Mouse.eLClick, function() OpenLeaderCard(pid); end);
		end
		inst.Name:SetText((t == "human") and (GOLD .. "You" .. END) or ((person and person.Color or LIGHT) .. Label(t) .. END));
		local w = 64 + 8 + inst.Name:GetSizeX() + 14;
		named = named + w;
		table.insert(items, { inst = inst, w = w });
	end
	local withNames = named <= room;
	for _, it in ipairs(items) do
		it.inst.Name:SetHide(not withNames);
		it.inst.Root:SetSizeX(withNames and it.w or 64);
	end
	Controls.HeadPortraits:CalculateSize();
	Controls.HeadPortraits:ReprocessAnchoring();
	Controls.MembersButton:SetHide(g_Sel.Id == "all" or CityState(g_Sel.Id) ~= nil);
end

local function ChatWidth()
	return g_W - LIST_W - 12 - 22 - 36;
end

local function AddBubble(inst, text, wrap)
	inst.Text:SetWrapWidth(wrap);
	inst.Text:SetText(LIGHT .. text .. END);
	local w, h = inst.Text:GetSizeX(), inst.Text:GetSizeY();
	inst.Bubble:SetSizeVal(w + 28, h + 18);
	inst.Root:SetSizeVal(ChatWidth() - 16, math.max(52, h + 18 + 22));
end

-- The input row: a city-state that only its ally may speak to is read-only, and says why.
local function SetInput()
	local person = g_Sel and CityState(g_Sel.Id);
	local writable = not (person and not person.Speakable);
	Controls.Field:SetHide(not writable);
	Controls.SendButton:SetHide(not writable);
	Controls.ReadOnly:SetHide(writable);
	if not writable then Controls.ReadOnly:SetText(DIM .. NOT_ALLY .. END); end
end

local function RenderChat(force)
	SetInput();
	if g_Sel == nil then
		Controls.ChatStack:DestroyAllChildren();
		Controls.Empty:SetText(DIM .. "Pick a player or a group on the left." .. END);
		Controls.Empty:SetHide(false);
		return;
	end
	local cs = CityState(g_Sel.Id);
	local lines = cs and CsLines(g_Sel.Id) or (Game.MCLines(g_Sel.Id, 0) or {});
	local width = ChatWidth();
	local count = #lines .. "|" .. LineKey(lines[#lines]);
	if not force and g_Rendered.Id == g_Sel.Id and g_Rendered.Count == count and g_Rendered.Width == width then return; end
	local atBottom = (g_Rendered.Id ~= g_Sel.Id) or Controls.ChatScroll:GetScrollValue() > 0.98 or Controls.ChatScroll:GetRatio() >= 1;
	g_Rendered = { Id = g_Sel.Id, Count = count, Width = width };
	Controls.ChatStack:DestroyAllChildren();
	Controls.Empty:SetHide(#lines > 0);
	Controls.Empty:SetText(DIM .. "No messages yet. Say something." .. END);
	local wrap = math.min(560, width - 170);
	local lastTurn = nil;
	for i = math.max(1, #lines - MAX_LINES_SHOWN + 1), #lines do
		local l = lines[i];
		if l.Turn >= 0 and l.Turn ~= lastTurn then
			lastTurn = l.Turn;
			local t = Build("TurnInstance", Controls.ChatStack);
			t.Root:SetSizeX(width - 16);
			t.Text:SetText(DIM .. "TURN " .. l.Turn .. END);
			local side = math.max(20, (width - 16 - t.Text:GetSizeX()) / 2 - 14);
			t.LeftLine:SetSizeX(side);
			t.RightLine:SetSizeX(side);
		end
		if l.From == "human" then
			local m = Build("MineInstance", Controls.ChatStack);
			m.Who:SetText(GOLD .. "You" .. END);
			AddBubble(m, l.Text, wrap);
		else
			local m = Build("TheirsInstance", Controls.ChatStack);
			local person = g_People[l.From];
			m.Who:SetText(LabelWithCiv(l.From));
			m.Portrait:SetHide(true);
			Portrait(l.From, m.Portrait, 45);
			AddBubble(m, l.Text, wrap);
		end
	end
	Controls.ChatStack:CalculateSize();
	Controls.ChatStack:ReprocessAnchoring();
	Controls.ChatScroll:CalculateInternalSize();
	if atBottom then Controls.ChatScroll:SetScrollValue(1); end
	if not Controls.Window:IsHidden() then
		if cs then g_CsSeen[g_Sel.Id] = LineKey(lines[#lines]); else Game.MCSeen(g_Sel.Id); end
	end
end

-- ===== refresh =====

local function TotalUnread()
	local n = 0;
	for _, c in pairs(g_Chats) do n = n + c.Unread; end
	return n;
end

function RefreshAll()
	RefreshPeople();
	g_Chats = {};
	for _, c in ipairs(Game.MCChats() or {}) do g_Chats[c.Id] = c; end
	AddCityStateChats();
	local unread = TotalUnread();
	Controls.OpenPill:SetHide(unread == 0);
	Controls.OpenCount:SetText(tostring(unread));
	Controls.OpenGlow:SetHide(unread == 0);   -- the opener pulses while anything is unread
	if g_LastUnread ~= nil and unread > g_LastUnread then Events.AudioPlay2DSound("AS2D_IF_MP_CHAT_DING"); end
	g_LastUnread = unread;
	if not Controls.Window:IsHidden() then
		local parts = { g_Sel and g_Sel.Id or "", tostring(g_W) };
		for id, c in pairs(g_Chats) do table.insert(parts, id .. ":" .. c.Lines .. ":" .. c.Unread .. ":" .. c.Name .. ":" .. tostring(c.Stale) .. ":" .. LineKey(c.Last)); end
		for token, p in pairs(g_People) do table.insert(parts, token .. tostring(p.Met) .. tostring(p.Ally) .. tostring(p.Speakable)); end
		table.sort(parts);
		local key = table.concat(parts, "|");
		if key ~= g_ListKey then
			g_ListKey = key;
			BuildList();
			SetHeader();
		end
		RenderChat(false);
	end
end

-- ===== sending =====

local function Send()
	if g_Sel == nil then return; end
	local text = Controls.ChatEntry:GetText() or "";
	text = text:gsub("^%s+", ""):gsub("%s+$", "");
	if text == "" then return; end
	-- a city-state: through the game's channel to it, which only its ally may use (the plugin checks again)
	local cs = CityState(g_Sel.Id);
	if cs then
		if not cs.Speakable or Game.LLMChat == nil then return; end
		Game.LLMChat(cs.Player, text);
		Controls.ChatEntry:ClearString();
		RefreshAll();
		RenderChat(true);
		Controls.ChatScroll:SetScrollValue(1);
		return;
	end
	local id, err = Game.MCSend(g_Sel.Members, text);
	if id == nil then
		-- the words stay in the box; why they were not sent shows under the chat until it next changes
		print("MessageCenter: not sent: " .. tostring(err));
		local note = Build("TurnInstance", Controls.ChatStack);
		note.Root:SetSizeX(ChatWidth() - 16);
		note.Text:SetText(DIM .. "Not sent: " .. tostring(err) .. END);
		note.LeftLine:SetHide(true);
		note.RightLine:SetHide(true);
		Controls.ChatStack:CalculateSize();
		Controls.ChatStack:ReprocessAnchoring();
		Controls.ChatScroll:CalculateInternalSize();
		Controls.ChatScroll:SetScrollValue(1);
		return;
	end
	Controls.ChatEntry:ClearString();
	g_Sel.Id = id;
	RefreshAll();
	RenderChat(true);
	Controls.ChatScroll:SetScrollValue(1);
end
Controls.ChatEntry:RegisterCallback(function() Send(); end);
Controls.SendButton:RegisterCallback(Mouse.eLClick, Send);

-- ===== layout and resizing =====

local function Layout()
	g_W = math.max(MIN_W, math.min(MAX_W, g_W));
	g_H = math.max(MIN_H, math.min(MAX_H, g_H));
	local w, h = g_W, g_H;
	Controls.Window:SetSizeVal(w, h);
	Controls.DragBar:SetSizeVal(w - 50, 42);
	local titleW = Controls.Title:GetSizeX();
	local rule = math.max(20, (w - titleW) / 2 - 40);
	Controls.RuleLeft:SetSizeX(rule);
	Controls.RuleRight:SetSizeX(math.max(20, rule - 30));   -- the cog sits at its right end
	Controls.BandTop:SetSizeX(w - 8);
	Controls.BandMid:SetSizeX(w - 8);
	Controls.BandBottom:SetSizeX(w - 8);
	local paneH = h - 72;
	Controls.ListPane:SetSizeVal(LIST_W, paneH);
	Controls.ListScroll:SetSizeVal(LIST_W - 8, paneH - 8);
	Controls.ListBar:SetSizeY(paneH - 8);
	local cw = w - LIST_W - 12 - 22;
	Controls.ChatPane:SetSizeVal(cw, paneH);
	Controls.ChatHeader:SetSizeX(cw - 8);
	Controls.ChatRule:SetSizeX(cw - 8);
	Controls.ChatRuleTop:SetSizeX(cw - 8);
	Controls.ChatRuleBottom:SetSizeX(cw - 8);
	Controls.ChatScroll:SetSizeVal(cw - 42, paneH - 98 - 72);
	Controls.ChatBar:SetSizeY(paneH - 98 - 72);
	Controls.InputRule:SetSizeX(cw - 8);
	Controls.Field:SetSizeX(cw - 28 - 120 - 10);
	Controls.ChatEntry:SetSizeX(cw - 28 - 120 - 10 - 24);
	SetHeader();   -- its names come and go with the width
	Controls.Window:ReprocessAnchoring();
	g_Settings.SetValue("W", w);
	g_Settings.SetValue("H", h);
end

Controls.Grip:RegisterCallback(Mouse.eLDown, function()
	local x, y = UIManager:GetMousePos();
	g_Drag = { Kind = "size", X = x, Y = y, W = g_W, H = g_H };
end);
Controls.DragBar:RegisterCallback(Mouse.eLDown, function()
	local x, y = UIManager:GetMousePos();
	g_Drag = { Kind = "move", X = x, Y = y, OffX = Controls.Window:GetOffsetX(), OffY = Controls.Window:GetOffsetY() };
end);

-- Ends a move or a resize where the mouse is. The button-up can go astray (it lands on a control, or off the window),
-- so letting go on the bar or the grip, or a click anywhere, drops it.
local function EndDrag()
	if g_Drag == nil then return; end
	if g_Drag.Kind == "size" then BuildList(); RenderChat(true); end
	g_Drag = nil;
end
Controls.DragBar:RegisterCallback(Mouse.eLUp, EndDrag);
Controls.Grip:RegisterCallback(Mouse.eLUp, EndDrag);

-- ===== New group =====

-- The chosen members as the chats name them: a city-state by its agent's seat.
local function ChosenTokens()
	local out = {};
	for token, on in pairs(g_Chosen) do if on then table.insert(out, g_People[token].Seat or token); end end
	table.sort(out);
	return out;
end

-- A city-state alone is reached through the game, by its ally: it joins a chat only beside another member.
local function OnlyACityState(chosen)
	local person = (#chosen == 1) and g_People[chosen[1]];
	return person ~= nil and person ~= false and person.CityState == true;
end

local function UpdateNewGroup()
	local chosen = ChosenTokens();
	Controls.ChosenCount:SetText(DIM .. #chosen .. " + you" .. END);
	local id = (#chosen > 0) and Game.MCChatId(chosen) or nil;
	local existing = id and g_Chats[id];
	local alone = OnlyACityState(chosen);
	Controls.ExistsNote:SetHide(existing == nil and not alone);
	if alone then
		Controls.ExistsText:SetText(GOLD .. "Add another member." .. END .. LIGHT .. " A city-state alone is reached through its own row, by its ally. In a group it talks with everyone in it." .. END);
		Controls.StartLabel:SetText("START GROUP");
	elseif existing then
		local title = ChatTitle(id, chosen);
		Controls.ExistsText:SetText(GOLD .. "This chat already exists." .. END .. LIGHT .. " You and these members already talk in " .. title .. ". The same members always share one chat." .. END);
		Controls.StartLabel:SetText("OPEN " .. string.upper(title));
	else
		Controls.StartLabel:SetText("START GROUP");
	end
	local w = math.max(170, Controls.StartLabel:GetSizeX() + 36);
	Controls.StartGroup:SetSizeX(w);
	Controls.StartBack:SetSizeX(w);
	Controls.StartGroup:SetDisabled(#chosen == 0 or alone);
end

function OpenNewGroup()
	g_Chosen = {};
	Controls.MemberStack:DestroyAllChildren();
	local entries = {};
	for token, person in pairs(g_People) do
		-- a city-state only when an agent plays it: nobody reads chat for the others
		if person.Met and not person.NoChat and (not person.CityState or person.Seat ~= nil) then table.insert(entries, { Token = token, Person = person }); end
	end
	table.sort(entries, function(a, b)
		if a.Person.Minor ~= b.Person.Minor then return not a.Person.Minor; end
		return a.Person.Leader < b.Person.Leader;
	end);
	for _, e in ipairs(entries) do
		local row = Build("MemberInstance", Controls.MemberStack);
		local token = e.Token;
		row.Name:SetText(LabelWithCiv(token));
		row.Sub:SetText(DIM .. (e.Person.Minor and "City-state" or "Civilization") .. END);
		row.Ally:SetHide(not e.Person.Ally);
		Portrait(token, row.Portrait, 45, row.IconBG, row.Icon, row.IconShadow);
		local function show()
			local on = g_Chosen[token] == true;
			row.Selected:SetHide(not on);
			row.CheckOn:SetHide(not on);
			row.Check:SetHide(on);
		end
		show();
		row.Root:RegisterCallback(Mouse.eLClick, function()
			g_Chosen[token] = not g_Chosen[token];
			show();
			UpdateNewGroup();
		end);
	end
	Controls.MemberStack:CalculateSize();
	Controls.MemberScroll:CalculateInternalSize();
	Controls.GroupName:SetText("");
	UpdateNewGroup();
	Controls.NewGroup:SetHide(false);
end

Controls.StartGroup:RegisterCallback(Mouse.eLClick, function()
	local chosen = ChosenTokens();
	if #chosen == 0 or OnlyACityState(chosen) then return; end
	local id = Game.MCChatId(chosen);
	local name = (Controls.GroupName:GetText() or ""):gsub("^%s+", ""):gsub("%s+$", "");
	if g_Chats[id] == nil and name ~= "" then Game.MCName(id, name); end
	Controls.NewGroup:SetHide(true);
	SelectChat(id, chosen);
	RefreshAll();
	Controls.ChatEntry:TakeFocus();
end);
Controls.CancelGroup:RegisterCallback(Mouse.eLClick, function() Controls.NewGroup:SetHide(true); end);
Controls.NewGroupClose:RegisterCallback(Mouse.eLClick, function() Controls.NewGroup:SetHide(true); end);

-- ===== Members pop-out =====

Controls.MembersButton:RegisterCallback(Mouse.eLClick, function()
	if g_Sel == nil then return; end
	Controls.PopStack:DestroyAllChildren();
	local chat = g_Chats[g_Sel.Id];
	Controls.PopTitle:SetText(GOLD .. string.upper(ChatTitle(g_Sel.Id, g_Sel.Members)) .. END);
	local list = { "human" };
	for _, m in ipairs(g_Sel.Members) do table.insert(list, m); end
	for _, m in ipairs(list) do
		local row = Build("PopMemberInstance", Controls.PopStack);
		row.Portrait:SetHide(true);
		Portrait(m, row.Portrait, 45);
		row.Name:SetText(LabelWithCiv(m));
		row.Note:SetText((chat and chat.StartedBy == m) and (DIM .. "started it" .. END) or "");
	end
	Controls.PopStack:CalculateSize();
	local note = "Members are fixed: a different set of players is a different chat.";
	if chat then
		note = string.format("%s messages, last on turn %d. ", chat.Lines, chat.LastTurn) .. note;
	end
	Controls.PopNote:SetText(DIM .. note .. END);
	Controls.MembersPop:SetSizeY(50 + Controls.PopStack:GetSizeY() + 20 + Controls.PopNote:GetSizeY() + 14);
	Controls.MembersPop:SetHide(false);
end);
Controls.PopClose:RegisterCallback(Mouse.eLClick, function() Controls.MembersPop:SetHide(true); end);

-- ===== the leader card: each civilization a leader is, with its abilities and its uniques' icons =====

local KIND_LABEL = { unit = "Unit", building = "Building", improvement = "Improvement" };

function OpenLeaderCard(playerID)
	local parts = LeaderParts(playerID);
	if #parts == 0 then return; end
	Controls.CardStack:DestroyAllChildren();
	Controls.CardTitle:SetText(GOLD .. string.upper(TextOf(parts[1].Leader.Description)) .. END);
	for _, part in ipairs(parts) do
		local c = Build("CardCivInstance", Controls.CardStack);
		c.Portrait:SetHide(true);
		if IconHookup(part.Leader.PortraitIndex, 64, part.Leader.IconAtlas, c.Portrait) then c.Portrait:SetHide(false); end
		c.Name:SetText((part.Second and (DIM .. "SECOND  " .. END) or "") .. LIGHT .. TextOf(part.Leader.Description) .. END .. DIM .. "  " .. TextOf(part.Civ.ShortDescription) .. END);
		local ab = {};
		for _, a in ipairs(part.Abilities) do
			local note = "";
			if not a.Merged then note = DIM .. " (not merged" .. (a.Clash and (": " .. a.Clash) or "") .. ")" .. END;
			elseif a.LeftOut then note = DIM .. " (without " .. a.LeftOut .. ")" .. END; end
			table.insert(ab, (a.Merged and GOLD or DIM) .. TextOf(a.Info.ShortDescription) .. END .. note
				.. "[NEWLINE]" .. (a.Merged and LIGHT or DIM) .. TextOf(a.Info.Description) .. END);
		end
		c.Ability:SetText(table.concat(ab, "[NEWLINE][NEWLINE]"));
		for _, u in ipairs(part.Uniques) do
			local icon = Build("CardUniqueInstance", c.Uniques);
			icon.Icon:SetHide(not IconHookup(u.Info.PortraitIndex, 45, u.Info.IconAtlas, icon.Icon));
			icon.Root:SetAlpha(u.Merged and 1 or 0.35);
			icon.Root:SetToolTipString((KIND_LABEL[u.Kind] or "") .. ": " .. UniqueLine(u) .. (u.Merged and "" or "[NEWLINE]Not merged: the first civilization has its own."));
		end
		c.Uniques:CalculateSize();
		local h = c.Ability:GetSizeY();
		c.Uniques:SetOffsetY(36 + h + 8);
		c.Root:SetSizeY(math.max(72, 36 + h + 8 + ((#part.Uniques > 0) and 50 or 0) + 8));
	end
	Controls.CardStack:CalculateSize();
	local merged = #parts > 1;
	Controls.CardNote:SetHide(merged);
	Controls.CardNote:SetText(DIM .. "Hover an icon for what it is." .. END);
	Controls.LeaderCard:SetSizeY(50 + Controls.CardStack:GetSizeY() + (merged and 0 or (20 + Controls.CardNote:GetSizeY())) + 14);
	Controls.LeaderCard:SetHide(false);
end
Controls.CardClose:RegisterCallback(Mouse.eLClick, function() Controls.LeaderCard:SetHide(true); end);

-- ===== settings: switches other mods offer =====
-- A mod offers a switch with LuaEvents.MCToggleOffer(id, label, tip, on), on load and whenever it is asked
-- (LuaEvents.MCToggleAsk) or switched (LuaEvents.MCToggleSet(id, on)). A row shows what its mod last said.

local g_Toggles, g_ToggleOrder = {}, {};

local function RenderSettings()
	Controls.SettingsStack:DestroyAllChildren();
	for _, id in ipairs(g_ToggleOrder) do
		local t = g_Toggles[id];
		local row = Build("SettingInstance", Controls.SettingsStack);
		row.Name:SetText(LIGHT .. t.Label .. END);
		row.Check:SetHide(t.On);
		row.CheckOn:SetHide(not t.On);
		row.Root:SetToolTipString(t.Tip);
		row.Root:RegisterCallback(Mouse.eLClick, function() LuaEvents.MCToggleSet(id, not t.On); end);
	end
	Controls.SettingsStack:CalculateSize();
	Controls.SettingsNote:SetText(DIM .. ((#g_ToggleOrder == 0) and "Nothing to switch on in this game. A mod that offers a switch lists it here."
		or "Click a row to switch it on or off.") .. END);
	Controls.SettingsPop:SetSizeY(50 + Controls.SettingsStack:GetSizeY() + 20 + Controls.SettingsNote:GetSizeY() + 14);
end

LuaEvents.MCToggleOffer.Add(function(id, label, tip, on)
	if g_Toggles[id] == nil then table.insert(g_ToggleOrder, id); end
	g_Toggles[id] = { Label = label, Tip = tip or "", On = on == true };
	if not Controls.SettingsPop:IsHidden() then RenderSettings(); end
end);

Controls.SettingsButton:RegisterCallback(Mouse.eLClick, function()
	if not Controls.SettingsPop:IsHidden() then Controls.SettingsPop:SetHide(true); return; end
	LuaEvents.MCToggleAsk();
	RenderSettings();
	Controls.SettingsPop:SetHide(false);
end);
Controls.SettingsClose:RegisterCallback(Mouse.eLClick, function() Controls.SettingsPop:SetHide(true); end);

-- ===== open, close =====

local function Open()
	Controls.Window:SetHide(false);
	Layout();
	g_Rendered.Id = nil;
	g_ListKey = nil;
	RefreshAll();
end

local function Close()
	Controls.Window:SetHide(true);
	Controls.NewGroup:SetHide(true);
	Controls.MembersPop:SetHide(true);
	Controls.LeaderCard:SetHide(true);
	Controls.SettingsPop:SetHide(true);
end

Controls.OpenButton:RegisterCallback(Mouse.eLClick, function()
	if Controls.Window:IsHidden() then Open(); else Close(); end
end);
Controls.CloseButton:RegisterCallback(Mouse.eLClick, Close);

ContextPtr:SetInputHandler(function(uiMsg, wParam, lParam)
	if g_Drag ~= nil then
		if uiMsg == MouseEvents.MouseMove then
			local x, y = UIManager:GetMousePos();
			if g_Drag.Kind == "size" then
				g_W = g_Drag.W + (x - g_Drag.X) * 2;
				g_H = g_Drag.H + (y - g_Drag.Y) * 2;
				Layout();
				RenderChat(false);
			else
				Controls.Window:SetOffsetVal(g_Drag.OffX + (x - g_Drag.X), g_Drag.OffY + (y - g_Drag.Y));
			end
			return true;
		elseif uiMsg == MouseEvents.LButtonUp or uiMsg == MouseEvents.LButtonDown then
			EndDrag();
			return true;
		end
	end
	if uiMsg == KeyEvents.KeyDown and wParam == Keys.VK_ESCAPE then
		if not Controls.LeaderCard:IsHidden() then Controls.LeaderCard:SetHide(true); return true; end
		if not Controls.SettingsPop:IsHidden() then Controls.SettingsPop:SetHide(true); return true; end
		if not Controls.NewGroup:IsHidden() then Controls.NewGroup:SetHide(true); return true; end
		if not Controls.MembersPop:IsHidden() then Controls.MembersPop:SetHide(true); return true; end
		if not Controls.Window:IsHidden() then Close(); return true; end
	end
	return false;
end);

local g_Since = 0;
ContextPtr:SetUpdate(function(dt)
	g_Since = g_Since + dt;
	local every = Controls.Window:IsHidden() and 2 or 0.5;
	if g_Since >= every then
		g_Since = 0;
		RefreshAll();
	end
end);

g_W = tonumber(g_Settings.GetValue("W")) or 1080;
g_H = tonumber(g_Settings.GetValue("H")) or 720;
RefreshAll();
