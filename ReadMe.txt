Prey (2017) Luma is made of two parts: a ReShade addon (from Pumbo), and a "native" plugin (dll) (from Ersh).
It's rewriting the whole post processing phase (and potentially rendering...) of CryEngine/Prey.
The native plugin has the purpose of hooking into the engine code and swapping texture formats (e.g. from 8bit to 16bit etc), changing the swapchain to be scRGB HDR and making it use a more modern and responsive flip model. It also makes some modifications to TAA to change the rendering jitters for improved DLSS compatibility. Doing this stuff exclusively through DirectX hooks would probably be possible but exponentially more complicated (mostly because CryEngine re-uses textures for different purposes, and we'd only want to upgrade some of them while not other).
The ReShade addon has the purpose of adding behaviour by hooking to their DirectX functions, these include: replacing shaders (with versions that add HDR post processing support), change the resolution and quality of effects, add new passes (e.g. DLSS/GTAO), improve anisotropic filtering, ...
ReShade per se isn't necessarily needed by Luma, we could hook to the DirectX functions ourselves, but it makes things easier and natively adds ImGUI and serialization features (plus it's widely compatible).

Instructions:
Run "setup.ps1" to fully setup the Visual Studio projects and solution with cmake (part of the projects are pre-generated without cmake, because I couldn't bother to port everything to it). This will also add the game installation environment variables.
Open "Prey-Luma.sln" and build ...
The code hot spots are in the main.cpp files etc etc etc...

To upgrade the native plugin version, open ".\Native Plugin\Plugin\CMakeLists.txt" and update the "VERSION" number there.
The ReShade addon version is stored in ...

The game's original shaders code can be found in the ... pak in the GOG version of the game (extract the zip).
Luma shaders can be found in ".\Data\Binaries\Danielle\x64\Release\renodx-dev\live".

Luma inherits code from the following repositories:
https://github.com/ersh1/Luma-Prey/
https://github.com/clshortfuse/renodx