param(
    [switch]$Demo,
    [ValidateRange(1024, 65535)][int]$Port = 8765
)
$ErrorActionPreference = 'Stop'
$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
$nodePath = if ($null -ne $nodeCommand) { $nodeCommand.Source } else { $null }
if (-not $nodePath) {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'nodejs\node.exe'),
        (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe')
    )
    $nodePath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $nodePath) { throw 'Install Node.js 20 or newer, then open a new PowerShell window.' }
$nodeVersion = & $nodePath --version
if ([int]($nodeVersion.TrimStart('v').Split('.')[0]) -lt 20) { throw 'Node.js 20 or newer is required.' }
$serverArguments = @((Join-Path $PSScriptRoot 'server.mjs'), '--port', [string]$Port)
if ($Demo) { $serverArguments += '--demo' }
& $nodePath @serverArguments
if ($LASTEXITCODE -ne 0) { throw 'MQTT console exited with an error. See message above.' }
