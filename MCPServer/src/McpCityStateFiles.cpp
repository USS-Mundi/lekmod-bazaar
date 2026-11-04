// Built without the game's precompiled header; see McpCityStateFiles.h.
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include "McpCityStateFiles.h"
#pragma comment(lib, "shell32.lib")

namespace
{
	std::wstring Wide(const std::string& utf8)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
		if(n <= 0) return L"";
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
		w.resize(n - 1);
		return w;
	}

	std::string Utf8(const std::wstring& w)
	{
		int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
		if(n <= 0) return "";
		std::string s(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
		s.resize(n - 1);
		return s;
	}

	// The folder, made on the way if it is missing.
	std::wstring Folder(const std::string& gameId, const char* sub)
	{
		wchar_t docs[MAX_PATH];
		if(gameId.empty() || FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs))) return L"";
		std::wstring dir = std::wstring(docs) + L"\\My Games\\Sid Meier's Civilization 5\\MCP";
		CreateDirectoryW(dir.c_str(), NULL);
		dir += L"\\" + Wide(gameId);
		CreateDirectoryW(dir.c_str(), NULL);
		dir += L"\\" + Wide(sub);
		CreateDirectoryW(dir.c_str(), NULL);
		return dir;
	}

	std::wstring PathOf(const std::string& gameId, const std::string& fileName, const char* sub = "city-states")
	{
		std::wstring dir = Folder(gameId, sub);
		return dir.empty() ? dir : dir + L"\\" + Wide(fileName);
	}
}

std::string McpCsFileRead(const std::string& gameId, const std::string& fileName)
{
	std::wstring path = PathOf(gameId, fileName);
	FILE* f = path.empty() ? NULL : _wfopen(path.c_str(), L"rb");
	if(!f) return "";
	std::string content;
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
	fclose(f);
	return content;
}

void McpCsFileAppend(const std::string& gameId, const std::string& fileName, const std::string& headerIfNew, const std::string& line)
{
	std::wstring path = PathOf(gameId, fileName);
	if(path.empty()) return;
	bool bNew = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
	FILE* f = _wfopen(path.c_str(), L"ab");
	if(!f) return;
	if(bNew) fputs(headerIfNew.c_str(), f);
	fputs(line.c_str(), f);
	fclose(f);
}

std::string McpCsFilePath(const std::string& gameId, const std::string& fileName)
{
	std::wstring path = PathOf(gameId, fileName);
	if(path.empty()) return "";
	typedef char* (__cdecl *UnixName)(const wchar_t*);
	UnixName unixName = (UnixName)GetProcAddress(GetModuleHandleA("kernel32.dll"), "wine_get_unix_file_name");
	if(unixName)
	{
		char* u = unixName(path.c_str());
		if(u)
		{
			std::string s(u);
			HeapFree(GetProcessHeap(), 0, u);
			return s;
		}
	}
	return Utf8(path);
}

std::string McpGameFileRead(const std::string& gameId, const char* sub, const std::string& fileName)
{
	std::wstring path = PathOf(gameId, fileName, sub);
	FILE* f = path.empty() ? NULL : _wfopen(path.c_str(), L"rb");
	if(!f) return "";
	std::string content;
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
	fclose(f);
	return content;
}

void McpGameFileAppend(const std::string& gameId, const char* sub, const std::string& fileName, const std::string& headerIfNew, const std::string& line)
{
	std::wstring path = PathOf(gameId, fileName, sub);
	if(path.empty()) return;
	bool bNew = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
	FILE* f = _wfopen(path.c_str(), L"ab");
	if(!f) return;
	if(bNew) fputs(headerIfNew.c_str(), f);
	fputs(line.c_str(), f);
	fclose(f);
}

bool McpCityStatesOffAtStart()
{
	wchar_t docs[MAX_PATH];
	if(FAILED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs))) return false;
	std::wstring path = std::wstring(docs) + L"\\My Games\\Sid Meier's Civilization 5\\MCPServer-city-states-off";
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}
