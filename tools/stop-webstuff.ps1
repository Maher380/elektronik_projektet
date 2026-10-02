# Stops the web panel and the cnb MQTT broker started by start-webstuff.ps1.
# Press Stop on the web page first; otherwise the Ford brakes when its heartbeat is lost (3 s).
param([switch]$KeepBroker)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$brokerConf = Join-Path (Join-Path $env:USERPROFILE 'cnb-mqtt') 'mosquitto.conf'
$serverScript = Join-Path $PSScriptRoot 'mqtt-ui\server.mjs'

function Stop-CnbProcess([string]$Label, [string]$Name, [string]$Marker) {
    $found = @(Get-CimInstance Win32_Process -Filter "Name='$Name'" |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($Marker) })
    if ($found.Count -eq 0) { Write-Host "$Label was not running."; return }
    foreach ($process in $found) {
        Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
        Write-Host "$Label stopped (pid $($process.ProcessId))."
    }
}

Stop-CnbProcess 'Web panel' 'node.exe' $serverScript
if (-not $KeepBroker) { Stop-CnbProcess 'Broker' 'mosquitto.exe' $brokerConf }
