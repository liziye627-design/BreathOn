$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$evidencePath = Join-Path (Split-Path $PSScriptRoot -Parent) 'evidence'
$captures = Get-ChildItem -LiteralPath $evidencePath -Filter '*.bmp' | Sort-Object Name
foreach ($capture in $captures) {
    $bitmap = [System.Drawing.Image]::FromFile($capture.FullName)
    try {
        $bitmap.Save([System.IO.Path]::ChangeExtension($capture.FullName, '.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
}

function Save-Sheet([string[]]$Names, [string]$OutputName) {
    $cols = 4
    $rows = [int][Math]::Ceiling($Names.Count / 4.0)
    $sheet = New-Object System.Drawing.Bitmap(1024, ($rows * 360 + 20))
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    $font = New-Object System.Drawing.Font('Segoe UI', 9)
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(23, 59, 87))
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(234, 246, 255))
        for ($i = 0; $i -lt $Names.Count; $i++) {
            $x = 8 + ($i % $cols) * 256
            $y = 12 + [int][Math]::Floor($i / 4) * 360
            $graphics.DrawString($Names[$i], $font, $brush, $x, $y)
            $frame = [System.Drawing.Image]::FromFile((Join-Path $evidencePath ($Names[$i] + '.png')))
            try {
                $rect = New-Object System.Drawing.Rectangle($x, ($y + 24), 240, 320)
                $graphics.DrawImage($frame, $rect, 0, 0, 240, 320, [System.Drawing.GraphicsUnit]::Pixel)
            } finally { $frame.Dispose() }
        }
        $sheet.Save((Join-Path $evidencePath $OutputName), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $brush.Dispose(); $font.Dispose(); $graphics.Dispose(); $sheet.Dispose() }
}
Save-Sheet @('02-home-connected', '51-gas-demo', '43-activity-empty', '52-relation-day', '55-insight-demo', '05-voice-listening', '56-suggestions', '57-water-reminder') 'board-ui-overview.png'
Save-Sheet @('67-demo-home', '68-demo-call', '69-demo-next', '72-demo-device') 'presentation-overview.png'
Save-Sheet @($captures | Where-Object Name -Match '^\d\d-' | ForEach-Object { $_.BaseName }) 'all-states.png'
$pageNames = @($captures | Where-Object Name -Match '^\d\d-' | ForEach-Object { $_.BaseName })
for ($start = 0; $start -lt $pageNames.Count; $start += 16) {
    $last = [Math]::Min($start + 15, $pageNames.Count - 1)
    Save-Sheet @($pageNames[$start..$last]) ('review-states-' + ([int]($start / 16) + 1) + '.png')
}
Save-Sheet @($captures | Where-Object Name -Match '^(1[7-9]|2\d|30)-' | ForEach-Object { $_.BaseName }) 'audio-states.png'
Save-Sheet @($captures | Where-Object Name -Like 'motion-*' | ForEach-Object { $_.BaseName }) 'motion-frames.png'
Save-Sheet @('01-home-unknown', '02-home-connected', '03-home-pressed', '04-voice-idle', '34-voice-start-pending', '35-voice-error', '36-home-stop-pending', '37-home-external-idle', '38-voice-handler-removed', '39-home-handler-removed', '40-voice-programmatic', '41-voice-reconnect') 'realtime-states.png'
Write-Output "Exported $($captures.Count) native 240x320 captures and five overview sheets; AUDIO is internal maintenance only."

