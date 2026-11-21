// The game's own folder for a player. Built without the game's precompiled header, whose headers clash with shlobj.h.
#include <windows.h>
#include <shlobj.h>
#include <string>

// My Games\Sid Meier's Civilization 5, or "" when Windows does not say where the documents folder is.
std::string SlMyGamesFolder()
{
	char docs[MAX_PATH];
	if(FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docs))) return std::string();
	return std::string(docs) + "\\My Games\\Sid Meier's Civilization 5";
}
