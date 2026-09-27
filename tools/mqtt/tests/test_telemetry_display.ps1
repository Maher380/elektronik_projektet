Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../format-telemetry.ps1')

function Assert-Contains($Text, $Expected) {
    if (-not $Text.Contains($Expected)) { throw "Missing '$Expected' in display: $Text" }
}

$line = 'cnb/vagrant/telemetry {"schema_version":1,"sequence":263,"uptime_ms":264570,"distance_cm":{"left":25.06317138671875,"center":70.11067962646484,"right":380.66986083984375},"adc_raw":{"left":1200,"center":0,"right":null},"motor":{"speed_command":0,"forward_duty":0,"backward_duty":0},"closest":{"sensor":"left","distance_cm":25.06317138671875},"driver_style":"decide_action","steering_deg":90,"control_state":"disarmed","motion_state":"stopped","reason":"operator_stop"}'
$originalCulture = [Threading.Thread]::CurrentThread.CurrentCulture
try {
    [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('sv-SE')
    $display = (Format-CnbMqttLine $line) -join "`n"
    foreach ($expected in @('DISARMED / STOPPED','25.06','70.11','380.67','ADC raw','1200','0','--','operator_stop','90.0 deg')) {
        Assert-Contains $display $expected
    }
    if ($display.Contains('25.06317138671875')) { throw 'Display did not round distance.' }
    if ((Format-CnbMqttLine $line -Raw) -cne $line) { throw 'Raw mode changed payload.' }
    $legacy = 'cnb/vagrant/telemetry {"distance_cm":{"left":null},"closest":null,"motor":{}}'
    Assert-Contains ((Format-CnbMqttLine $legacy) -join "`n") 'ADC raw'
    $config = 'cnb/vagrant/config/state {"revision":5,"result":"rejected","error":"driver_style_requires_disarmed","driver_style":"slow_left"}'
    $configDisplay = Format-CnbMqttLine $config
    foreach ($expected in @('CONFIG #5: rejected','slow_left','driver_style_requires_disarmed')) { Assert-Contains $configDisplay $expected }
    $command = 'cnb/vagrant/command/state {"last_request_id":8,"result":"accepted","session_id":"test-session","control_state":"armed"}'
    Assert-Contains (Format-CnbMqttLine $command) 'session test-session'
    Assert-Contains (Format-CnbMqttLine 'cnb/vagrant/status {"online":false}') 'False'
    $rejected = $false
    try { Format-CnbMqttLine 'cnb/vagrant/telemetry not-json' | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid JSON was accepted.' }
    Write-Output $display
    Write-Output 'PASS: readable telemetry, null/missing ADC, Swedish locale, status/errors and unchanged raw JSON.'
}
finally { [Threading.Thread]::CurrentThread.CurrentCulture = $originalCulture }
