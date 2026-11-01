#pragma once

// What the plugin was doing just before the game died, and a report of how it died: if the game crashes, a report
// (Logs\MCPServer-crash-<time>.txt) and a dump (.dmp) are written under the game's My Games folder. Built without the
// game's precompiled header, as McpMapDraw.cpp is. Safe from any thread.

// Takes over the game's last-chance crash handler, handing on to whoever had it. Again after a load, in case the game
// took it back.
void McpCrashLogInstall();
// One step of the trail the report ends with: the latest 128 are kept.
void McpTrail(const char* what, int a = -1, int b = -1);

// Counts calls that enter the pathfinder hooks while another thread is inside them. Enter and leave pair up per call.
void McpPathHookEnter();
void McpPathHookLeave();
// How many such overlaps there have been, and the most threads inside at once.
long McpPathHookOverlaps();
long McpPathHookMostAtOnce();
