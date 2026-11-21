-- MCP Server for LEKMOD UI add-in, with no window of its own: popup capture, leader screens left for a model, the
-- mod list, the turn banner for seats, and tile coordinates. The Message Center (MessageCenter.lua) is the window.
-- Every game popup about what happened in the game is reported to the DLL (Game.LLMNotePopup). It stays open
-- until the model acknowledges it; the DLL then names the dialog in Game.LLMDialogsToClose.
include("IconSupport");

-- Bazaar can run this mod while its plugin is not loaded (refused, failed or skipped).
if Game.LLMNotePopup == nil then
	print("MCPServerUI: the MCP server plugin is not running");
	return;
end

local g_PendingDismiss = {};
local g_Stopped = false;

local function GoodyText(popupInfo)
	local goody = GameInfo.GoodyHuts[popupInfo.Data1];
	if goody == nil then
		return "";
	end
	local text = Locale.ConvertTextKey(goody.Description);
	if goody.Gold > 0 or goody.Culture > 0 then
		text = Locale.ConvertTextKey(goody.Description, popupInfo.Data2);
	end
	return text;
end

local function OnGamePopup(popupInfo)
	if g_Stopped then
		return;
	end
	-- The game's screens open pop-ups at this priority when the player clicks something to look at (a Great
	-- Work, an advisor's answer); pop-ups for what happened in the game never come at it.
	if popupInfo.Priority == PopupPriority.Current then
		return;
	end
	local text = popupInfo.Text or "";
	if popupInfo.Type == ButtonPopupTypes.BUTTONPOPUP_GOODY_HUT_REWARD then
		text = GoodyText(popupInfo);
	elseif popupInfo.Type == ButtonPopupTypes.BUTTONPOPUP_CITY_STATE_MESSAGE then
		local message = UI.GetTempString();
		if message ~= nil and message ~= "" then
			text = message;
		end
	end
	Game.LLMNotePopup(popupInfo.Type, popupInfo.Data1 or -1, popupInfo.Data2 or -1, popupInfo.Data3 or -1, popupInfo.Option1 == true, text);
end
Events.SerialEventGameMessagePopup.Add(OnGamePopup);

-- A leader screen holds the game until someone leaves it, which a model playing the civilization on screen
-- cannot do: the plugin passes the leader's words to the model, and the screen is left here, the way its
-- own Back button does, once it shows. The game hides this window's update while a leader screen is up,
-- so the watch runs on the application's frames. A conversation the human opens, by clicking a leader or a
-- spy notification, stays theirs until the leader view closes or the next turn starts; the AI answers a
-- model's discuss with DISCUSS_HUMAN_INVOKED too, so that state does not mark the human.
local LEADER_SCREENS = { "/LeaderHeadRoot/DiscussionDialog", "/LeaderHeadRoot" };
local HUMAN_OPENED = {
	[DiploUIStateTypes.DIPLO_UI_STATE_DEFAULT_ROOT] = true,
	[DiploUIStateTypes.DIPLO_UI_STATE_CONFRONT_YOU_KILLED_MY_SPY] = true,
};
local LEADER_WATCH_SECONDS = 5;
local g_LeaderWatch = 0;
local g_HumanTalking = false;

local function OnLeaderMessage(iPlayer, iDiploUIState, szLeaderMessage)
	if g_Stopped then
		return;
	end
	if HUMAN_OPENED[iDiploUIState] then
		g_HumanTalking = true;
	end
	if g_HumanTalking then
		g_LeaderWatch = 0;
		return;
	end
	if Game.LLMTakeLeaderMessage(iPlayer, iDiploUIState, szLeaderMessage or "") then
		g_LeaderWatch = LEADER_WATCH_SECONDS;
	end
end
Events.AILeaderMessage.Add(OnLeaderMessage);

-- Some screens the human opens never enter the leader view, so the next turn start ends their conversation too.
local function OnHumanDoneTalking()
	g_HumanTalking = false;
end
Events.LeavingLeaderViewMode.Add(OnHumanDoneTalking);
Events.ActivePlayerTurnStart.Add(OnHumanDoneTalking);

