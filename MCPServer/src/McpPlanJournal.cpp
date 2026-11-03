// Built without the game's precompiled header; see McpPlanJournal.h.
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include "McpPlanJournal.h"
#pragma comment(lib, "shell32.lib")

namespace
{
	std::string LogsFile(const char* name)
	{
		char docs[MAX_PATH];
		if(FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs))) return "";
		return std::string(docs) + "\\My Games\\Sid Meier's Civilization 5\\Logs\\" + name;
	}

	struct Lock
	{
		CRITICAL_SECTION cs;
		Lock() { InitializeCriticalSection(&cs); }
		~Lock() { DeleteCriticalSection(&cs); }
	};
	Lock s_lock;
}

bool McpPlanJournalOn()
{
	std::string flag = LogsFile("MCPServer-plan-journal.on");
	return !flag.empty() && GetFileAttributesA(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void McpPlanJournalAppend(const std::string& jsonLine)
{
	std::string path = LogsFile("MCPServer-plan-journal.jsonl");
	if(path.empty()) return;
	EnterCriticalSection(&s_lock.cs);
	FILE* f = fopen(path.c_str(), "ab");
	if(f)
	{
		fputs(jsonLine.c_str(), f);
		fputs("\n", f);
		fclose(f);
	}
	LeaveCriticalSection(&s_lock.cs);
}
