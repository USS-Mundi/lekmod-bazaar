// Built without the game's precompiled header; see McpCrashLog.h.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 // GetModuleHandleEx
#endif
#include <windows.h>
#include <shlobj.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "McpCrashLog.h"
#pragma comment(lib, "shell32.lib")

namespace
{
	struct Step
	{
		DWORD tick;
		DWORD thread;
		char what[40];
		int a;
		int b;
	};

	const LONG STEPS = 128;
	Step s_steps[STEPS];
	volatile LONG s_next = 0;
	volatile LONG s_crashed = 0;
	LPTOP_LEVEL_EXCEPTION_FILTER s_previous = NULL;

	enum HeapWatch { HEAP_NOT_WATCHED, HEAP_WATCHED, HEAP_DAMAGED, HEAP_TOO_SLOW };
	HANDLE s_heap = NULL;
	volatile LONG s_heapWatchStarted = 0;
	volatile LONG s_heapState = HEAP_NOT_WATCHED;
	volatile DWORD s_heapChecks = 0;
	volatile DWORD s_heapSlowestMs = 0;
	volatile DWORD s_heapWholeTick = 0;
	volatile DWORD s_heapDamagedTick = 0;

	volatile LONG s_threadsInside = 0;
	volatile LONG s_overlaps = 0;
	volatile LONG s_mostAtOnce = 0;
	DWORD s_depthSlot = TLS_OUT_OF_INDEXES;

	typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
		PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