local function LeaveLeaderScreens(tickCount, timeIncrement)
	if g_LeaderWatch <= 0 then
		return;
	end
	if g_Stopped or g_HumanTalking then
		g_LeaderWatch = 0;
		return;
	end
	g_LeaderWatch = g_LeaderWatch - (timeIncrement or 0);
	local left = false;
	for _, path in ipairs(LEADER_SCREENS) do
		local control = ContextPtr:LookUpControl(path);
		if control ~= nil and not control:IsHidden() then
			UIManager:DequeuePopup(control);
			left = true;
		end
	end
	if left then
		UI.SetLeaderHeadRootUp(false);
		UI.RequestLeaveLeader();
		-- tests/ingame/run.py looks for this line in Lua.log.
		print("MCPServerUI: left the leader screen for the model playing this civilization");
	end
end
Events.LocalMachineAppUpdate.Add(LeaveLeaderScreens);

local function CollectAcknowledged()
	local names = Game.LLMDialogsToClose();
	if names == nil or names == "" then
		return;
	end
	for name in string.gmatch(names, "[^\n]+") do
		if #g_PendingDismiss >= 50 then
			print("MCPServerUI: too many dialogs waiting to be closed; dropping " .. g_PendingDismiss[1].name);
			table.remove(g_PendingDismiss, 1);
		end
		table.insert(g_PendingDismiss, { name = name, waited = 0 });
	end
end

-- An acknowledged dialog may still be waiting behind other dialogs, so each entry
-- stays until its dialog has actually been shown and closed. If the human closed
-- it by hand it never shows again; the entry is dropped so it cannot close the
-- next dialog of that kind unread. A dialog stuck behind others for that long is
-- left for the human to close, which is the safer failure.
local MAX_DISMISS_WAIT_SECONDS = 10;

local function DismissPending(fDeltaTime)
	local remaining = {};
	local closed = {};
	for _, pending in ipairs(g_PendingDismiss) do
		local control = ContextPtr:LookUpControl("/InGame/" .. pending.name);
		if control == nil then
			print("MCPServerUI: no dialog named " .. pending.name);
		elseif not control:IsHidden() and not closed[pending.name] then
			UIManager:DequeuePopup(control);
			closed[pending.name] = true;
		else
			pending.waited = pending.waited + fDeltaTime;
			if pending.waited > MAX_DISMISS_WAIT_SECONDS then
				print("MCPServerUI: " .. pending.name .. " was not shown within " .. MAX_DISMISS_WAIT_SECONDS .. " s; dropping it");
			else
				table.insert(remaining, pending);
			end
		end
	end
	g_PendingDismiss = remaining;
end

-- One line per mod: "Name (v N)", a tab, then the description flattened to one line.
local function ReportActiveMods()
	local mods = {};
	for _, v in ipairs(Modding.GetActivatedMods()) do
		local name = Modding.GetModProperty(v.ID, v.Version, "Name") or v.ID;
		local description = Modding.GetModProperty(v.ID, v.Version, "Description") or "";
		description = description:gsub("%s+", " ");
		table.insert(mods, string.format("%s (v %d)\t%s", (name:gsub("%s+", " ")), v.Version, description));
	end
	Game.LLMSetActiveMods(table.concat(mods, "\n"));
end
ReportActiveMods();

-- The game's "Processing turn for ..." banner (TurnProcessing.lua) skips human players, and a
-- seat is a human slot, so the banner stays blank during a seat's turn. This fills it the way
-- the game does for an AI civilization; the game hides it again when the human's turn starts.
local TURN_BANNER = "/InGame/WorldView/TurnProcessing";

local function BannerControl(id)
	return ContextPtr:LookUpControl(TURN_BANNER .. "/" .. id);
end

