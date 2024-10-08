Prey Luma is made of two parts: a ReShade addon (from Pumbo), and a "native" plugin (dll) (from Ersh).

Instructions:
Run "setup.bat" to fully setup the Visual Studio projects and solution with cmake (part of the projects are pre-generated without cmake, because I couldn't bother to port everything to it).
Open "Prey-Luma.sln" and build ...

To upgrade the native plugin version, open ".\Native Plugin\Plugin\CMakeLists.txt" and update the "VERSION" number there.