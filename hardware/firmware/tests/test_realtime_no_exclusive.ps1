param([string]$Elf)
$ErrorActionPreference = 'Stop'
$firmwareRoot = Split-Path $PSScriptRoot -Parent

if (-not $Elf) {
    $sources = @(
        (Join-Path $firmwareRoot 'app_breathon_realtime.c'),
        (Join-Path $firmwareRoot 'app_breathon_audio.c')
    )
    $hits = Select-String -Path $sources -Pattern '__atomic_(?:compare_exchange|exchange|fetch_\w+|\w+_fetch)(?:_n)?\b|RT_BOOT'
    if ($hits) {
        $hits | ForEach-Object { Write-Output ('{0}:{1}: {2}' -f $_.Filename, $_.LineNumber, $_.Line.Trim()) }
        throw 'Application RMW/temporary boot tracing remains'
    }
    Write-Output 'Source guard passed: no application atomic RMW or RT_BOOT'
    return
}

$Elf = (Resolve-Path -LiteralPath $Elf).Path
$objdump = (Get-Command arm-none-eabi-objdump -ErrorAction Stop).Source
$headers = & $objdump -h $Elf
if ($LASTEXITCODE -ne 0) { throw 'ELF section inspection failed' }
$sections = @($headers | ForEach-Object {
    if ($_ -match '^\s*\d+\s+(\.text\.(?:app_breathon_(?:audio|realtime)\w*|rt_\w*|audio_task|network_worker|request_transition|request_clear)(?:[.]\S*)?)\s+') {
        $Matches[1]
    }
})
foreach ($required in @('.text.network_worker', '.text.audio_task', '.text.app_breathon_audio_acquire')) {
    if ($sections -notcontains $required) { throw "Missing expected application section: $required" }
}
$dumpArgs = @('-d')
foreach ($section in $sections) { $dumpArgs += @('-j', $section) }
$assembly = & $objdump @dumpArgs $Elf
if ($LASTEXITCODE -ne 0) { throw 'Application disassembly failed' }
$exclusive = $assembly | Select-String '\b(?:ldaex|ldrex|stlex|strex)(?:b|h|d)?\b'
if ($exclusive) {
    $exclusive | ForEach-Object { Write-Output $_.Line }
    throw 'Exclusive instructions remain in application state/worker code'
}
Write-Output "ELF guard passed: $($sections.Count) application sections contain no exclusive instructions"
