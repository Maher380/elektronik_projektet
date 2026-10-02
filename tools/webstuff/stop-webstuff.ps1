# Stops the web panel and the cnb MQTT broker started by start-webstuff.ps1.
# Press Stop on the web page first; otherwise the Ford brakes when its heartbeat is lost (3 s).
param([switch]$KeepBroker)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$brokerConf = Join-Path (Join-Path $env:USERPROFILE 'cnb-mqtt') 'mosquitto.conf'
$serverScript = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\mqtt-ui\server.mjs'))

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

# Close the web panel port that start-webstuff.ps1 opened in the firewall.
$firewallGroup = 'cnb web panel'
if (Get-NetFirewallRule -Group $firewallGroup -ErrorAction SilentlyContinue) {
    Write-Host 'Closing the web panel firewall port - accept the Administrator prompt.'
    try {
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes(
            "Get-NetFirewallRule -Group '$firewallGroup' | Remove-NetFirewallRule"))
        $admin = Start-Process -FilePath (Get-Process -Id $PID).Path -Verb RunAs -Wait -PassThru -WindowStyle Hidden `
            -ArgumentList @('-NoProfile', '-EncodedCommand', $encoded)
        if ($admin.ExitCode -ne 0) { throw "exit code $($admin.ExitCode)" }
        Write-Host 'Firewall rule removed.'
    }
    catch { Write-Warning "Firewall rule not removed ($($_.Exception.Message)). Run stop-webstuff.ps1 again." }
}
else { Write-Host 'No web panel firewall rule to remove.' }
