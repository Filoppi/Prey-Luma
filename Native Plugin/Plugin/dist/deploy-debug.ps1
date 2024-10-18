New-Item './' -ItemType Directory -Force -ErrorAction:SilentlyContinue
Copy-Item '../../build/Debug/*' './' -Force -Recurse -ErrorAction:SilentlyContinue
New-Item '$env:PREY_BIN_PATH' -ItemType Directory -Force -ErrorAction:SilentlyContinue
Copy-Item './Prey-Luma-Native.dll' '$env:PREY_BIN_PATH/Prey-Luma-Native.asi' -Force -Recurse -ErrorAction:SilentlyContinue
if (Test-Path './*.pdb') {
New-Item '$env:PREY_BIN_PATH' -ItemType Directory -Force -ErrorAction:SilentlyContinue
Copy-Item './*.pdb' '$env:PREY_BIN_PATH' -Force -Recurse -ErrorAction:SilentlyContinue }
if (Test-Path './*.ini') {
New-Item '$env:PREY_BIN_PATH' -ItemType Directory -Force -ErrorAction:SilentlyContinue
Copy-Item './*.ini' '$env:PREY_BIN_PATH' -Force -Recurse -ErrorAction:SilentlyContinue }
if (Test-Path './*.toml') {
New-Item '$env:PREY_BIN_PATH' -ItemType Directory -Force -ErrorAction:SilentlyContinue
Copy-Item './*.toml' '$env:PREY_BIN_PATH' -Force -Recurse -ErrorAction:SilentlyContinue }
if (Test-Path './*.json') {
New-Item '$env:PREY_BIN_PATH' -ItemType Directory -Force -ErrorAction:SilentlyContinue
Copy-Item './*.json' '$env:PREY_BIN_PATH' -Force -Recurse -ErrorAction:SilentlyContinue }
