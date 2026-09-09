param(
    [ValidateRange(1, 2)][int]$HeartbeatIntervalSeconds = 1
)

. (Join-Path $PSScriptRoot 'common.ps1')
$settings = Get-CnbMqttSettings
$sessionId = [guid]::NewGuid().ToString('N').Substring(0, 16)
$requestId = Get-CnbNextRevision
$subscriber = $null
$startAttempted = $false

try {
    # Start listening before publishing. A bounded retry with the SAME request
    # handles an ACK arriving before subscription; it cannot extend the car lease.
    $process = Start-CnbMqttProcess -Name 'mosquitto_sub' -Settings $settings `
        -ClientArguments @('-t', 'cnb/vagrant/command/state', '-q', '1', '-R')
    $subscriber = [pscustomobject]@{
        Process = $process
        Pending = $process.StandardOutput.ReadLineAsync()
        Errors = $process.StandardError.ReadToEndAsync()
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $startPayload = [ordered]@{
        schema_version = 1; request_id = $requestId; session_id = $sessionId; command = 'start'
    } | ConvertTo-Json -Compress
    $startAttempted = $true
    Publish-CnbMqttMessage -Settings $settings -Topic 'cnb/vagrant/command' -Payload $startPayload -Qos 1
    $timer.Restart()
    $accepted = $false
    $retried = $false
    while (-not $accepted) {
        $state = Read-CnbCommandState -Subscriber $subscriber
        if ($null -ne $state -and $state.last_request_id -eq $requestId) {
            if ($state.result -ne 'accepted' -or $state.control_state -ne 'armed' -or $state.session_id -ne $sessionId) {
                throw "Car rejected start: $($state.reason)."
            }
            $accepted = $true
        }
        if ($timer.ElapsedMilliseconds -ge 1500) { throw 'No matching start acknowledgement from the car.' }
        if (-not $accepted -and -not $retried -and $timer.ElapsedMilliseconds -ge 500) {
            $retried = $true
            Publish-CnbMqttMessage -Settings $settings -Topic 'cnb/vagrant/command' -Payload $startPayload -Qos 1
        }
        if (-not $accepted) { Start-Sleep -Milliseconds 25 }
    }
    Write-Host "Car accepted session $sessionId. Press Ctrl+C to stop."
    $heartbeatPayload = [ordered]@{
        schema_version = 1; session_id = $sessionId; command = 'heartbeat'
    } | ConvertTo-Json -Compress
    $nextHeartbeatMs = 0L
    $timer.Restart()
    while ($true) {
        $state = Read-CnbCommandState -Subscriber $subscriber
        if ($null -ne $state -and ($state.control_state -ne 'armed' -or $state.session_id -ne $sessionId)) {
            throw "Car session ended: $($state.reason)."
        }
        if ($timer.ElapsedMilliseconds -ge $nextHeartbeatMs) {
            $nextHeartbeatMs = $timer.ElapsedMilliseconds + 1000L * $HeartbeatIntervalSeconds
            Publish-CnbMqttMessage -Settings $settings -Topic 'cnb/vagrant/command' -Payload $heartbeatPayload -Qos 0
        }
        Start-Sleep -Milliseconds 25
    }
}
finally {
    if ($startAttempted) {
        try {
            $stopPayload = [ordered]@{
                schema_version = 1; request_id = (Get-CnbNextRevision); session_id = $sessionId; command = 'stop'
            } | ConvertTo-Json -Compress
            Publish-CnbMqttMessage -Settings $settings -Topic 'cnb/vagrant/command' -Payload $stopPayload -Qos 1
            Write-Host 'Stop sent.'
        }
        catch { Write-Warning "Stop publish failed; heartbeat timeout must disarm the car. $_" }
    }
    if ($null -ne $subscriber) {
        if (-not $subscriber.Process.HasExited) { $subscriber.Process.Kill(); $subscriber.Process.WaitForExit() }
        $subscriber.Process.Dispose()
    }
}
