Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-CnbMqttExecutable {
    param([Parameter(Mandatory)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }

    $fallback = Join-Path $env:ProgramFiles "mosquitto\$Name.exe"
    if (Test-Path -LiteralPath $fallback) { return $fallback }

    throw "$Name was not found. Install Mosquitto and add its directory to PATH."
}

function Get-CnbMqttSettings {
    $environmentPath = Join-Path $PSScriptRoot '.env'
    if (-not (Test-Path -LiteralPath $environmentPath)) {
        throw "Missing $environmentPath. Create it from .env.example and enter local credentials."
    }

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $environmentPath) {
        $trimmed = $line.Trim()
        if (($trimmed.Length -eq 0) -or $trimmed.StartsWith('#')) { continue }

        $parts = $line -split '=', 2
        if ($parts.Count -ne 2) { throw "Invalid line in ${environmentPath}: $line" }
        $values[$parts[0].Trim()] = $parts[1].Trim()
    }

    $required = @('CNB_MQTT_HOST', 'CNB_MQTT_PORT', 'CNB_MQTT_USERNAME', 'CNB_MQTT_PASSWORD')
    foreach ($name in $required) {
        if (-not $values.ContainsKey($name) -or [string]::IsNullOrWhiteSpace($values[$name])) {
            throw "Missing $name in $environmentPath"
        }
    }

    $port = 0
    if (-not [int]::TryParse($values.CNB_MQTT_PORT, [ref]$port) -or ($port -lt 1) -or ($port -gt 65535)) {
        throw 'CNB_MQTT_PORT must be an integer from 1 to 65535.'
    }

    [pscustomobject]@{
        Host = $values.CNB_MQTT_HOST
        Port = $port
        Username = $values.CNB_MQTT_USERNAME
        Password = $values.CNB_MQTT_PASSWORD
    }
}

function Start-CnbMqttProcess {
    param([string]$Name, [pscustomobject]$Settings, [string[]]$ClientArguments)

    $arguments = @('-h', $Settings.Host, '-p', [string]$Settings.Port,
        '-u', $Settings.Username, '-P', $Settings.Password) + $ClientArguments
    # Windows CRT quoting also works in Windows PowerShell 5.1, which lacks
    # ProcessStartInfo.ArgumentList. Never interpolate these into shell code.
    $quoted = foreach ($argument in $arguments) {
        '"' + [regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
    }
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = Get-CnbMqttExecutable -Name $Name
    $info.Arguments = $quoted -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    [void]$process.Start()
    return $process
}

function Publish-CnbMqttMessage {
    param(
        [Parameter(Mandatory)][pscustomobject]$Settings,
        [Parameter(Mandatory)][string]$Topic,
        [Parameter(Mandatory)][string]$Payload,
        [ValidateSet(0, 1)][int]$Qos = 0,
        [switch]$Retain
    )
    $arguments = @('-t', $Topic, '-q', [string]$Qos, '-s')
    if ($Retain) { $arguments += '-r' }
    $process = Start-CnbMqttProcess -Name 'mosquitto_pub' -Settings $Settings -ClientArguments $arguments
    try {
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        # Passing JSON on stdin preserves its quotes in both PowerShell 5 and 7.
        $bytes = [Text.Encoding]::UTF8.GetBytes($Payload)
        $process.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
        $process.StandardInput.Close()
        if (-not $process.WaitForExit(2000)) { throw 'MQTT publication timed out.' }
        if ($process.ExitCode -ne 0) { throw "mosquitto_pub failed with exit code $($process.ExitCode)." }
    }
    finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

function Read-CnbCommandState {
    param([Parameter(Mandatory)]$Subscriber)
    if ($Subscriber.Process.HasExited) { throw 'MQTT state subscription disconnected.' }
    if ($Subscriber.Pending.IsCompleted) {
        $line = $Subscriber.Pending.GetAwaiter().GetResult()
        if ($null -eq $line) { throw 'MQTT state subscription ended.' }
        $Subscriber.Pending = $Subscriber.Process.StandardOutput.ReadLineAsync()
        try { return ($line | ConvertFrom-Json) }
        catch { throw 'Invalid JSON on command/state.' }
    }
    return $null
}

function Get-CnbNextRevision {
    $revisionPath = Join-Path $PSScriptRoot '.revision'
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    $stream = $null
    while ($null -eq $stream) {
        try {
            $stream = [IO.File]::Open($revisionPath, [IO.FileMode]::OpenOrCreate,
                [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        }
        catch [IO.IOException] {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Revision file is busy.' }
            Start-Sleep -Milliseconds 25
        }
    }
    try {
        $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8, $true, 1024, $true)
        $text = $reader.ReadToEnd().Trim()
        $reader.Dispose()
        $stored = 0L
        if ($text.Length -gt 0 -and (-not [long]::TryParse($text, [ref]$stored) -or $stored -lt 0)) {
            throw 'Invalid revision file.'
        }
        if ($stored -ge [uint32]::MaxValue) { throw 'Revision counter exhausted.' }
        $revision = [uint32][Math]::Max([DateTimeOffset]::UtcNow.ToUnixTimeSeconds(), $stored + 1)
        $bytes = [Text.Encoding]::UTF8.GetBytes([string]$revision)
        $stream.Position = 0
        $stream.SetLength(0)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        return $revision
    }
    finally { $stream.Dispose() }
}
