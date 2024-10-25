cd /D "%~dp0"
taskkill /F /IM Prey.exe
copy ".\ReShade Addon\build\x64-Release\Prey-Luma-ReShade.addon64" "%PREY_BIN_PATH%"
:: copy ".\Data" "C:\Program Files (x86)\Steam\steamapps\common\Prey"
"C:\Program Files (x86)\Steam\steam.exe" steam://rungameid/480490