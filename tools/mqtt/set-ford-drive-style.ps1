<#
.SYNOPSIS
    Select the Ford's drive style, and nothing else.

.DESCRIPTION
    The Ford's config/set is deliberately narrowed to the drive style: every other Ford
    setting is compiled into the firmware, and a payload offering one is rejected rather
    than applied. So this is a separate script from set-config.ps1, which carries
    Vagrant's settings and would be refused by the Ford.

    A style can only change while the car is disarmed. That is what makes "arming starts
    the selected style" answerable: the question of what the car will do when you press
    Start has one answer, fixed before anything can move. Stop the car first, or the car
    replies drive_style_requires_disarmed.

    ManualByRemote is what the Ford boots on: you drive it from the page. GapCalibration
    drives a fixed script that measures the wheel's magnet gaps, and the style is not
    persisted, so a power cycle returns the car to ManualByRemote. See ADR 0009.

.PARAMETER DriveStyle
    ManualByRemote to drive it yourself, GapCalibration to measure the magnet gaps.

.EXAMPLE
    .\set-ford-drive-style.ps1 -DriveStyle GapCalibration

    Then LIFT THE CAR before pressing Start: arming spins the measured wheel under power
    for about a minute.
#>
param(
    [Parameter(Mandatory)][ValidateSet('ManualByRemote', 'GapCalibration')][string]$DriveStyle
)

. (Join-Path $PSScriptRoot 'common.ps1')

$settings = Get-CnbMqttSettings
$revision = Get-CnbNextRevision

# Exactly the three fields the Ford's narrowed config/set accepts.
$styles = @{ ManualByRemote = 'manual_by_remote'; GapCalibration = 'gap_calibration' }
$payload = [ordered]@{
    schema_version = 1
    revision       = $revision
    drive_style    = $styles[$DriveStyle]
} | ConvertTo-Json -Compress

Publish-CnbMqttMessage -Settings $settings `
    -Topic 'cnb/ford/config/set' `
    -Payload $payload `
    -Qos 1 `
    -Retain

Write-Host "Published Ford drive style $($styles[$DriveStyle]) as revision $revision"
if ($DriveStyle -eq 'GapCalibration') {
    Write-Host 'LIFT THE CAR before starting: arming spins the measured wheel for about a minute.' -ForegroundColor Yellow
}
