param(
    [string]$QtRoot = 'C:\Qt\6.8.2\mingw_64',
    [string]$MingwRoot = 'C:\Qt\Tools\mingw1310_64',
    [int]$Jobs = 8,
    [switch]$SmokeTest
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $projectRoot 'build'
$packageDir = Join-Path $projectRoot 'dist\Porymap-JP'
$env:Path = "$QtRoot\bin;$MingwRoot\bin;C:\Program Files\Git\cmd;$env:Path"
function Check-Exit([string]$step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed: $LASTEXITCODE" }
}
New-Item -ItemType Directory -Force $buildDir, $packageDir | Out-Null
Copy-Item -LiteralPath "$QtRoot\translations\qtbase_ja.qm" -Destination "$projectRoot\translations\qtbase_ja.qm"
& "$QtRoot\bin\lrelease.exe" "$projectRoot\translations\porymap_ja.ts"
Check-Exit 'lrelease'
Push-Location $buildDir
try {
    & "$QtRoot\bin\qmake.exe" "$projectRoot\porymap.pro" -config release
    Check-Exit 'qmake'
    & "$MingwRoot\bin\mingw32-make.exe" "-j$Jobs"
    Check-Exit 'make'
    Copy-Item -LiteralPath 'release\porymap.exe' -Destination "$packageDir\porymap.exe"
    & "$QtRoot\bin\windeployqt.exe" --release --compiler-runtime --no-translations "$packageDir\porymap.exe"
    Check-Exit 'windeployqt'
    Copy-Item -LiteralPath "$projectRoot\README-JA.md" -Destination "$packageDir\README-JA.md"
    Copy-Item -LiteralPath "$projectRoot\LICENSE.md" -Destination "$packageDir\LICENSE.md"
    Copy-Item -LiteralPath "$projectRoot\licenses" -Destination $packageDir -Recurse -Force
    if ($SmokeTest) {
        $qtModules = @('QtCharts','QtOpenGLWidgets','QtWidgets','QtOpenGL','QtGui','QtQml','QtQmlIntegration','QtNetwork','QtCore')
        $includes = @("-I$QtRoot\include", "-I$projectRoot\include", "-I$projectRoot\include\core", "-I$projectRoot\include\ui", "-I$projectRoot\include\lib", "-I$buildDir")
        $includes += $qtModules | ForEach-Object { "-I$QtRoot\include\$_" }
        & "$MingwRoot\bin\g++.exe" -std=c++17 -O2 -DQT_CHARTS_LIB -DQT_QML_LIB -DQT_NETWORK_LIB @includes -c "$PSScriptRoot\localization_smoke.cpp" -o 'release\localization_smoke.o'
        Check-Exit 'smoke compile'
        $objects = Get-Content 'release\object_script.porymap.Release' | Where-Object { $_ -ne 'release/main.o' }
        $objects += 'release/localization_smoke.o'
        $objects | Set-Content -Encoding ascii 'smoke-objects.txt'
        $libraries = $qtModules | Where-Object { $_ -ne 'QtQmlIntegration' } | ForEach-Object { "-l$($_ -replace '^Qt', 'Qt6')" }
        & "$MingwRoot\bin\g++.exe" '-mthreads' '-o' 'localization-smoke.exe' '@smoke-objects.txt' "-L$QtRoot\lib" @libraries '-lshell32'
        Check-Exit 'smoke link'
        & '.\localization-smoke.exe' -platform offscreen
        Check-Exit 'smoke run'
    }
} finally { Pop-Location }
Compress-Archive -Path $packageDir -DestinationPath "$projectRoot\dist\Porymap-JP-windows-x64.zip" -Force
Write-Output "Built: $packageDir\porymap.exe"
