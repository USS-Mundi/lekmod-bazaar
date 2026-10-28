# Toolchain

Everything here is Windows software. Under CrossOver it goes into the same
bottle Civilization V runs in, not a new one.

A plugin has to be built with the same compiler and settings as Bazaar. The two
pass game objects, strings and lists between them, and a DLL built any other way
lays them out differently and crashes the game. That is why the version below is
exact rather than a suggestion.

## Install in this order

**1. Visual C++ 2008 Express Edition with SP1**

Microsoft no longer offers it. The Internet Archive keeps the disc image:
<https://archive.org/details/vs-2008-express-w-sp-1>

Mount the image on the host and run `VCExpress/setup.exe` from it. The disc also
carries Visual Basic, C# and Web Developer; none of them are needed. Decline
every optional extra the installer offers. It brings the Windows SDK headers and
libraries with it.

**2. Sid Meier's Civilization V SDK**

From Steam, under Tools, in the same bottle. It supplies ModBuddy, which packages
a mod and uploads it to the Workshop. A build calls ModBuddy's build steps; you
only open ModBuddy yourself to upload.

**3. Microsoft Visual Studio 2010 Shell (Isolated) Redistributable**

ModBuddy will not start without it. Also withdrawn, also archived:
<https://web.archive.org/web/20201112013416/http://download.microsoft.com/download/1/9/3/1939AD78-F8E8-4336-83F3-E2470F422C62/VSIsoShell.exe>

**4. Start ModBuddy once**

From the Civilization V SDK's launcher, before the first build. Its updater
copies the build steps into `C:\Program Files (x86)\MSBuild\Firaxis\ModBuddy`.

**5. The Bazaar SDK**

`Bazaar-SDK-<version>.zip` from the Bazaar releases page. Unzip it somewhere with
a short path and point the `BAZAAR_SDK` environment variable at the folder.

## Checking it worked

Run `build.cmd` in the SDK's `plugin-template` folder. It should produce
`TemplatePlugin.dll` and a mod folder beside it. Copy that folder into
`Documents\My Games\Sid Meier's Civilization 5\MODS`, turn it on with Bazaar and
the Bootstrap, start a game, and look in
`Documents\My Games\Sid Meier's Civilization 5\Logs\Bazaar.log` for a line
beginning `Plugin Template Plugin 1 loaded`.

The SDK guide lists each build error and its cause. Two bite early: a path longer
than 260 characters fails at the link step, and a missing `Civ5Mod.targets` means
ModBuddy has not been started yet.
