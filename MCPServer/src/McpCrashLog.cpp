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
		fprintf(f, "\nThe plugin's last steps, oldest first (ms before the crash, thread, step, values):\n");
		DWORD now = GetTickCount();
		LONG last = s_next;
		for(LONG i = (last > STEPS ? last - STEPS : 0); i < last; ++i)
		{
			const Step& s = s_steps[i % STEPS];
			fprintf(f, "  -%6lu  thread %5lu  %-40s %d %d\n", now - s.tick, s.thread, s.what, s.a, s.b);
		}
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
			SYSTEMTIME t;
			GetLocalTime(&t);
			char when[32];
			_snprintf_s(when, sizeof(when), _TRUNCATE, "%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
			std::string stem = LogsFolder() + "MCPServer-crash-" + when;
			WriteReport(ep, stem);
			WriteDump(ep, stem);
		}
		return s_previous ? s_previous(ep) : EXCEPTION_CONTINUE_SEARCH;
	}
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
