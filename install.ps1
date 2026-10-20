# Installs the built T6Gnosis.dll into Plutonium's plugins folder (T6 must be closed).
# The DLL writes its GSC helper itself at game start, so nothing else is copied.
# Also removes files earlier Gnosis builds left behind (logs, old helper names,
# the separate ImGui window file); the previous DLL is kept as T6Gnosis.dll.bak.
#   powershell -ExecutionPolicy Bypass -File install.ps1 [-Dll path]
param([string]$Dll = '')
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name 'plutonium-bootstrapper-win32','t6zm' -ErrorAction SilentlyContinue).Count) {
    throw 'Close T6 before installing T6 Gnosis.'
}
if (-not $Dll) {
    $Dll = @((Join-Path $PSScriptRoot 'dist\T6Gnosis.dll'), (Join-Path $PSScriptRoot 'build\T6Gnosis.dll')) |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Sort-Object { (Get-Item -LiteralPath $_).LastWriteTime } -Descending | Select-Object -First 1
}
if (-not $Dll -or -not (Test-Path -LiteralPath $Dll -PathType Leaf)) { throw 'Build T6Gnosis.dll first (build.bat).' }
$plutonium = Join-Path $env:LOCALAPPDATA 'Plutonium'
$plugins = Join-Path $plutonium 'plugins'
$scripts = Join-Path $plutonium 'storage\t6\scripts\zm'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
$target = Join-Path $plugins 'T6Gnosis.dll'
if (Test-Path -LiteralPath $target -PathType Leaf) { Copy-Item -LiteralPath $target -Destination "$target.bak" -Force }
Copy-Item -LiteralPath $Dll -Destination $target -Force
if ((Get-FileHash -LiteralPath $Dll).Hash -ne (Get-FileHash -LiteralPath $target).Hash) { throw 'Copy verification failed.' }
# Leftovers of earlier Gnosis builds; none is written by the current one.
$stale = @(
    (Join-Path $plugins 'T6StaticNavigation.dll'),
    (Join-Path $plugins 'T6Gnosis-imgui.ini'), (Join-Path $plugins 'T6Gnosis-despawn.csv'),
    (Join-Path $plugins 'T6Gnosis-timing.csv'), (Join-Path $plugins 'T6Gnosis-frametrace.txt'),
    (Join-Path $plugins 'T6Gnosis-probe.txt'),
    (Join-Path $scripts 't6gnosis_despawn.gsc'), (Join-Path $scripts 't6nav_spawn_sites.gsc'))
foreach ($path in $stale) {
    if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path; Write-Host "Removed old file: $path" }
}
Write-Host "Installed $target"
