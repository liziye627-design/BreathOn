$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $projectRoot
try {
    & (Join-Path $PSScriptRoot 'test_realtime_no_exclusive.ps1')
    & (Join-Path $PSScriptRoot 'test_realtime_mic_jitter.ps1')
    gcc -std=c11 -Wall -Wextra -Werror -Ifirmware/tests/test_realtime_stubs -Ifirmware firmware/tests/test_realtime_pacing.c firmware/app_breathon_realtime_playback.c firmware/realtime_wire.c -o firmware/tests/test_realtime_pacing.exe
    if ($LASTEXITCODE -ne 0) { throw 'Playback pacing test compile failed' }
    & firmware/tests/test_realtime_pacing.exe
    if ($LASTEXITCODE -ne 0) { throw 'Playback pacing test failed' }
    gcc -std=c11 -Wall -Wextra -Werror -Ifirmware firmware/tests/test_realtime.c firmware/realtime_wire.c -o firmware/tests/test_realtime.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test compile failed' }
    & firmware/tests/test_realtime.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test failed' }

    # Compile the actual pinned SDK decoder and memory datasink unchanged.
    # Vendor warnings are left visible; application code below uses -Werror.
    gcc -std=c11 -Wall -Wextra -Ifirmware/tests/test_realtime_stubs -Ifirmware -Ivendor/TuyaOpen-official/src/audio_player/src/decoder -Ivendor/TuyaOpen-official/src/audio_player/src/datasink firmware/tests/test_realtime_wav.c firmware/realtime_wire.c vendor/TuyaOpen-official/src/audio_player/src/decoder/decoder_wav.c vendor/TuyaOpen-official/src/audio_player/src/datasink/datasink_mem.c -o firmware/tests/test_realtime_wav.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDK WAV test compile failed' }
    & firmware/tests/test_realtime_wav.exe
    if ($LASTEXITCODE -ne 0) { throw 'SDK WAV test failed' }

    $sources = Get-ChildItem -Path firmware/app_breathon_realtime*.c | ForEach-Object { $_.FullName }
    gcc -std=c11 -Wall -Wextra -Werror -Ifirmware/tests/test_realtime_stubs -Ifirmware -Ivendor/TuyaOpen-official/src/libcjson/cJSON firmware/tests/test_realtime_runtime.c @sources firmware/realtime_wire.c firmware/realtime_request.c firmware/app_breathon_audio.c firmware/audio_capture.c vendor/TuyaOpen-official/src/libcjson/cJSON/cJSON.c -lws2_32 -o firmware/tests/test_realtime_runtime.exe
    if ($LASTEXITCODE -ne 0) { throw 'TCP worker test compile failed' }
    & firmware/tests/test_realtime_runtime.exe
    if ($LASTEXITCODE -ne 0) { throw 'TCP worker test failed' }
    & firmware/tests/test_realtime_runtime.exe --boot
    if ($LASTEXITCODE -ne 0) { throw 'Persisted configuration boot test failed' }
    & firmware/tests/test_realtime_runtime.exe --sync
    if ($LASTEXITCODE -ne 0) { throw 'Request/stop/ownership synchronization test failed' }
} finally {
    Pop-Location
}
