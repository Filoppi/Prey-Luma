Prey (2017) Luma is mod that rewrites the whole late rendering phase of CryEngine/Prey.
It's made by two relatively separate parts:
The first, by Ersh, has the purpose of hooking into the game's native code and (e.g.) swapping texture formats (e.g. from 8bit to 16bit etc).
The second, by Pumbo, leverages the ReShade Addon system to add or modify rendering passes (and replace post processing shaders) through DirectX hooks.
It'd be possible to achieve the same without ReShade and game specific code hooks, by only using generic DirectX hooks, but it'd be exponentially more complicated (CryEngine re-uses render target textures for different purposes, so we couldn't easily tell which ones to upgrade, and ReShade offers settings serialization and a bunch of other features).
The quality of the code is not that great (the code itself should be good, but it's almost all dumped in a single file), the focus has been on getting things to work.

# Development requirements
Windows 11 (Windows 10 probably works fine too)
Visual Studio 2022 (older versions might work too)
Windows 11 SDK 10.0.26100.0 (older versions work, but don't support HDR as good)

# Instructions
- Run "setup.ps1" to add the game installation environment variables.
- Set "VCPKG_ROOT" environment variable to your vcpkg installation folder if it wasn't already (download it from here "https://github.com/microsoft/vcpkg", the version integrated with Visual Studio doesn't seem to be as reliable).
- Open "Prey-Luma.sln" and build it. Note that "Edit and Continue" build settings (\ZI) should not be used as they break the code patches generation.
- Run "deploy (*).bat" to run the game. The Steam version of the game can't be launched from the exe (without a modified steam dll), so that bat automatically closes the previous instance of the game, copies the new files and launches the game through Steam.
- The "Data" folder needs to be manually copied into the directory of the game at least once. For development of shaders, it's suggested to make a symbolic link of the "Prey-Luma" folder (to allow git to pick up the changes while also having the latest version in game).
- If you want to load the mod with an asi loader instead of through ReShade Addons automatic loading, you can rename the dll to ".asi" name, add the asi loader and use one of the following names: bink2w64.dll, dinput8.dll, version.dll, winhttp.dll, winmm.dll (untested), wininet.dll.

# Further development details
- The code hot spots are in main.cpp file.
- There's a "DEVELOPMENT" and "TEST" flag (defines) in main.cpp. They automatically spread to shaders on the next load/compile. Building in Debug (as opposed to Release), simply adds debug symbols etc, but no additional development features.
- The mod version is stored in "Globals::VERSION" and can be increased there.
- Vcpkg package dependencies are forced to the version I tested the mod on, upgrading is possible but there seem to be no issues.
- There's some warnings in the DKUtil code, we haven't fixed them as they seem harmless.
- The mod also comes with some replaced game files. These are packaged in ".pak" files in CryEngine, and they are simple zips (they can be extracted and re-compressed as zip).

# Shaders development
- The mod automatically dumps the game's shaders in development mode.
- Luma shaders can be found in ".\Data\Binaries\Danielle\x64\Release\Prey-Luma\".
- Shader are saved and replaced by (cso/binary) hash.
- VSCode is suggested.
- The game's original shaders code can be found in the Engine\Shaders.pak in the GOG version of the game (extract it as zip).
- To decompile further game shaders you will need 3DMigoto (see RenoDX).
- Running a graphics capture debugger requires ReShade to be off. NV Nsight and Intel "Graphics Frame Analyzer" work. Microsoft Pix and RenderDoc might also work but are untested. The GOG version is the easiest to debug graphics for as it can be launched directly from its exe (differently from the Steam version).