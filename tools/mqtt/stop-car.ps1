. (Join-Path $PSScriptRoot 'common.ps1')

$settings = Get-CnbMqttSettings
$payload = [ordered]@{
    schema_version = 1
    request_id = (Get-CnbNextRevision)
    session_id = [guid]::NewGuid().ToString('N').Substring(0, 16)
    command = 'stop'
} | ConvertTo-Json -Compress

Publish-CnbMqttMessage -Settings $settings `
    -Topic 'cnb/vagrant/command' `
    -Payload $payload `
    -Qos 1

Write-Host 'Stop sent.'
