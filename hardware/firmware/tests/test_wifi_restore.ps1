param([switch]$Presentation)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testExecutable = Join-Path $env:TEMP ('breathon-wifi-restore-' + [guid]::NewGuid().ToString('N') + '.exe')
Push-Location $projectRoot
try {
    gcc -std=c11 -Wall -Wextra -Werror ("-DBREATHON_PRESENTATION_ONLY=" + [int][bool]$Presentation) -Ifirmware/tests/wifi_restore_stubs -Ifirmware -Ivendor/TuyaOpen-official/src/libcjson/cJSON firmware/tests/test_wifi_restore.c firmware/app_breathon_wifi.c firmware/app_breathon_wifi_static.c firmware/realtime_wire.c vendor/TuyaOpen-official/src/libcjson/cJSON/cJSON.c -o $testExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi restore host test compile failed' }
    $cases = @('unbound', 'tuya', 'tuya-after-marker', 'mismatched-saved-ssid', 'requested-other', 'requested-same', 'connecting', 'linked', 'missing-netinfo', 'non-string-password', 'missing-password', 'empty-ssid', 'long-ssid', 'long-password', 'trailing-junk', 'duplicate-key', 'encoded-nul', 'embedded-nul', 'invalid-scope', 'other-static-ssid', 'tuya-static-ssid', 'nested-json', 'escaped-password', 'open-network', 'trailing-space')
    $cases += 'reconfigure-during-stop'
    if ($Presentation) { $cases = @('explicit-presentation','explicit-no-static','explicit-invalid-static','explicit-mismatch','explicit-empty','explicit-long','explicit-nul','explicit-read-error','explicit-requested-same','explicit-reconfigure-during-stop') }
    foreach ($case in $cases) {
        & $testExecutable $case
        if ($LASTEXITCODE -ne 0) { throw "Wi-Fi restore host test failed: $case" }
    }
} finally {
    Pop-Location
    if (Test-Path -LiteralPath $testExecutable) { Remove-Item -LiteralPath $testExecutable }
}
