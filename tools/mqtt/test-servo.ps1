param([Parameter(Mandatory)][ValidateRange(-90.0, 90.0)][double]$Angle)

. (Join-Path $PSScriptRoot 'common.ps1')
if ([double]::IsNaN($Angle) -or [double]::IsInfinity($Angle)) { throw 'Angle must be finite.' }
$settings = Get-CnbMqttSettings
$requestId = Get-CnbNextRevision
$payload = [ordered]@{
    schema_version = 1
    request_id = $requestId
    session_id = [guid]::NewGuid().ToString('N').Substring(0, 16)
    command = 'servo'
    angle_deg = $Angle
} | ConvertTo-Json -Compress

Publish-CnbMqttMessage -Settings $settings -Topic 'cnb/vagrant/command' -Payload $payload -Qos 1
Write-Host "Servo request $requestId sent. Check command/state for acceptance. Motor is disabled on acceptance; Start resumes navigation."
