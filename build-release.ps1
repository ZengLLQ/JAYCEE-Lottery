$ErrorActionPreference = "Stop"

$projectDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDirectory = Join-Path $projectDirectory "build-v2"
$distributionDirectory = Join-Path $projectDirectory "dist"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found. Install CMake to build JAYCEE Lottery."
}

if (Get-Command g++ -ErrorAction SilentlyContinue) {
    cmake -S $projectDirectory -B $buildDirectory -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
} else {
    cmake -S $projectDirectory -B $buildDirectory -A x64
}

cmake --build $buildDirectory --config Release --parallel

New-Item -ItemType Directory -Force -Path $distributionDirectory | Out-Null

$candidates = @(
    (Join-Path $buildDirectory "bin\JAYCEE Lottery.exe"),
    (Join-Path $buildDirectory "bin\Release\JAYCEE Lottery.exe")
)

$executable = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $executable) {
    throw "The build completed, but JAYCEE Lottery.exe was not found."
}

Copy-Item -LiteralPath $executable -Destination (Join-Path $distributionDirectory "JAYCEE Lottery.exe") -Force
Copy-Item -LiteralPath (Join-Path $projectDirectory "docs\QUICK_START.md") `
    -Destination (Join-Path $distributionDirectory "README.md") -Force
Copy-Item -LiteralPath (Join-Path $projectDirectory "assets\participants-template.csv") `
    -Destination (Join-Path $distributionDirectory "participants-template.csv") -Force

$installerCandidates = @(
    (Join-Path $buildDirectory "bin\JAYCEE-Lottery-Setup.exe"),
    (Join-Path $buildDirectory "bin\Release\JAYCEE-Lottery-Setup.exe")
)
$installer = $installerCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $installer) {
    throw "The build completed, but JAYCEE-Lottery-Setup.exe was not found."
}
Copy-Item -LiteralPath $installer -Destination (Join-Path $distributionDirectory "JAYCEE-Lottery-Setup.exe") -Force

$portableArchive = Join-Path $distributionDirectory "JAYCEE-Lottery-portable.zip"
Compress-Archive -LiteralPath @(
    (Join-Path $distributionDirectory "JAYCEE Lottery.exe"),
    (Join-Path $distributionDirectory "README.md"),
    (Join-Path $distributionDirectory "participants-template.csv")
) -DestinationPath $portableArchive -Force

Write-Host "Portable executable created at: $distributionDirectory\JAYCEE Lottery.exe"
Write-Host "Portable package created at: $portableArchive"
Write-Host "Windows installer created at: $distributionDirectory\JAYCEE-Lottery-Setup.exe"
