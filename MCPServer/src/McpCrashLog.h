#pragma once

// What the plugin was doing just before the game died, and a report of how it died: if the game crashes, a report
// (Logs\MCPServer-crash-<time>.txt) and a dump (.dmp) are written under the game's My Games folder. Built without the
// game's precompiled header, as McpMapDraw.cpp is. Safe from any thread.

// Takes over the game's last-chance crash handler, handing on to whoever had it. Again after a load, in case the game
// took it back.
void McpCrashLogInstall();
// One step of the trail the report ends with: the latest 128 are kept.
void McpTrail(const char* what, int a = -1, int b = -1);

// Watches the heap the game keeps the objects it hands the UI in: a background thread checks it is whole, and the
// report says when it was last found whole or first found damaged. Damage found before any crash writes
// Logs\MCPServer-heap-damaged-<time>.txt once, with the trail, and the watch ends there. Starting again does nothing.
void McpHeapWatchStart(void* heap);
// Ends the watch and waits for its thread, so the plugin can be unloaded and loaded again (the game reloads plugins
// between setting up a game and playing it). The next McpHeapWatchStart starts it again.
void McpHeapWatchStop();
// How long to wait before the next check, given how long the last took: about 5% of the time at most, never more
// often than every half second. 0 when one check took too long to keep watching.
inline unsigned long McpHeapCheckEveryMs(unsigned long lastCheckMs)
{
	if(lastCheckMs > 200) return 0;
	return lastCheckMs * 20 > 500 ? lastCheckMs * 20 : 500;
}

// Counts calls that enter the pathfinder hooks while another thread is inside them. Enter and leave pair up per call.
void McpPathHookEnter();
void McpPathHookLeave();
// How many such overlaps there have been, and the most threads inside at once.
long McpPathHookOverlaps();
long McpPathHookMostAtOnce();
