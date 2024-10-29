Prey (2017) Luma is mod that is internally made of a ReShade addon (from Pumbo), and a "native" plugin (dll/asi) (from Ersh).
It's rewriting the whole post processing phase (and potentially rendering...) of CryEngine/Prey.
The native plugin has the purpose of hooking into the engine code and swapping texture formats (e.g. from 8bit to 16bit etc), changing the swapchain to be scRGB HDR and making it use a more modern and responsive flip model. It also makes some modifications to TAA to change the rendering jitters for improved DLSS compatibility. Doing this stuff exclusively through DirectX hooks would probably be possible but exponentially more complicated (mostly because CryEngine re-uses textures for different purposes, and we'd only want to upgrade some of them while not other).
The ReShade addon has the purpose of adding behaviour by hooking to their DirectX functions, these include: replacing shaders (with versions that add HDR post processing support), change the resolution and quality of effects, add new passes (e.g. DLSS/GTAO), improve anisotropic filtering, ...
ReShade per se isn't necessarily needed by Luma, we could hook to the DirectX functions ourselves, but it makes things easier and natively adds ImGUI and serialization features (plus it's widely compatible).

Development requirements:
Windows 11 (Windows 10 probably works fine too)
Visual Studio 2022 (older versions might work too)
Windows 11 SDK 10.0.26100.0 (older versions work, but don't support HDR as good)

Instructions:
Run "setup.ps1" to add the game installation environment variables.
Set "VCPKG_ROOT" environment variable to your vcpkg installation folder if it wasn't already (download it from here https://github.com/microsoft/vcpkg, the version integrated with Visual Studio doesn't seem to be as reliable) (note that as of now these packages are not bound to a specific version).
Open "Prey-Luma.sln" and build it.
The code hot spots are in the main.cpp files etc etc etc...
Run "deploy (*).bat" to run the game. The Steam version of the game can't be launched from the exe, so that bat automatically closes the previous instance of the game, copies the new files and launches the game through Steam.
The "Data" folder needs to be manually copied into the directory of the game at least once. For development of shaders, it's suggested to make a symbolic link of the "Prey-Luma" folder (to allow git to pick up the changes while also having the latest version in game).

The mod version is stored in ... "VERSION"...
There's a "DEVELOPMENT" and "TEST" flag in main.cpp. They automatically spread to shaders on the next load/compile. Building in Debug (as opposed to Release), simply adds debug symbols etc, but no additional development features.

The game's original shaders code can be found in the ... pak in the GOG version of the game (extract the zip).
Luma shaders can be found in ".\Data\Binaries\Danielle\x64\Release\Prey-Luma\".

The steam game won't start if launched directly from the executable, unless u have a cracked steam dll. That's also the only way to hook graphics debuggers to the Steam version (NV and Intel work...?). no reshade

Luma inherits code from the following repositories:
https://github.com/ersh1/Luma-Prey/
https://github.com/clshortfuse/renodx