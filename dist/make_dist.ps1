# Builds the download: dist\files from the loader build, then BruteForceRecomp.zip (Install.bat, install.ps1, files\).
# Only the loader's own programs, the patches and the project's own assets go in; no game data (no xbox_hdd).
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot; $ld = Join-Path $here '..\loader'; $f = Join-Path $here 'files'
New-Item -ItemType Directory -Force $f | Out-Null
if (Test-Path (Join-Path $here 'server.txt')) { Copy-Item (Join-Path $here 'server.txt') $f -Force }   # the online server's address (set once by whoever runs the server)
foreach ($n in 'Start online server.bat','bfserver.exe','online_port.ps1') { $old = Join-Path $f $n; if (Test-Path $old) { Remove-Item $old -Force } }   # Milestone 100: the relay replaces them
foreach ($n in 'bflaunch.exe','bfcore.dll','bfview.exe','bflist.exe','bruteforce.ico','razorback.stl','razorback_tex.raw','razor_shots.wav','razor_reload.wav','razorhand.txt','razorhand_ready.txt','hitmarker.wav','hitmarker_img.raw') { Copy-Item (Join-Path $ld $n) $f -Force }
New-Item -ItemType Directory -Force (Join-Path $f 'patches') | Out-Null
Copy-Item (Join-Path $ld 'patches\*.bfp') (Join-Path $f 'patches') -Force
$zip = Join-Path $here '..\BruteForceRecomp.zip'; if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $here 'Install.bat'), (Join-Path $here 'install.ps1'), $f -DestinationPath $zip -CompressionLevel Optimal
Get-Item $zip | Select-Object FullName, @{n='MB'; e={[math]::Round($_.Length / 1MB, 1)}}
