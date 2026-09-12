param([ValidateSet('car_s3', 'remote_p4', 'both')][string]$Board = 'both')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $env:IDF_PATH) {
    $profilePath = 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
    if (-not (Test-Path -LiteralPath $profilePath)) { throw 'Activate ESP-IDF 5.5.x before running this script.' }
    . $profilePath
}
$boards = if ($Board -eq 'both') { @('car_s3', 'remote_p4') } else { @($Board) }
foreach ($boardName in $boards) {
    Push-Location (Join-Path $projectRoot $boardName)
    try {
        & idf.py build
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $boardName" }
    } finally { Pop-Location }
}
