echo off
rd /s /q "%~dp0/build"
cmake -B "%~dp0/build" -S "%~dp0/Plugin" --preset=build-release-msvc-msvc
cmake --build "%~dp0/build"
