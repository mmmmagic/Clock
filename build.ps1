$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $scriptRoot "build"
$generator = "Visual Studio 17 2022"
$platform = "x64"
$config = "Release"
$portableVsRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$portableMsBuild = Join-Path $portableVsRoot "MSBuild\Current\Bin\amd64\MSBuild.exe"
$generatorInstance = $null

if (Test-Path -LiteralPath $portableMsBuild) {
    $version = (Get-Item -LiteralPath $portableMsBuild).VersionInfo.FileVersion
    if ($version -match '\d+\.\d+\.\d+\.\d+') {
        $generatorInstance = "$($portableVsRoot.Replace('\', '/')),version=$($Matches[0])"
    }
}

$cache = Join-Path $buildDir "CMakeCache.txt"
if (Test-Path $cache) {
    $cacheText = Get-Content -Raw $cache
    $expectedGenerator = "CMAKE_GENERATOR:INTERNAL=$generator"
    $expectedPlatform = "CMAKE_GENERATOR_PLATFORM:INTERNAL=$platform"
    $expectedSourceDirectory = [IO.Path]::GetFullPath($scriptRoot).Replace('\', '/')
    $expectedSource = "CMAKE_HOME_DIRECTORY:INTERNAL=$expectedSourceDirectory"
    $validGeneratorInstance = $true

    if ($cacheText -match '(?m)^CMAKE_GENERATOR_INSTANCE:[^=]+=(.+)$') {
        $cachedInstance = ($Matches[1].Trim() -split ',', 2)[0].Replace('/', '\')
        if ($cachedInstance -and -not (Test-Path -LiteralPath $cachedInstance)) {
            $validGeneratorInstance = $false
        }
    }

    if (-not $cacheText.Contains($expectedGenerator) -or
        -not $cacheText.Contains($expectedPlatform) -or
        -not $cacheText.Contains($expectedSource) -or
        -not $validGeneratorInstance) {
        Write-Host "Reconfiguring stale build directory for $generator $platform..."
        @(
            "CMakeCache.txt",
            "CMakeFiles",
            "Makefile",
            "cmake_install.cmake",
            "FocusClock.sln",
            "FocusClock.vcxproj",
            "FocusClock.vcxproj.filters",
            "FocusClock.vcxproj.user",
            "CopyWhitelist.vcxproj",
            "CopyWhitelist.vcxproj.filters",
            "ALL_BUILD.vcxproj",
            "ALL_BUILD.vcxproj.filters",
            "ZERO_CHECK.vcxproj",
            "ZERO_CHECK.vcxproj.filters"
        ) | ForEach-Object {
            $path = Join-Path $buildDir $_
            if (Test-Path $path) {
                Remove-Item -LiteralPath $path -Recurse -Force
            }
        }
    }
}

$configureArgs = @("-S", $scriptRoot, "-B", $buildDir, "-G", $generator, "-A", $platform)
if ($generatorInstance) {
    $configureArgs += "-DCMAKE_GENERATOR_INSTANCE=$generatorInstance"
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

& cmake --build $buildDir --config $config
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

Write-Host ""
Write-Host "Build complete: $buildDir\FocusClock.exe"
