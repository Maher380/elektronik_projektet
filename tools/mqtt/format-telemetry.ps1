# Presentation only: MQTT and NDJSON keep the original numeric precision.
function Get-CnbDisplayField {
    param($Data, [string]$Path, $Fallback = '--')
    $value = $Data
    foreach ($part in $Path.Split('.')) {
        if ($null -eq $value) { return $Fallback }
        $property = $value.PSObject.Properties[$part]
        if ($null -eq $property) { return $Fallback }
        $value = $property.Value
    }
    if ($null -eq $value) { return $Fallback }
    return $value
}

function Format-CnbNumber {
    param($Value, [int]$Decimals = 2)
    $number = 0.0
    if ($null -eq $Value -or -not [double]::TryParse([string]$Value,
        [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture,
        [ref]$number) -or [double]::IsNaN($number) -or [double]::IsInfinity($number)) { return '--' }
    return $number.ToString("F$Decimals", [Globalization.CultureInfo]::InvariantCulture)
}

function Format-CnbMqttLine {
    param([Parameter(Mandatory)][string]$Line, [switch]$Raw)
    if ($Raw) { return $Line }
    $parts = $Line -split ' ', 2
    if ($parts.Count -ne 2) { throw 'Expected an MQTT topic followed by JSON.' }
    $topic = $parts[0]
    $data = $parts[1] | ConvertFrom-Json -ErrorAction Stop
    $time = Get-Date -Format 'HH:mm:ss'
    if ($topic -eq 'cnb/vagrant/telemetry') {
        $state = [string](Get-CnbDisplayField $data 'control_state')
        $motion = [string](Get-CnbDisplayField $data 'motion_state')
        '[{0}] #{1} | {2} / {3} | uptime {4} ms' -f $time,
            (Get-CnbDisplayField $data 'sequence'), $state.ToUpperInvariant(),
            $motion.ToUpperInvariant(), (Get-CnbDisplayField $data 'uptime_ms')
        '  Mode: {0} | Reason: {1} | Steering: {2} deg' -f
            (Get-CnbDisplayField $data 'driver_style'), (Get-CnbDisplayField $data 'reason'),
            (Format-CnbNumber (Get-CnbDisplayField $data 'steering_deg') 1)
        '  {0,-13} {1,10} {2,10} {3,10}' -f 'Sensor', 'LEFT', 'CENTER', 'RIGHT'
        '  {0,-13} {1,10} {2,10} {3,10}' -f 'Distance cm',
            (Format-CnbNumber (Get-CnbDisplayField $data 'distance_cm.left')),
            (Format-CnbNumber (Get-CnbDisplayField $data 'distance_cm.center')),
            (Format-CnbNumber (Get-CnbDisplayField $data 'distance_cm.right'))
        '  {0,-13} {1,10} {2,10} {3,10}' -f 'ADC raw',
            (Format-CnbNumber (Get-CnbDisplayField $data 'adc_raw.left') 0),
            (Format-CnbNumber (Get-CnbDisplayField $data 'adc_raw.center') 0),
            (Format-CnbNumber (Get-CnbDisplayField $data 'adc_raw.right') 0)
        '  Motor cmd: {0} | PWM forward: {1} | backward: {2} | Closest: {3} ({4} cm)' -f
            (Format-CnbNumber (Get-CnbDisplayField $data 'motor.speed_command')),
            (Format-CnbNumber (Get-CnbDisplayField $data 'motor.forward_duty')),
            (Format-CnbNumber (Get-CnbDisplayField $data 'motor.backward_duty')),
            (Get-CnbDisplayField $data 'closest.sensor'),
            (Format-CnbNumber (Get-CnbDisplayField $data 'closest.distance_cm'))
        ''
    }
    elseif ($topic -eq 'cnb/vagrant/config/state') {
        '[{0}] CONFIG #{1}: {2} | mode {3} | stop {4} cm | duty {5} | telemetry {6} ms | error {7}' -f $time,
            (Get-CnbDisplayField $data 'revision'), (Get-CnbDisplayField $data 'result'),
            (Get-CnbDisplayField $data 'driver_style'), (Get-CnbDisplayField $data 'stop_distance_cm'),
            (Format-CnbNumber (Get-CnbDisplayField $data 'drive_duty')),
            (Get-CnbDisplayField $data 'telemetry_interval_ms'), (Get-CnbDisplayField $data 'error')
    }
    elseif ($topic -eq 'cnb/vagrant/command/state') {
        '[{0}] COMMAND #{1}: {2} | {3}/{4} | mode {5} | reason {6} | error {7} | session {8}' -f $time,
            (Get-CnbDisplayField $data 'last_request_id'), (Get-CnbDisplayField $data 'result'),
            (Get-CnbDisplayField $data 'control_state'), (Get-CnbDisplayField $data 'motion_state'),
            (Get-CnbDisplayField $data 'driver_style'), (Get-CnbDisplayField $data 'reason'),
            (Get-CnbDisplayField $data 'error'), (Get-CnbDisplayField $data 'session_id')
    }
    elseif ($topic -eq 'cnb/vagrant/status') {
        '[{0}] STATUS online: {1}' -f $time, (Get-CnbDisplayField $data 'online')
    }
    else { $Line }
}
