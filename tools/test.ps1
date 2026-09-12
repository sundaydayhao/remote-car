param([string]$Compiler = 'cc')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testOut = Join-Path $projectRoot 'tmp\tests'
New-Item -ItemType Directory -Path $testOut -Force | Out-Null
$executable = Join-Path $testOut 'test_core.exe'
& $Compiler '-std=c99' '-Wall' '-Wextra' '-I' (Join-Path $projectRoot 'components\rc_common\include') (Join-Path $projectRoot 'components\rc_common\rc_protocol.c') (Join-Path $projectRoot 'tests\test_core.c') '-o' $executable
if ($LASTEXITCODE -ne 0) { throw 'Host C compilation failed' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
