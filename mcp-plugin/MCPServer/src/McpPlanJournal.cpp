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

bool McpLogsFlag(const char* name)
{
	std::string flag = LogsFile(name);
	return !flag.empty() && GetFileAttributesA(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool McpPlanJournalOn()
{
	return McpLogsFlag("MCPServer-plan-journal.on");
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

std::string McpLogsWrite(const std::string& name, const std::string& content)
{
	std::string path = LogsFile(name.c_str());
	if(path.empty()) return "";
	std::string part = path + ".part";
	EnterCriticalSection(&s_lock.cs);
	bool ok = false;
	FILE* f = fopen(part.c_str(), "wb");
	if(f)
	{
		ok = fwrite(content.data(), 1, content.size(), f) == content.size();
		ok = (fclose(f) == 0) && ok;
		ok = ok && MoveFileExA(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
		if(!ok) DeleteFileA(part.c_str());
	}
	LeaveCriticalSection(&s_lock.cs);
	return ok ? path : "";
}
