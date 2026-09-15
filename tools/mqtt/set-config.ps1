param(
    [Parameter(Mandatory)][ValidateRange(30.0, 70.0)][double]$StopDistanceCm,
    [Parameter(Mandatory)][ValidateRange(0.0, 0.5)][double]$DriveDuty,
    [Parameter(Mandatory)][ValidateRange(200, 5000)][uint32]$TelemetryIntervalMs,
    [ValidateSet('DecideAction', 'SlowLeft', 'SlowRight', 'GradualSweep')][string]$DriverStyle
)

. (Join-Path $PSScriptRoot 'common.ps1')

$settings = Get-CnbMqttSettings
$revision = Get-CnbNextRevision
$payload = [ordered]@{
    schema_version = 1
    revision = $revision
    stop_distance_cm = $StopDistanceCm
    drive_duty = $DriveDuty
    telemetry_interval_ms = $TelemetryIntervalMs
}
# Omission preserves the car's active RAM style and supports legacy commands.
if ($PSBoundParameters.ContainsKey('DriverStyle')) {
    $styles = @{ DecideAction = 'decide_action'; SlowLeft = 'slow_left'; SlowRight = 'slow_right'; GradualSweep = 'gradual_sweep' }
    $payload.driver_style = $styles[$DriverStyle]
}
$payload = $payload | ConvertTo-Json -Compress

Publish-CnbMqttMessage -Settings $settings `
    -Topic 'cnb/vagrant/config/set' `
    -Payload $payload `
    -Qos 1 `
    -Retain

Write-Host "Published configuration revision $revision"
