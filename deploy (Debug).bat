cd /D "%~dp0"
taskkill /F /IM Prey.exe
copy ".\ReShade Addon\build\x64-Debug\Prey-Luma-ReShade.addon64" "C:\Program Files (x86)\Steam\steamapps\common\Prey\Binaries\Danielle\x64\Release"
copy ".\Native Plugin\build\Debug\Prey-Luma-Native.dll" "C:\Program Files (x86)\Steam\steamapps\common\Prey\Binaries\Danielle\x64\Release\Prey-Luma-Native.asi"
:: copy ".\Data" "C:\Program Files (x86)\Steam\steamapps\common\Prey"
"C:\Program Files (x86)\Steam\steam.exe" steam://rungameid/480490