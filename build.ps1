$ErrorActionPreference = "Stop"

$generator = "MinGW Makefiles"

cmake -S . -B build -G $generator
cmake --build build

Write-Host ""
Write-Host "Build complete: $PWD\build\FocusClock.exe"
