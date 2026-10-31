$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$testExecutable = Join-Path $env:TEMP ('breathon-netcfg-lifecycle-' + [guid]::NewGuid().ToString('N') + '.exe')
$receipt = Join-Path $projectRoot 'evidence/netcfg-1.0.20-r3-tests.log'
Push-Location $projectRoot
try {
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath 'vendor/TuyaOpen-official/src/tuya_cloud_service/netcfg/netcfg.c').Hash
    @('Netcfg 1.0.20 r3: real netcfg.c/netcfg.h + real SDK tuya_list.c/tuya_list.h', "netcfg.c SHA256: $sourceHash") | Set-Content -Encoding UTF8 -LiteralPath $receipt
    gcc -std=c11 -Wall -Wextra -Werror -Ifirmware/tests/netcfg_lifecycle_stubs -Ivendor/TuyaOpen-official/src/tuya_cloud_service/netcfg -Ivendor/TuyaOpen-official/tools/porting/adapter/utilities/include -Ivendor/TuyaOpen-official/src/common/include firmware/tests/test_netcfg_lifecycle.c vendor/TuyaOpen-official/src/tuya_cloud_service/netcfg/netcfg.c vendor/TuyaOpen-official/tools/porting/adapter/utilities/src/tuya_list.c -o $testExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Real netcfg host test compile failed' }
    $cases = @('dequeued-after-stop', 'dequeued-after-reinit', 'queued-stale-drain', 'inflight-start-stop', 'session-allocation-failure', 'handler-allocation-failure', 'job-allocation-failure', 'job-schedule-failure', 'job-lock-failure', 'module-start-failure', 'handler-removed')
    foreach ($case in $cases) {
        $output = & $testExecutable $case
        $testExit = $LASTEXITCODE
        $output | Add-Content -Encoding UTF8 -LiteralPath $receipt
        Write-Output $output
        if ($testExit -ne 0) { throw "Real netcfg lifecycle test failed: $case (exit $testExit)" }
    }
    $summary = "TOTAL PASS: $($cases.Count); GCC -std=c11 -Wall -Wextra -Werror; host only"
    $summary | Add-Content -Encoding UTF8 -LiteralPath $receipt
    Write-Output $summary
} catch {
    $_.ToString() | Add-Content -Encoding UTF8 -LiteralPath $receipt
    throw
} finally {
    Pop-Location
    if (Test-Path -LiteralPath $testExecutable) { Remove-Item -LiteralPath $testExecutable }
}
