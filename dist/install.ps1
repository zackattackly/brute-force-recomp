# Brute Force PC recomp - installer.
# Copies the loader to %LOCALAPPDATA%\BruteForceRecomp, finds your own copy of the game, sets up online play if you want it,
# and puts a "Brute Force" shortcut on the desktop. Nothing from the game itself is in this download: it runs your copy.
param([string]$Target = "$env:LOCALAPPDATA\BruteForceRecomp", [switch]$NoShortcut, [string]$GameFolder = "", [string]$Online = "")
$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot 'files'
Write-Host ""
Write-Host "  Brute Force - PC recomp installer" -ForegroundColor Cyan
Write-Host "  ---------------------------------"
Write-Host ""

function Test-GameFolder([string]$p) { return $p -and (Test-Path (Join-Path $p 'default.xbe')) -and (Test-Path (Join-Path $p 'data\common.tgz')) }

# 1. the files
if (Get-Process bflaunch -ErrorAction SilentlyContinue) { Write-Host "  The game is running. Close it first, then run the installer again." -ForegroundColor Yellow; Read-Host "  Press Enter to close"; exit 1 }
New-Item -ItemType Directory -Force $Target | Out-Null
Copy-Item (Join-Path $src '*') $Target -Recurse -Force
Write-Host "  Installed to $Target"

# 2. the game (an extracted game folder: default.xbe and the data folder)
$game = $GameFolder
if (-not (Test-GameFolder $game)) {
    $old = Join-Path $Target 'gamepath.txt'
    if (Test-Path $old) { $g = (Get-Content $old -TotalCount 1).Trim(); if (Test-GameFolder $g) { $game = $g } }
}
if (-not (Test-GameFolder $game)) {
    Write-Host "  Looking for your Brute Force game folder..."
    $roots = @("$env:USERPROFILE\Desktop", "$env:USERPROFILE\OneDrive\Desktop", "$env:USERPROFILE\Downloads", "$env:USERPROFILE\Documents", "$env:USERPROFILE\Games", "C:\Games", "D:\", "E:\") | Where-Object { Test-Path $_ }
    foreach ($r in $roots) {
        $hit = Get-ChildItem $r -Filter default.xbe -Recurse -Depth 4 -ErrorAction SilentlyContinue | Where-Object { Test-GameFolder $_.DirectoryName } | Select-Object -First 1
        if ($hit) { $game = $hit.DirectoryName; break }
    }
    if ($game) { Write-Host "  Found: $game" }
}
if (-not (Test-GameFolder $game)) {
    Write-Host "  Please pick your Brute Force game folder (the one with default.xbe in it)."
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = "Pick your Brute Force game folder (it has default.xbe in it)"
    while ($true) {
        if ($dlg.ShowDialog() -ne 'OK') { Write-Host "  No game folder picked. Run the installer again when you have it." -ForegroundColor Yellow; Read-Host "  Press Enter to close"; exit 1 }
        if (Test-GameFolder $dlg.SelectedPath) { $game = $dlg.SelectedPath; break }
        [System.Windows.Forms.MessageBox]::Show("That folder has no default.xbe and data\common.tgz. Pick the extracted Brute Force game folder.", "Brute Force") | Out-Null
    }
}
Set-Content (Join-Path $Target 'gamepath.txt') $game -Encoding ascii
Write-Host "  Game folder: $game"

# 3. online play: PLAY ONLINE in the game's menu finds matches through the online server (server.txt holds its address)
$np = Join-Path $Target 'netplay.txt'
# Milestone 100: online play needs no setup. PLAY ONLINE goes through the online server's relay (server.txt), so nobody forwards
# ports. An old netplay.txt from the match-list version (list / upnp lines) is removed; other settings (password, offline) stay.
if (Test-Path $np) {
    $old = @(Get-Content $np | Where-Object { $_ -notmatch '^\s*(list|upnp|host)\b' -and $_.Trim() })
    if ($old.Count) { Set-Content $np $old -Encoding ascii } else { Remove-Item $np }
}
if ($Online -match '^[Nn]') { Set-Content $np 'offline' -Encoding ascii; Write-Host "  Online play off (netplay.txt says offline)." }
else { Write-Host "  Online play: on. Pick PLAY ONLINE in the game to host or join; no router setup needed." }
# 4. shortcuts
if (-not $NoShortcut) {
    $ws = New-Object -ComObject WScript.Shell
    foreach ($dir in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) {
        $lnk = $ws.CreateShortcut((Join-Path $dir 'Brute Force.lnk'))
        $lnk.TargetPath = Join-Path $Target 'play.bat'
        $lnk.WorkingDirectory = $Target
        $lnk.IconLocation = (Join-Path $Target 'bruteforce.ico') + ',0'
        $lnk.WindowStyle = 7
        $lnk.Description = 'Brute Force (PC recomp)'
        $lnk.Save()
    }
    Write-Host "  Shortcut 'Brute Force' added to the desktop and the Start menu."
}
Write-Host ""
Write-Host "  Done. The game starts full screen (F11 or Alt+Enter: window). Click in the game to use the mouse, F10 lets it go." -ForegroundColor Green
Write-Host ""
if (-not $NoShortcut) { if ((Read-Host "  Start the game now? (Y/N)") -match '^[Yy]') { Start-Process (Join-Path $Target 'play.bat') -WorkingDirectory $Target -WindowStyle Minimized } }
