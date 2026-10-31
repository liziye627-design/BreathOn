$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$evidencePath = Join-Path (Split-Path $PSScriptRoot -Parent) 'evidence'
$outputPath = Join-Path $evidencePath 'rotom-ui'
$names = @('idle', 'listening', 'thinking', 'speaking')
for ($state = 0; $state -lt 4; $state++) {
    & ffmpeg -hide_banner -loglevel error -y -framerate 25 -i (Join-Path $outputPath "loop-$state-%03d.bmp") -filter_complex '[0:v]split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=none' -loop 0 (Join-Path $outputPath ($names[$state] + '.gif'))
    if ($LASTEXITCODE -ne 0) { throw 'GIF export failed.' }
    $bitmap = [Drawing.Bitmap]::FromFile((Join-Path $outputPath "loop-$state-000.bmp"))
    try { $bitmap.Save((Join-Path $outputPath ($names[$state] + '.png')), [Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
}
foreach ($name in @('board-ui-overview.png', 'all-states.png', 'audio-states.png', 'motion-frames.png', 'motion-report.json')) {
    Copy-Item -LiteralPath (Join-Path $evidencePath $name) -Destination $outputPath
}
Write-Output "Exported native LVGL animations: idle 12s, three voice states 6s each, to $outputPath"
