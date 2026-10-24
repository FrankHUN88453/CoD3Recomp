# Automatic builds. Leave this running (AUTO-BUILD.bat). When an update is
# ready to apply, Claude puts build-request.txt beside this file; this window
# then builds it by itself (BUILD-MULTIPLAYER.ps1, the same as double-clicking
# BUILD-MULTIPLAYER.bat), writes how it went to build-status.txt (Claude
# reads it) and pops up a notice. Nothing builds without a request.
#
# It never builds while CoD3MP.exe or CoD3.exe is running: it waits until
# you quit the game. Close this window to stop it.

$ErrorActionPreference = 'Continue'
$Root = $PSScriptRoot
$Request = Join-Path $Root 'build-request.txt'
$Status = Join-Path $Root 'build-status.txt'
$Log = Join-Path $Root 'build-mp-log.txt'
$BuildLogs = Join-Path $Root 'docs\build-logs'
$Host.UI.RawUI.WindowTitle = 'CoD3 auto-build (leave open)'

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
function Notify([string]$title, [string]$text, [string]$kind = 'Info') {
    try {
        $icon = New-Object System.Windows.Forms.NotifyIcon
        $icon.Icon = [System.Drawing.SystemIcons]::Information
        $icon.Visible = $true
        $icon.ShowBalloonTip(10000, $title, $text, [System.Windows.Forms.ToolTipIcon]::$kind)
        Start-Sleep -Seconds 6
        $icon.Dispose()
    } catch { }
}
function Say([string]$text, [string]$color = 'Gray') { Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $text) -ForegroundColor $color }
function SetStatus([string]$text) { Set-Content -Path $Status -Value ("{0}`r`n{1}" -f (Get-Date -Format 's'), $text) -Encoding ASCII }

Say 'Watching for updates from Claude. Leave this window open (you can minimise it).' 'Green'
SetStatus 'idle: watching for build-request.txt'
$waitingSaid = $false
while ($true) {
    if (Test-Path $Request) {
        $game = Get-Process -Name 'CoD3MP', 'CoD3' -ErrorAction SilentlyContinue
        if ($game) {
            if (-not $waitingSaid) {
                $waitingSaid = $true
                Say 'An update is ready. Waiting for you to quit the game before building...' 'Yellow'
                SetStatus 'waiting: an update is ready; the game is running; builds when it is closed'
                Notify 'CoD3 update ready' 'Quit the game and it will build by itself.'
            }
            Start-Sleep -Seconds 10
            continue
        }
        $waitingSaid = $false
        Start-Sleep -Seconds 15   # let the last files finish copying
        $what = ((Get-Content $Request -Raw -ErrorAction SilentlyContinue) -replace '\s+', ' ').Trim()
        Move-Item $Request (Join-Path $Root 'build-request.taken.txt') -Force
        Say "Building: $what" 'Cyan'
        SetStatus "building: $what"
        Notify 'CoD3 building' 'An update is building (about 5 minutes). Do not start the game yet.'
        $started = Get-Date
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'BUILD-MULTIPLAYER.ps1') *> $Log
        $text = Get-Content $Log -Raw -ErrorAction SilentlyContinue
        $minutes = [math]::Round(((Get-Date) - $started).TotalMinutes, 1)
        $found = Select-String -Path $Log -Pattern 'building version (\S+)' -ErrorAction SilentlyContinue | Select-Object -First 1
        $version = if ($found) { $found.Matches[0].Groups[1].Value } else { '?' }
        if ($text -match 'Done\.') {
            Copy-Item $Log (Join-Path $BuildLogs "build-$version.log") -Force -ErrorAction SilentlyContinue
            Say "Built $version in $minutes min. It is in your game folder." 'Green'
            SetStatus "built: $version OK in $minutes min ($what)"
            Notify "CoD3 $version is ready" 'The new build is in your game folder. Start the game whenever you like.'
        } else {
            $why = ($text -split "`n" | Where-Object { $_ -match 'STOPPED|error' } | Select-Object -First 5) -join ' | '
            Copy-Item $Log (Join-Path $BuildLogs "build-$version-FAILED.log") -Force -ErrorAction SilentlyContinue
            Say "The build FAILED after $minutes min: $why" 'Red'
            SetStatus "failed: $version after $minutes min: $why"
            Notify 'CoD3 build failed' 'Tell Claude: it can read build-status.txt and the log.' 'Error'
        }
        Say 'Watching for the next update.' 'Green'
    }
    Start-Sleep -Seconds 10
}