local function ShowSeatTurnBanner(iPlayerID)
	if PreGame.IsMultiplayerGame() then
		return;
	end
	local player = Players[iPlayerID];
	if player == nil or not player:IsHuman() or iPlayerID == Game.GetActivePlayer() or not player:IsTurnActive() then
		return;
	end
	local banner = ContextPtr:LookUpControl(TURN_BANNER);
	local anim = BannerControl("Anim");
	local title = BannerControl("TurnProcessingTitle") or BannerControl("Anim/TurnProcessingGrid/TurnProcessingTitle");
	local icon, iconBG, iconShadow = BannerControl("CivIcon"), BannerControl("CivIconBG"), BannerControl("CivIconShadow");
	if banner == nil or anim == nil or title == nil then
		print("MCPServerUI: the turn banner was not found at " .. TURN_BANNER);
		return;
	end
	banner:SetHide(false);
	local bMet = Teams[Players[Game.GetActivePlayer()]:GetTeam()]:IsHasMet(player:GetTeam());
	if bMet then
		title:SetText(Locale.ConvertTextKey("TXT_KEY_PROCESSING_TURN_FOR", Locale.ConvertTextKey(GameInfo.Civilizations[player:GetCivilizationType()].ShortDescription)));
	else
		title:SetText(Locale.ConvertTextKey("TXT_KEY_PROCESSING_TURN_FOR_UNMET_PLAYER", iPlayerID + 1));
	end
	if icon ~= nil and iconBG ~= nil and iconShadow ~= nil then
		if bMet then
			iconBG:SetHide(false);
			iconShadow:SetHide(false);
			CivIconHookup(iPlayerID, 64, icon, iconBG, iconShadow, false, true);
		else
			icon:SetTexture("CivSymbolsColor512.dds");
			icon:SetTextureOffsetVal(448 + 7, 128 + 7);
			icon:SetColor(Vector4(1.0, 1.0, 1.0, 1.0));
			icon:SetHide(false);
			iconBG:SetHide(true);
			iconShadow:SetHide(true);
		end
	end
	if anim:IsHidden() then
		anim:SetHide(false);
		anim:BranchResetAnimation();
	end
end
Events.AIProcessingStartedForPlayer.Add(ShowSeatTurnBanner);

local function Update(fDeltaTime)
	CollectAcknowledged();
	if #g_PendingDismiss > 0 then
		DismissPending(fDeltaTime);
	end
end

-- After a crash Bazaar turns the plugin off and every call to it raises: the first error then stops this add-in for
-- good. Any other error reaches Lua.log once, not every frame.
local g_LastError = nil;
ContextPtr:SetUpdate(function(fDeltaTime)
	local ok, err = pcall(Update, fDeltaTime);
	if ok then
		return;
	end
	if not pcall(Game.LLMDialogsToClose) then
		g_Stopped = true;
		ContextPtr:ClearUpdate();
	elseif err == g_LastError then
		return;
	end
	g_LastError = err;
	error(err, 0);
end);

-- Tile coordinates in the game's tile box above the minimap, so the human can name a tile to
-- the AI. Relies on the game filling the box on the same event first; the game's debug mode
-- already prints coordinates there.
local function PlotHelpControl(id)
	return ContextPtr:LookUpControl("/InGame/WorldView/PlotHelpText/" .. id);
end

local g_PlotHelpMissingReported = false;

local function AddTileCoordinates(hexX, hexY)
	local plot = Map.GetPlot(hexX, hexY);
	if plot == nil or OptionsManager:IsDebugMode() then
		return;
	end
	local box, label = PlotHelpControl("TextBox"), PlotHelpControl("Text");
	if box == nil or label == nil then
		if not g_PlotHelpMissingReported then
			print("MCPServerUI: the tile info box was not found; no coordinates will be shown");
			g_PlotHelpMissingReported = true;
		end
		return;
	end
	local coords = "(" .. plot:GetX() .. ", " .. plot:GetY() .. ")";
	local text = box:IsHidden() and "" or label:GetText();
	if string.sub(text, -string.len(coords)) == coords then
		return;
	end
	if text == "" then
		label:SetText(coords);
	else
		label:SetText(text .. "[NEWLINE]" .. coords);
	end
	box:SetHide(false);
	box:DoAutoSize();
	box:ReprocessAnchoring();
end
Events.SerialEventMouseOverHex.Add(AddTileCoordinates);
