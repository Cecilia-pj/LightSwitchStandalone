<#
.SYNOPSIS
    Compares the vendored upstream files in src/vendor with microsoft/PowerToys.

.DESCRIPTION
    Downloads the current upstream copies of the files listed in UPSTREAM.md into
    tools/upstream-cache, then reports, per file, whether the local vendored copy
    is still identical (modulo the documented ThemeHelper.cpp edits).

    Nothing is modified: this script is read-only apart from filling the cache
    directory. Decide by hand whether an upstream change is worth adopting.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\fetch_upstream.ps1
    powershell -ExecutionPolicy Bypass -File tools\fetch_upstream.ps1 -Ref v0.95.0
#>
[CmdletBinding()]
param(
    [string]$Ref = 'main',
    [string]$Owner = 'microsoft',
    [string]$Repo = 'PowerToys'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$cache = Join-Path $root 'tools\upstream-cache'
$vendor = Join-Path $root 'src\vendor'

if (-not (Test-Path $cache)) {
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
}

# local file name  ->  upstream path
$map = [ordered]@{
    'ThemeHelper.h'                = 'src/modules/LightSwitch/LightSwitchLib/ThemeHelper.h'
    'ThemeHelper.cpp'              = 'src/modules/LightSwitch/LightSwitchLib/ThemeHelper.cpp'
    'ThemeScheduler.h'             = 'src/modules/LightSwitch/LightSwitchService/ThemeScheduler.h'
    'ThemeScheduler.cpp'           = 'src/modules/LightSwitch/LightSwitchService/ThemeScheduler.cpp'
    'LightSwitchUtils.h'           = 'src/modules/LightSwitch/LightSwitchService/LightSwitchUtils.h'
    'NightLightRegistryObserver.h' = 'src/modules/LightSwitch/LightSwitchService/NightLightRegistryObserver.h'
    'SettingsConstants.h'          = 'src/modules/LightSwitch/LightSwitchService/SettingsConstants.h'
}

function Get-UpstreamContent {
    param([string]$Path)

    $url = "https://raw.githubusercontent.com/$Owner/$Repo/$Ref/$Path"
    $bytes = (Invoke-WebRequest -Uri $url -UseBasicParsing).Content
    if ($bytes -is [byte[]]) {
        return [System.Text.Encoding]::UTF8.GetString($bytes)
    }
    return [string]$bytes
}

function Get-NormalizedText {
    param([string]$Text)

    # Normalise line endings and strip a UTF-8 BOM.
    $t = $Text -replace "`r`n", "`n"
    if ($t.StartsWith([char]0xFEFF)) { $t = $t.Substring(1) }
    return $t
}

function Remove-KnownEdits {
    param([string]$Text)

    # Undo the three documented edits to ThemeHelper.cpp so the comparison shows
    # only *unexpected* upstream changes.
    $lines = $Text -split "`n"
    $kept = New-Object System.Collections.Generic.List[string]
    $skipBlock = $false

    foreach ($line in $lines) {
        if ($line -match '^\s*#include\s+"pch\.h"') { continue }
        if ($line -match '^\s*#include\s+<logger/logger\.h>') { continue }
        if ($line -match '^\s*#include\s+"\.\./Logging\.h"') { continue }
        if ($line -match '^\s*//\s*Vendored from microsoft/PowerToys') { $skipBlock = $true; continue }
        if ($line -match '^\s*//\s*Modifications') { $skipBlock = $true; continue }
        if ($skipBlock) {
            if ($line -match '^\s*//') { continue }
            $skipBlock = $false
        }
        if ($line -match 'Logger::info\(L"\[LightSwitchLib\] Reset ColorPrevalence') { continue }

        $kept.Add($line)
    }

    return (($kept -join "`n") -replace "`n{3,}", "`n`n").Trim()
}

$identical = 0
$differing = @()

Write-Host ''
Write-Host "Comparing against $Owner/$Repo@$Ref" -ForegroundColor Cyan
Write-Host ''

foreach ($local in $map.Keys) {
    $upstreamPath = $map[$local]
    $localPath = Join-Path $vendor $local
    $cachePath = Join-Path $cache $local
    $upstreamPathOnDisk = Join-Path $cache (($upstreamPath -replace '/', '_'))

    if (-not (Test-Path $localPath)) {
        Write-Host ("  MISSING   {0}" -f $local) -ForegroundColor Red
        $differing += $local
        continue
    }

    try {
        $remote = Get-NormalizedText (Get-UpstreamContent -Path $upstreamPath)
    }
    catch {
        Write-Host ("  FETCH ERR {0}: {1}" -f $local, $_.Exception.Message) -ForegroundColor Yellow
        continue
    }

    Set-Content -LiteralPath $upstreamPathOnDisk -Value $remote -Encoding UTF8

    $expectedSource = if ($local -eq 'ThemeHelper.cpp') {
        Remove-KnownEdits -Text $remote
    }
    else {
        ($remote -replace "`n+$", '').Trim()
    }

    $localRaw = Get-Content -LiteralPath $localPath -Raw
    $localText = Get-NormalizedText $localRaw
    $localStripped = if ($local -eq 'ThemeHelper.cpp') {
        Remove-KnownEdits -Text $localText
    }
    else {
        $localText.Trim()
    }

    if ($localStripped -eq $expectedSource) {
        Write-Host ("  OK        {0}" -f $local) -ForegroundColor Green
        $identical++
    }
    else {
        Write-Host ("  DIFFERS   {0}" -f $local) -ForegroundColor Yellow
        Write-Host ("            local : {0}" -f $localPath)
        Write-Host ("            remote: {0}" -f $upstreamPathOnDisk)
        $differing += $local
    }
}

Write-Host ''
if ($differing.Count -eq 0) {
    Write-Host ("All {0} vendored files match upstream@{1}." -f $identical, $Ref) -ForegroundColor Green
}
else {
    Write-Host ("{0} file(s) differ: {1}" -f $differing.Count, ($differing -join ', ')) -ForegroundColor Yellow
    Write-Host 'Upstream copies are in tools\upstream-cache\. Run a real diff (VS Code, git diff --no-index)'
    Write-Host 'to decide whether the change is worth adopting, then update UPSTREAM.md.'
}
Write-Host ''
Write-Host 'Reminder: after adopting anything, re-apply the three ThemeHelper.cpp edits'
Write-Host '(drop pch.h, drop <logger/logger.h>, use LOG_INFO instead of Logger::info).'
