# Brute Force PC recomp - the game's online port on the router (UPnP), run by play.bat around the game.
#   open   ask the router to forward UDP 34567 to this PC, and write the router's internet address to external_ip.txt
#   close  remove that forwarding again
# Only used when netplay.txt has the line "upnp" (the installer asks first). Uses Windows' own UPnP support (HNetCfg.NATUPnP).
param([ValidateSet('open', 'close')][string]$Action = 'open', [int]$Port = 34567)
$ErrorActionPreference = 'SilentlyContinue'
Set-Location $PSScriptRoot
$log = Join-Path $PSScriptRoot 'online_port.log'
function Log($m) { Add-Content $log ("{0}  {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $m) }

$nat = New-Object -ComObject HNetCfg.NATUPnP
$maps = if ($nat) { $nat.StaticPortMappingCollection } else { $null }
if (-not $maps) { Log "$Action : the router did not answer UPnP (turn UPnP on in the router, or forward UDP $Port to this PC by hand)"; exit 0 }

if ($Action -eq 'close') { $maps.Remove($Port, 'UDP') | Out-Null; Log "close: UDP $Port forwarding removed"; exit 0 }

# this PC's address on the home network: the one the default route uses
$local = (Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway -ne $null -and $_.NetAdapter.Status -eq 'Up' } | Select-Object -First 1).IPv4Address.IPAddress
if (-not $local) { Log "open: no local IPv4 address found"; exit 0 }
$maps.Remove($Port, 'UDP') | Out-Null
$m = $maps.Add($Port, 'UDP', $Port, $local, $true, 'Brute Force online')
if (-not $m) { Log "open: the router refused to forward UDP $Port to $local"; exit 0 }
$ext = $m.ExternalIPAddress
if ($ext -match '^\d{1,3}(\.\d{1,3}){3}$') {
    Set-Content (Join-Path $PSScriptRoot 'external_ip.txt') $ext -Encoding ascii
    Log "open: UDP $Port forwarded to $local; internet address $ext"
    $o = $ext.Split('.') | ForEach-Object { [int]$_ }
    if ($o[0] -eq 10 -or ($o[0] -eq 100 -and $o[1] -ge 64 -and $o[1] -le 127) -or ($o[0] -eq 172 -and $o[1] -ge 16 -and $o[1] -le 31) -or ($o[0] -eq 192 -and $o[1] -eq 168)) {
        Log "open: warning - the router's internet address $ext is itself private: the provider shares addresses (CGNAT), so this PC can join matches but others cannot reach it when it hosts" }
} else { Log "open: UDP $Port forwarded to $local, but the router gave no internet address" }
exit 0
