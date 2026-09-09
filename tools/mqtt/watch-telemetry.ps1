param([switch]$Raw)

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'format-telemetry.ps1')

$settings = Get-CnbMqttSettings
$executable = Get-CnbMqttExecutable -Name 'mosquitto_sub'
$arguments = @(
    '-h', $settings.Host,
    '-p', $settings.Port,
    '-u', $settings.Username,
    '-P', $settings.Password,
    '-t', 'cnb/vagrant/#',
    '-q', 1,
    '-v'
)

& $executable @arguments | ForEach-Object {
    try { Format-CnbMqttLine -Line $_ -Raw:$Raw }
    catch { Write-Warning "Could not format MQTT message: $($_.Exception.Message). Use -Raw to inspect messages." }
}
if ($LASTEXITCODE -ne 0) { throw "mosquitto_sub failed with exit code $LASTEXITCODE" }
