$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testExecutable = Join-Path ([System.IO.Path]::GetTempPath()) ('breathon-network-diag-' + [guid]::NewGuid().ToString('N') + '.exe')
Push-Location $projectRoot
try {
    gcc -std=c11 -Wall -Wextra -Werror -Ifirmware/tests/test_realtime_stubs -Ifirmware -Ivendor/TuyaOpen-official/src/libcjson/cJSON firmware/tests/test_realtime_network_diag.c firmware/app_breathon_realtime_control.c firmware/realtime_wire.c vendor/TuyaOpen-official/src/libcjson/cJSON/cJSON.c -lws2_32 -o $testExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Network diagnostic test compile failed' }
    & $testExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Network diagnostic test failed' }
} finally {
    Pop-Location
    if (Test-Path -LiteralPath $testExecutable) { Remove-Item -LiteralPath $testExecutable }
}
