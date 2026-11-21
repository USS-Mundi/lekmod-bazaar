#include "McpFiles.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace
{
#ifdef _WIN32
	std::wstring Wide(const std::string& s)
	{
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
		if(n <= 1) return std::wstring();
		std::wstring w(n - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
		return w;
	}

	std::string Narrow(const wchar_t* w)
	{
		int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
		if(n <= 1) return std::string();
		std::string s(n - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
		return s;
	}
#endif

	FILE* OpenFile(const std::string& path, const char* mode)
	{
#ifdef _WIN32
		return _wfopen(Wide(path).c_str(), Wide(mode).c_str());
#else
		return fopen(path.c_str(), mode);
#endif
	}

	void MakeDir(const std::string& path)
	{
#ifdef _WIN32
		CreateDirectoryW(Wide(path).c_str(), NULL);
#else
		mkdir(path.c_str(), 0755);
#endif
	}
}

const char* McpPathSep()
{
#ifdef _WIN32
	return "\\";
#else
	return "/";
#endif
}

void McpMakeDirs(const std::string& path)
{
	for(size_t i = 1; i <= path.size(); ++i)
	{
		if(i == path.size() || path[i] == '/' || path[i] == '\\') MakeDir(path.substr(0, i));
	}
}

bool McpReadFile(const std::string& path, std::string& out)
{
	FILE* f = OpenFile(path, "rb");
	if(!f) return false;
	out.clear();
	char buf[4096];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

bool McpWriteFile(const std::string& path, const std::string& text, bool append)
{
	FILE* f = OpenFile(path, append ? "ab" : "wb");
	if(!f) return false;
	bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	return fclose(f) == 0 && ok;
}

bool McpFileExists(const std::string& path)
{
	FILE* f = OpenFile(path, "rb");
	if(!f) return false;
	fclose(f);
	return true;
}

std::vector<std::string> McpFilesIn(const std::string& dir, const std::string& suffix)
{
	std::vector<std::string> out;
#ifdef _WIN32
	WIN32_FIND_DATAW data;
	HANDLE h = FindFirstFileW(Wide(dir + "\\*" + suffix).c_str(), &data);
	if(h == INVALID_HANDLE_VALUE) return out;
	do
	{
		std::string n = Narrow(data.cFileName);
		if(n.size() > suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
			out.push_back(n.substr(0, n.size() - suffix.size()));
	} while(FindNextFileW(h, &data));
	FindClose(h);
#else
	DIR* d = opendir(dir.c_str());
	if(!d) return out;
	while(dirent* e = readdir(d))
	{
		std::string n = e->d_name;
		if(n.size() > suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
			out.push_back(n.substr(0, n.size() - suffix.size()));
	}
	closedir(d);
#endif
	return out;
}
