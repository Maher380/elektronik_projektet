param(
    [string]$OutputPath
)

. (Join-Path $PSScriptRoot 'common.ps1')

$settings = Get-CnbMqttSettings
$executable = Get-CnbMqttExecutable -Name 'mosquitto_sub'

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $fileName = 'telemetry-{0}.ndjson' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
    $OutputPath = Join-Path (Join-Path $PSScriptRoot 'logs') $fileName
}

$directory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$arguments = @(
    '-h', $settings.Host,
    '-p', $settings.Port,
    '-u', $settings.Username,
    '-P', $settings.Password,
    '-t', 'cnb/vagrant/telemetry',
    '-q', 0
)

Write-Host "Logging telemetry to $OutputPath. Press Ctrl+C to stop."

& $executable @arguments | ForEach-Object {
    $payload = $_
    try {
        $telemetry = $payload | ConvertFrom-Json -ErrorAction Stop
    }
    catch {
        Write-Warning "Ignored invalid telemetry payload: $payload"
        return
    }

    $record = [ordered]@{
        received_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
        telemetry = $telemetry
    } | ConvertTo-Json -Compress -Depth 8

    Add-Content -LiteralPath $OutputPath -Value $record -Encoding UTF8
}

if ($LASTEXITCODE -ne 0) { throw "mosquitto_sub failed with exit code $LASTEXITCODE" }
