Prey (2017) Luma is made of two parts: a ReShade addon (from Pumbo), and a "native" plugin (dll) (from Ersh).

Instructions:
Run "setup.bat" to fully setup the Visual Studio projects and solution with cmake (part of the projects are pre-generated without cmake, because I couldn't bother to port everything to it).
Open "Prey-Luma.sln" and build ...

To upgrade the native plugin version, open ".\Native Plugin\Plugin\CMakeLists.txt" and update the "VERSION" number there.

The game's original shaders code can be found in the ... pak in the GOG version of the game (extract the zip).
Luma shaders can be found in ".\Data\Binaries\Danielle\x64\Release\renodx-dev\live".