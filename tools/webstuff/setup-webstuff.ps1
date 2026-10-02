# One-time setup for the cnb MQTT broker and web panel. Safe to run again: it only adds
# what is missing. Asks for UAC once if the Windows mosquitto service or firewall needs fixing.
param([switch]$AdminStep)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- Elevated part, run in a separate Administrator window ---
if ($AdminStep) {
    $service = Get-Service mosquitto -ErrorAction SilentlyContinue
    if ($service) {
        Stop-Service mosquitto -ErrorAction SilentlyContinue
        Set-Service mosquitto -StartupType Manual
        Write-Host "Windows mosquitto service stopped and set to Manual."
    }
    if (-not (Get-NetFirewallRule -DisplayName 'MQTT 1883 (cnb-mqtt broker)' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName 'MQTT 1883 (cnb-mqtt broker)' -Direction Inbound -Protocol TCP `
            -LocalPort 1883 -Action Allow -Profile Any | Out-Null
        Write-Host 'Firewall rule for TCP 1883 added.'
    }
    Start-Sleep -Seconds 3
    return
}

$brokerDir = Join-Path $env:USERPROFILE 'cnb-mqtt'
$brokerConf = Join-Path $brokerDir 'mosquitto.conf'
$passwords = Join-Path $brokerDir 'passwords'
$acl = Join-Path $brokerDir 'acl'
$repoAcl = Join-Path $PSScriptRoot '..\mqtt\mosquitto-acl.example'
$envFile = Join-Path $PSScriptRoot '..\mqtt\.env'
$mosquittoDir = Join-Path $env:ProgramFiles 'mosquitto'

# --- 1. Tools ---
if (-not (Test-Path -LiteralPath (Join-Path $mosquittoDir 'mosquitto.exe'))) {
    throw "Install Mosquitto to $mosquittoDir (https://mosquitto.org/download/), then run this again."
}
$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
if (-not $nodeCommand) { throw 'Install Node.js 20 or newer (https://nodejs.org), open a new PowerShell and run this again.' }
if ([int]((& $nodeCommand.Source --version).TrimStart('v').Split('.')[0]) -lt 20) { throw 'Node.js 20 or newer is required.' }
Write-Host 'OK  Mosquitto and Node.js found.'

# --- 2. Broker folder, config, ACL ---
New-Item -ItemType Directory -Force (Join-Path $brokerDir 'data'), (Join-Path $brokerDir 'logs') | Out-Null
if (-not (Test-Path -LiteralPath $brokerConf)) {
    $path = $brokerDir.Replace('\', '/')
    @"
listener 1883
allow_anonymous false
password_file $path/passwords
acl_file $path/acl
persistence true
persistence_location $path/data/
log_type error
log_type warning
log_type notice
"@ | Set-Content -LiteralPath $brokerConf -Encoding ascii
    Write-Host "OK  Created $brokerConf"
}
else { Write-Host 'OK  Broker config exists.' }

if (-not (Test-Path -LiteralPath $acl) -or (Compare-Object (Get-Content $acl) (Get-Content $repoAcl))) {
    if (Test-Path -LiteralPath $acl) { Copy-Item -LiteralPath $acl "$acl.old" -Force }
    Copy-Item -LiteralPath $repoAcl $acl -Force
    Write-Host 'OK  ACL updated from tools\mqtt\mosquitto-acl.example (previous copy saved as acl.old).'
}
else { Write-Host 'OK  ACL is up to date.' }

# --- 3. MQTT accounts (mosquitto_passwd asks for each password twice) ---
$existing = @()
if (Test-Path -LiteralPath $passwords) { $existing = @(Get-Content $passwords | ForEach-Object { ($_ -split ':')[0] }) }
foreach ($user in 'cnb-vagrant', 'cnb-dashboard', 'cnb-ford') {
    if ($existing -contains $user) { Write-Host "OK  MQTT user $user exists."; continue }
    Write-Host "Choose a password for MQTT user ${user}:"
    $arguments = @($passwords, $user)
    if (-not (Test-Path -LiteralPath $passwords)) { $arguments = @('-c') + $arguments }
    & (Join-Path $mosquittoDir 'mosquitto_passwd.exe') @arguments
    if ($LASTEXITCODE -ne 0) { throw "Could not add MQTT user $user." }
}

# --- 4. Web panel credentials ---
if (-not (Test-Path -LiteralPath $envFile)) {
    $secure = Read-Host 'Password you chose for cnb-dashboard' -AsSecureString
    $plain = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
    @(
        'CNB_MQTT_HOST=127.0.0.1'
        'CNB_MQTT_PORT=1883'
        'CNB_MQTT_USERNAME=cnb-dashboard'
        "CNB_MQTT_PASSWORD=$plain"
    ) | Set-Content -LiteralPath $envFile -Encoding ascii
    Write-Host "OK  Created $envFile"
}
else { Write-Host 'OK  tools\mqtt\.env exists.' }

# --- 5. Windows service and firewall (needs Administrator) ---
$service = Get-Service mosquitto -ErrorAction SilentlyContinue
$serviceInTheWay = $service -and ($service.Status -eq 'Running' -or $service.StartType -eq 'Automatic')
$firewallMissing = -not (Get-NetFirewallRule -DisplayName 'MQTT 1883 (cnb-mqtt broker)' -ErrorAction SilentlyContinue)
if ($serviceInTheWay -or $firewallMissing) {
    Write-Host 'Fixing the Windows mosquitto service / firewall - accept the Administrator prompt.'
    $shell = (Get-Process -Id $PID).Path
    Start-Process -FilePath $shell -Verb RunAs -Wait `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-AdminStep')
}
else { Write-Host 'OK  Windows mosquitto service is out of the way and firewall allows 1883.' }

Write-Host "`nSetup done. Start everything with .\tools\webstuff\start-webstuff.ps1"
