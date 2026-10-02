# Starts the cnb MQTT broker and the web panel in the background, then opens the browser.
# Stop both with .\tools\stop-webstuff.ps1. Logs go to %USERPROFILE%\cnb-mqtt\logs.
param(
    [ValidateSet('vagrant', 'ford')][string]$Car = 'ford',
    [ValidateRange(1024, 65535)][int]$Port = 8765,
    [switch]$NoBrowser
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$brokerDir = Join-Path $env:USERPROFILE 'cnb-mqtt'
$brokerConf = Join-Path $brokerDir 'mosquitto.conf'
$logDir = Join-Path $brokerDir 'logs'
$repoAcl = Join-Path $PSScriptRoot 'mqtt\mosquitto-acl.example'
$serverScript = Join-Path $PSScriptRoot 'mqtt-ui\server.mjs'
$hotspotAddress = '192.168.137.1'

function Find-CnbProcess([string]$Name, [string]$Marker) {
    Get-CimInstance Win32_Process -Filter "Name='$Name'" |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($Marker) }
}

function Wait-CnbPort([int]$LocalPort, $Process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Get-NetTCPConnection -State Listen -LocalPort $LocalPort -ErrorAction SilentlyContinue) { return $true }
        if ($Process.HasExited) { return $false }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Show-CnbLogTail([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Get-Content -LiteralPath $Path -Tail 15 | ForEach-Object { "    $_" } }
}

if (-not (Test-Path -LiteralPath $brokerConf)) {
    throw "Missing $brokerConf. Do steps 1-3 in documentation\guides\mqtt_steg_for_steg.md first."
}
New-Item -ItemType Directory -Force $logDir | Out-Null

# Warnings only: the panel still works locally without these.
if (-not (Get-NetIPAddress -IPAddress $hotspotAddress -ErrorAction SilentlyContinue)) {
    Write-Warning "Mobile hotspot cnb-net is off ($hotspotAddress missing). The car cannot reach the broker."
}
$localAcl = Join-Path $brokerDir 'acl'
if ((Test-Path -LiteralPath $localAcl) -and (Compare-Object (Get-Content $localAcl) (Get-Content $repoAcl))) {
    Write-Warning "$localAcl differs from tools\mqtt\mosquitto-acl.example. Copy it over if topics are missing."
}

# --- Broker ---
$mosquitto = Join-Path $env:ProgramFiles 'mosquitto\mosquitto.exe'
if (-not (Test-Path -LiteralPath $mosquitto)) { throw "Mosquitto not found at $mosquitto." }

if (Find-CnbProcess 'mosquitto.exe' $brokerConf) {
    Write-Host "Broker already running."
}
else {
    $service = Get-Service mosquitto -ErrorAction SilentlyContinue
    if ($service -and $service.Status -eq 'Running') {
        throw ("The Windows 'mosquitto' service (anonymous, no ACL) is holding port 1883.`n" +
            "Run once in an Administrator PowerShell:`n" +
            "    Stop-Service mosquitto; Set-Service mosquitto -StartupType Manual")
    }
    if (Get-NetTCPConnection -State Listen -LocalPort 1883 -ErrorAction SilentlyContinue) {
        throw 'Port 1883 is used by another program. Close it and try again.'
    }
    $brokerLog = Join-Path $logDir 'mosquitto.log'
    $broker = Start-Process -FilePath $mosquitto -ArgumentList @('-c', "`"$brokerConf`"", '-v') `
        -WindowStyle Hidden -RedirectStandardError $brokerLog -RedirectStandardOutput (Join-Path $logDir 'mosquitto.out.log') -PassThru
    if (-not (Wait-CnbPort 1883 $broker)) {
        Show-CnbLogTail $brokerLog
        throw 'Broker did not start. See the log above.'
    }
    Write-Host "Broker started on port 1883 (pid $($broker.Id))."
}

# --- Web panel ---
$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
$node = if ($nodeCommand) { $nodeCommand.Source } else { Join-Path $env:ProgramFiles 'nodejs\node.exe' }
if (-not (Test-Path -LiteralPath $node)) { throw 'Install Node.js 20 or newer, then open a new PowerShell window.' }

$url = "http://127.0.0.1:$Port"
$running = Find-CnbProcess 'node.exe' $serverScript
if ($running) {
    Write-Host "Web panel already running ($url). Run stop-webstuff.ps1 first to switch car or port."
}
else {
    if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
        throw "Port $Port is used by another program. Pick another with -Port."
    }
    $webLog = Join-Path $logDir 'web.log'
    $web = Start-Process -FilePath $node `
        -ArgumentList @("`"$serverScript`"", '--port', [string]$Port, '--car', $Car) `
        -WindowStyle Hidden -RedirectStandardOutput $webLog -RedirectStandardError (Join-Path $logDir 'web.err.log') -PassThru
    if (-not (Wait-CnbPort $Port $web)) {
        Show-CnbLogTail (Join-Path $logDir 'web.err.log')
        throw 'Web panel did not start. See the log above.'
    }
    Write-Host "Web panel ($Car) started at $url (pid $($web.Id))."
}

if (-not $NoBrowser) { Start-Process $url }
Write-Host "Logs: $logDir"
