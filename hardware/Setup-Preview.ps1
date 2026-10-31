param()
$ErrorActionPreference = 'Stop'
$lvgl = Join-Path $PSScriptRoot 'vendor/lvgl-native'
$commit = 'e1c0b21b2723d391b885de4b2ee5cc997eccca91'
if (-not (Test-Path -LiteralPath $lvgl)) {
    New-Item -ItemType Directory -Force -Path $lvgl | Out-Null
    git -C $lvgl init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'LVGL init failed.' }
    git -C $lvgl remote add origin https://github.com/lvgl/lvgl.git
    if ($LASTEXITCODE -ne 0) { throw 'LVGL remote setup failed.' }
    git -C $lvgl fetch --depth 1 origin $commit
    if ($LASTEXITCODE -ne 0) { throw 'LVGL download failed.' }
    git -C $lvgl checkout --detach $commit
    if ($LASTEXITCODE -ne 0) { throw 'LVGL checkout failed.' }
}
$actual = git -C $lvgl rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual -ne $commit) { throw 'Unexpected LVGL version; use a separate clean checkout.' }
$fonts = @(
    @{
        Path = 'breathon-ui/assets/font_puhui_18_2.c'
        Url = 'https://raw.githubusercontent.com/NiceMCU-01/tuyaopen_nicmcu_t5/5424591cc7fb8bcfaef4ddeb5caac1f67625c4b5/apps/tuya.ai/ai_components/ai_ui/font/font_puhui_18_2.c'
        Hash = 'DA313731AF7FC04EE17C05CCC7CE579BC2CE859F2A9263519D68D4211C663B0C'
    },
    @{
        Path = 'breathon-ui/src/ui_font_small.c'
        Url = 'https://raw.githubusercontent.com/tuya/TuyaOpen/695e571fb4f62932dbf86a86fde664fcb7f2c0b4/src/ai_components/ai_ui/font/font_puhui_14_1.c'
        Hash = '76AF005ADFBB2394D1A327115E498642B2EBC0ABA7512B2F7B10DD698279963C'
    }
)
foreach ($font in $fonts) {
    $target = Join-Path $PSScriptRoot $font.Path
    if (-not (Test-Path -LiteralPath $target)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Invoke-WebRequest -UseBasicParsing -Uri $font.Url -OutFile $target -TimeoutSec 60
    }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $font.Hash) {
        throw ('Font checksum mismatch: ' + $font.Path)
    }
}
Write-Output 'Pinned preview dependencies verified. Read upstream licenses before redistribution.'
