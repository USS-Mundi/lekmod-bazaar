#pragma once

// The game's own record of the whole game, for reports: every player's per-turn figures (the replay data the game
// gathers each turn), its event log (cities founded, taken and razed, wonders, wars and peace, pacts, religions) and
// the world wonders standing. The history tool answers from it; and once each game turn the whole record is written to
// Logs\MCPServer-history-<game>.json, so a report can be made with the game closed.

void McpHistoryTick();
