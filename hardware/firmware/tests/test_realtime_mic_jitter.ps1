$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testExecutable = Join-Path ([System.IO.Path]::GetTempPath()) ('breathon-mic-jitter-' + [guid]::NewGuid().ToString('N') + '.exe')
Push-Location $projectRoot
try {
    # Keep the real capture callback and network loop; discard unused SDK boot paths.
    gcc -std=c11 -Wall -Wextra -Werror -O2 -flto -fwhole-program -Ifirmware/tests/test_realtime_stubs -Ifirmware -Ivendor/TuyaOpen-official/src/libcjson/cJSON firmware/tests/test_realtime_mic_jitter.c firmware/app_breathon_realtime.c firmware/realtime_wire.c -lws2_32 -o $testExecutable
    if ($LASTEXITCODE -ne 0) { throw 'MIC jitter test compile failed' }
    & $testExecutable
    if ($LASTEXITCODE -ne 0) { throw 'MIC jitter test failed' }
} finally {
    Pop-Location
    if (Test-Path -LiteralPath $testExecutable) { Remove-Item -LiteralPath $testExecutable }
}
