// Files by UTF-8 path on Windows (wide APIs, so any user name and any Documents folder work) and natively in tests.
// No game headers.
#pragma once

#include <string>
#include <vector>

const char* McpPathSep();
// Every folder along the path, made when missing.
void McpMakeDirs(const std::string& path);
bool McpReadFile(const std::string& path, std::string& out);
bool McpWriteFile(const std::string& path, const std::string& text, bool append);
bool McpFileExists(const std::string& path);
// Names of the files in dir ending in suffix, without it.
std::vector<std::string> McpFilesIn(const std::string& dir, const std::string& suffix);
// Names of the folders in dir that start with prefix.
std::vector<std::string> McpDirsIn(const std::string& dir, const std::string& prefix);
bool McpRemoveFile(const std::string& path);
