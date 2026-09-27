param(
    [ValidateSet('DecideAction', 'SlowLeft', 'SlowRight', 'GradualSweep')]
    [string]$DriverStyle = 'DecideAction',
    [ValidateRange(30.0, 70.0)][double]$StopDistanceCm = 30,
    [ValidateRange(0.0, 1.0)][double]$DriveDuty = 0.5,
    [ValidateRange(200, 5000)][uint32]$TelemetryIntervalMs = 1000
)

& (Join-Path $PSScriptRoot 'set-config.ps1') -StopDistanceCm $StopDistanceCm `
    -DriveDuty $DriveDuty -DriverStyle $DriverStyle -TelemetryIntervalMs $TelemetryIntervalMs
