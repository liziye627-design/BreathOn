param([switch]$CheckSourceReceipt)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$evidence = Join-Path $root 'evidence'
$sourceFiles = @(Get-ChildItem (Join-Path $root 'src'), (Join-Path $root 'include'), (Join-Path $root 'sim'), (Join-Path $root 'assets') -File) + @(Get-Item (Join-Path $root 'CMakeLists.txt'))
$sourceTime = ($sourceFiles | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
$frames = @(Get-ChildItem -LiteralPath $evidence -Filter '*.png' | Where-Object Name -Match '^(\d\d-|motion-(home|voice|record|play|level|blink|hop)-)')
if ($frames.Count -ne 102) { throw "Expected 102 frames (72 page states + 30 motion frames), got $($frames.Count)" }
$results = foreach ($frame in $frames) {
    $bytes = [IO.File]::ReadAllBytes($frame.FullName)
    if ([BitConverter]::ToString($bytes, 0, 8) -ne '89-50-4E-47-0D-0A-1A-0A') { throw "Invalid PNG: $($frame.Name)" }
    if ($frame.LastWriteTimeUtc -lt $sourceTime) { throw "Stale PNG: $($frame.Name)" }
    $bitmap = [Drawing.Bitmap]::FromFile($frame.FullName)
    try {
        if ($bitmap.Width -ne 240 -or $bitmap.Height -ne 320) { throw "Wrong dimensions: $($frame.Name)" }
        $colors = New-Object 'System.Collections.Generic.HashSet[int]'
        for ($y = 0; $y -lt 320; $y += 4) {
            for ($x = 0; $x -lt 240; $x += 4) {
                $color = $bitmap.GetPixel($x, $y)
                if ($color.A -ne 255) { throw "Unexpected transparency: $($frame.Name)" }
                [void]$colors.Add($color.ToArgb())
            }
        }
        if ($colors.Count -lt 20) { throw "Possibly empty capture: $($frame.Name)" }
        [pscustomobject]@{frame=$frame.Name; dimensions='240x320'; png=$true; fresh=$true; opaque=$true; sampledColors=$colors.Count}
    } finally { $bitmap.Dispose() }
}
if ($CheckSourceReceipt) {
    $receipt = Get-Content -Raw -Encoding UTF8 (Join-Path $evidence 'product-source-stable.json') | ConvertFrom-Json
    foreach ($entry in $receipt) {
        if ((Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash -ne $entry.Hash) { throw "Product source changed since latest receipt: $($entry.Path)" }
    }
    Write-Output 'PASS: product source hashes match latest handoff.'
}
$results | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $evidence 'capture-validation.json')
Write-Output "PASS: $($frames.Count) fresh opaque 240x320 PNG frames."