	std::string LogsFolder()
	{
		char docs[MAX_PATH];
		if(FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs))) return "";
		return std::string(docs) + "\\My Games\\Sid Meier's Civilization 5\\Logs\\";
	}

	std::string ModuleAt(void* address, DWORD& offset)
	{
		HMODULE module = NULL;
		offset = 0;
		if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module) || !module)
			return "(no module)";
		char name[MAX_PATH] = "";
		GetModuleFileNameA(module, name, MAX_PATH);
		offset = (DWORD)((char*)address - (char*)module);
		const char* base = strrchr(name, '\\');
		return base ? base + 1 : name;
	}

	std::string Stamp()
	{
		SYSTEMTIME t;
		GetLocalTime(&t);
		char when[32];
		_snprintf_s(when, sizeof(when), _TRUNCATE, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		return when;
	}

	void WriteHeapLine(FILE* f, DWORD now)
	{
		switch(s_heapState)
		{
		case HEAP_WATCHED:
			fprintf(f, "The game's UI object heap was whole at its last check, %lu ms before this (%lu checks, slowest %lu ms).\n",
				now - s_heapWholeTick, s_heapChecks, s_heapSlowestMs);
			break;
		case HEAP_DAMAGED:
			fprintf(f, "The game's UI object heap was found damaged %lu ms before this; it was last whole %lu ms before (%lu checks, slowest %lu ms).\n",
				now - s_heapDamagedTick, s_heapWholeTick ? now - s_heapWholeTick : 0, s_heapChecks, s_heapSlowestMs);
			break;
		case HEAP_TOO_SLOW:
			fprintf(f, "The game's UI object heap stopped being watched: one check took %lu ms.\n", s_heapSlowestMs);
			break;
		default:
			fprintf(f, "The game's UI object heap was not being watched.\n");
		}
	}

	void WriteTrail(FILE* f, DWORD now)
	{
		fprintf(f, "\nThe plugin's last steps, oldest first (ms before, thread, step, values):\n");
		LONG last = s_next;
		for(LONG i = (last > STEPS ? last - STEPS : 0); i < last; ++i)
		{
			const Step& s = s_steps[i % STEPS];
			fprintf(f, "  -%6lu  thread %5lu  %-40s %d %d\n", now - s.tick, s.thread, s.what, s.a, s.b);
		}
	}

	void WriteReport(EXCEPTION_POINTERS* ep, const std::string& stem)
	{
		FILE* f = fopen((stem + ".txt").c_str(), "w");
		if(!f) return;
		EXCEPTION_RECORD* r = ep ? ep->ExceptionRecord : NULL;
		DWORD offset = 0;
		std::string module = r ? ModuleAt(r->ExceptionAddress, offset) : "";
		fprintf(f, "The game died with exception 0x%08lX at %p, in %s at +0x%lX, on thread %lu.\n",
			r ? r->ExceptionCode : 0, r ? r->ExceptionAddress : NULL, module.c_str(), offset, GetCurrentThreadId());
		if(r && r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
			fprintf(f, "It tried to %s address %p.\n", r->ExceptionInformation[0] == 1 ? "write" : (r->ExceptionInformation[0] == 8 ? "run code at" : "read"),
				(void*)r->ExceptionInformation[1]);
#if defined(_M_IX86)
		if(ep && ep->ContextRecord)
			fprintf(f, "eip %08lX esp %08lX ebp %08lX eax %08lX ecx %08lX edx %08lX\n", ep->ContextRecord->Eip, ep->ContextRecord->Esp,
				ep->ContextRecord->Ebp, ep->ContextRecord->Eax, ep->ContextRecord->Ecx, ep->ContextRecord->Edx);
#endif
		fprintf(f, "Pathfinder hooks: %ld calls entered while another thread was inside; at most %ld threads at once.\n",
			(long)s_overlaps, (long)s_mostAtOnce);
		DWORD now = GetTickCount();
		WriteHeapLine(f, now);
		WriteTrail(f, now);
		fclose(f);
	}

	void WriteDump(EXCEPTION_POINTERS* ep, const std::string& stem)
	{
		HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
		MiniDumpWriteDumpFn write = dbghelp ? (MiniDumpWriteDumpFn)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
		if(!write) return;
		HANDLE file = CreateFileA((stem + ".dmp").c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if(file == INVALID_HANDLE_VALUE) return;
		MINIDUMP_EXCEPTION_INFORMATION info;
		info.ThreadId = GetCurrentThreadId();
		info.ExceptionPointers = ep;
		info.ClientPointers = FALSE;
		write(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithThreadInfo, ep ? &info : NULL, NULL, NULL);
		CloseHandle(file);
	}

	LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep)
	{
		if(InterlockedExchange(&s_crashed, 1) == 0)
		{
			std::string stem = LogsFolder() + "MCPServer-crash-" + Stamp();
			WriteReport(ep, stem);
			WriteDump(ep, stem);
		}
		return s_previous ? s_previous(ep) : EXCEPTION_CONTINUE_SEARCH;
	}

	// A damaged heap can send the walk through a bad pointer: that counts as damage, not a crash.
	bool HeapWhole(HANDLE heap)
	{
		__try { return HeapValidate(heap, 0, NULL) != FALSE; }
		__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	DWORD WINAPI WatchHeap(LPVOID)
	{
		for(;;)
		{
			if(s_crashed) return 0;
			DWORD start = GetTickCount();
			bool whole = HeapWhole(s_heap);
			DWORD now = GetTickCount();
			DWORD took = now - start;
			++s_heapChecks;
			if(took > s_heapSlowestMs) s_heapSlowestMs = took;
			if(!whole)
			{
				s_heapDamagedTick = now;
				s_heapState = HEAP_DAMAGED;
				if(s_crashed) return 0;
				FILE* f = fopen((LogsFolder() + "MCPServer-heap-damaged-" + Stamp() + ".txt").c_str(), "w");
				if(!f) return 0;
				fprintf(f, "No crash yet.\n");
				WriteHeapLine(f, now);
				WriteTrail(f, now);
				fclose(f);
				return 0;
			}
			s_heapWholeTick = now;
			unsigned long wait = McpHeapCheckEveryMs(took);
			if(!wait)
			{
				s_heapState = HEAP_TOO_SLOW;
				return 0;
			}
			Sleep(wait);
		}
	}
}

void McpHeapWatchStart(void* heap)
{
	if(!heap || InterlockedCompareExchange(&s_heapWatchStarted, 1, 0) != 0) return;
	s_heap = (HANDLE)heap;
	s_heapState = HEAP_WATCHED;
	HANDLE thread = CreateThread(NULL, 0, &WatchHeap, NULL, 0, NULL);
	if(thread) CloseHandle(thread);
	else s_heapState = HEAP_NOT_WATCHED;
}

void McpCrashLogInstall()
{
	LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(&OnCrash);
	if(previous != &OnCrash) s_previous = previous;
	if(s_depthSlot == TLS_OUT_OF_INDEXES) s_depthSlot = TlsAlloc();
}

void McpTrail(const char* what, int a, int b)
{
	LONG i = InterlockedIncrement(&s_next) - 1;
	Step& s = s_steps[i % STEPS];
	s.tick = GetTickCount();
	s.thread = GetCurrentThreadId();
	strncpy_s(s.what, sizeof(s.what), what ? what : "", _TRUNCATE);
	s.a = a;
	s.b = b;
}

void McpPathHookEnter()
{
	if(s_depthSlot == TLS_OUT_OF_INDEXES) return;
	LONG_PTR depth = (LONG_PTR)TlsGetValue(s_depthSlot);
	if(depth == 0)
	{
		LONG inside = InterlockedIncrement(&s_threadsInside);
		if(inside > 1)
		{
			InterlockedIncrement(&s_overlaps);
			LONG most = s_mostAtOnce;
			while(inside > most && InterlockedCompareExchange(&s_mostAtOnce, inside, most) != most) most = s_mostAtOnce;
		}
	}
	TlsSetValue(s_depthSlot, (LPVOID)(depth + 1));
}

void McpPathHookLeave()
{
	if(s_depthSlot == TLS_OUT_OF_INDEXES) return;
	LONG_PTR depth = (LONG_PTR)TlsGetValue(s_depthSlot) - 1;
	TlsSetValue(s_depthSlot, (LPVOID)depth);
	if(depth == 0) InterlockedDecrement(&s_threadsInside);
}

long McpPathHookOverlaps() { return s_overlaps; }
long McpPathHookMostAtOnce() { return s_mostAtOnce; }
