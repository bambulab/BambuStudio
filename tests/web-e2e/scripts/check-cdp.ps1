<#
.SYNOPSIS
    P0 verification: confirm we can attach Playwright to BambuStudio's
    embedded WebView2 over the Chrome DevTools Protocol without changing
    any C++ code.

.DESCRIPTION
    1. Sets WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS so the next WebView2 the
       process spawns will listen on a debug port.
    2. Spawns BambuStudio.exe.
    3. Waits for the developer to log in and open the Filament Manager
       (any page that lives inside a WebView2 will do; the manager is the
       canonical target).
    4. Polls http://127.0.0.1:<port>/json/version and lists the page
       endpoints.

    A successful run prints "P0 OK" and a list of WebView2 pages with their
    URLs and titles, including at least one entry that loads the device
    page bundle.  That is the green light to proceed with the full e2e
    framework without touching the C++ webview creation code.

.PARAMETER Port
    CDP debug port to use.  Defaults to STUDIO_E2E_CDP_PORT or 9222.

.PARAMETER StudioBin
    Absolute path to bambu-studio.exe.  Defaults to STUDIO_E2E_BIN or the
    repo's Release build output.

.PARAMETER LaunchStudio
    When set ($true is default), the script spawns BambuStudio for you.
    Pass -LaunchStudio:$false if you want to start it manually (useful
    when debugging argument injection).

.PARAMETER Mode
    Three-stage execution control:
      full     (default) — set env, launch Studio, wait for Enter, then probe.
      launch              — set env, launch Studio, return immediately. No
                            Read-Host, no probe.  Use when running from a
                            non-interactive shell (e.g. Cursor's Shell tool).
      probe               — assume BambuStudio is already running with the
                            env var.  Skip launch and Read-Host; jump
                            straight to /json/version + /json/list checks.

.NOTES
    Run from the tests/web-e2e directory:
        pnpm e2e:check-cdp
    Or directly:
        powershell -ExecutionPolicy Bypass -File ./scripts/check-cdp.ps1
        powershell -ExecutionPolicy Bypass -File ./scripts/check-cdp.ps1 -Mode launch
        powershell -ExecutionPolicy Bypass -File ./scripts/check-cdp.ps1 -Mode probe
#>

[CmdletBinding()]
param(
    [int]    $Port         = 0,
    [string] $StudioBin    = $null,
    [bool]   $LaunchStudio = $true,
    [ValidateSet('full', 'launch', 'probe')]
    [string] $Mode         = 'full'
)

$ErrorActionPreference = 'Stop'

# Resolve effective parameters from env if not passed.
if ($Port -eq 0) {
    $envPort = $env:STUDIO_E2E_CDP_PORT
    if ([string]::IsNullOrWhiteSpace($envPort)) { $Port = 9222 }
    else { $Port = [int]$envPort }
}
if ([string]::IsNullOrWhiteSpace($StudioBin)) {
    $StudioBin = $env:STUDIO_E2E_BIN
    if ([string]::IsNullOrWhiteSpace($StudioBin)) {
        $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
        $StudioBin = Join-Path $repoRoot 'build_release\src\Release\bambu-studio.exe'
    }
}

function Write-Section {
    param([string] $Title)
    Write-Host ''
    Write-Host ('=' * 70) -ForegroundColor DarkGray
    Write-Host (" $Title") -ForegroundColor Cyan
    Write-Host ('=' * 70) -ForegroundColor DarkGray
}

function Test-Port-Open {
    param([int] $P)
    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $iar = $client.BeginConnect('127.0.0.1', $P, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne(500, $false)
        if ($ok -and $client.Connected) { $client.Close(); return $true }
        $client.Close(); return $false
    } catch { return $false }
}

# -------------------------------------------------------------------- 1. Banner
$bannerTitle = switch ($Mode) {
    'launch' { 'P0 - launch only (no probe, no wait)' }
    'probe'  { 'P0 - probe only (assume Studio already running)' }
    default  { 'P0 - WebView2 CDP smoke check' }
}
Write-Section $bannerTitle
Write-Host "  bambu-studio.exe : $StudioBin"
Write-Host "  CDP port         : $Port"
Write-Host "  mode             : $Mode"

if ($Mode -ne 'probe') {
    if (-not (Test-Path $StudioBin)) {
        Write-Host ''
        Write-Host "[FAIL] bambu-studio.exe not found at the path above." -ForegroundColor Red
        Write-Host "       Build the project in Release first, or set STUDIO_E2E_BIN" -ForegroundColor Red
        Write-Host "       in tests/web-e2e/.env.local to override the location." -ForegroundColor Red
        exit 2
    }

    # ------------------------------------------------------- 2. Inject env var
    # This is the documented Microsoft mechanism.  Any WebView2 the process
    # spawns AFTER this variable is set will read it from the environment block.
    $cdpArgs = "--remote-debugging-port=$Port --remote-allow-origins=*"
    $env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = $cdpArgs
    Write-Host ''
    Write-Host "[step 1/4] Set WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS" -ForegroundColor Green
    Write-Host "           = $cdpArgs"

    # Sanity: nothing else listening on this port already.
    if (Test-Port-Open -P $Port) {
        Write-Host ''
        Write-Host "[FAIL] Port $Port is already in use before BambuStudio starts." -ForegroundColor Red
        Write-Host "       Pick another with -Port, or stop the process holding it." -ForegroundColor Red
        exit 3
    }

    # ----------------------------------------------------- 3. Spawn / wait
    if ($LaunchStudio) {
        Write-Host ''
        Write-Host '[step 2/4] Starting BambuStudio.exe ...' -ForegroundColor Green
        $proc = Start-Process -FilePath $StudioBin -PassThru -WorkingDirectory (Split-Path -Parent $StudioBin)
        Write-Host "           PID = $($proc.Id)"
    } else {
        Write-Host ''
        Write-Host '[step 2/4] Manual launch mode' -ForegroundColor Yellow
        Write-Host '           This shell has WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS exported.'
        Write-Host '           Start BambuStudio.exe FROM THIS SHELL only - opening it from'
        Write-Host '           Explorer will spawn a fresh process without the env var.'
    }

    if ($Mode -eq 'launch') {
        Write-Host ''
        Write-Host '[launch] BambuStudio is starting in the background.' -ForegroundColor Green
        Write-Host '         Log in, then Device -> Filament Manager.'
        Write-Host "         When ready, run probe in this same workspace:"
        Write-Host "           powershell -ExecutionPolicy Bypass -File ./scripts/check-cdp.ps1 -Mode probe -Port $Port" -ForegroundColor Cyan
        exit 0
    }

    Write-Host ''
    Write-Host '[step 3/4] Drive BambuStudio:' -ForegroundColor Green
    Write-Host '             1. Log in (your dev account)'
    Write-Host '             2. Top menu --> Device --> Filament Manager'
    Write-Host '             3. Wait until the filament list page is visible'
    Write-Host '             4. Press Enter here to continue'
    [void](Read-Host '          (press Enter when ready)')
} else {
    Write-Host ''
    Write-Host '[probe] Skipping launch + Read-Host; assuming BambuStudio is up.' -ForegroundColor Yellow
}

# ----------------------------------------------------------- 4. Probe CDP
Write-Section "Probing http://127.0.0.1:$Port/json/version"
$ok = $false
for ($i = 0; $i -lt 10; $i++) {
    if (Test-Port-Open -P $Port) { $ok = $true; break }
    Start-Sleep -Milliseconds 500
}
if (-not $ok) {
    Write-Host ''
    Write-Host "[FAIL] No process listening on 127.0.0.1:$Port." -ForegroundColor Red
    Write-Host "       This usually means BambuStudio overrides" -ForegroundColor Red
    Write-Host "       WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS internally, or starts" -ForegroundColor Red
    Write-Host "       the WebView2 in a child process that doesn't inherit env." -ForegroundColor Red
    Write-Host "       Fallback: small (1-line) C++ patch in the webview creator." -ForegroundColor Red
    exit 4
}

try {
    $version = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/json/version" -TimeoutSec 5
} catch {
    Write-Host "[FAIL] Port open but /json/version failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 5
}

Write-Host ''
Write-Host '[step 4/4] CDP endpoint reachable. Browser banner:' -ForegroundColor Green
$version | ConvertTo-Json -Depth 4 | Write-Host

# Enumerate pages so we can confirm the filament manager DOM is exposed.
try {
    $pages = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/json/list" -TimeoutSec 5
} catch {
    Write-Host "[WARN] /json/list failed: $($_.Exception.Message)" -ForegroundColor Yellow
    $pages = @()
}

Write-Section "WebView2 pages discovered ($($pages.Count) total)"
if ($pages.Count -eq 0) {
    Write-Host '[WARN] No pages discovered. Did you actually open the Filament Manager?' -ForegroundColor Yellow
} else {
    $pages | ForEach-Object {
        Write-Host ''
        Write-Host ('  type:  {0}' -f $_.type)
        Write-Host ('  title: {0}' -f $_.title)
        Write-Host ('  url:   {0}' -f $_.url)
    }
}

# ----------------------------------------------------------- 5. Verdict
Write-Section 'Verdict'
$looksLikeFilament = $pages | Where-Object {
    ($_.type -eq 'page') -and (
        ($_.url -match 'device_page') -or
        ($_.url -match 'fila') -or
        ($_.title -match 'Filament') -or
        ($_.title -match '耗材')
    )
}

if ($looksLikeFilament) {
    Write-Host '[PASS] P0 OK - WebView2 exposes a CDP page that looks like the' -ForegroundColor Green
    Write-Host '              filament manager.  Proceed to building the framework' -ForegroundColor Green
    Write-Host '              without touching C++.' -ForegroundColor Green
    exit 0
} else {
    Write-Host '[PARTIAL] CDP works, but no page URL/title matches the filament' -ForegroundColor Yellow
    Write-Host '          manager heuristics (device_page / fila / Filament / 耗材).' -ForegroundColor Yellow
    Write-Host '          Confirm you are actually on the manager screen, then re-run.' -ForegroundColor Yellow
    Write-Host '          If the page list above is correct but heuristics missed it,' -ForegroundColor Yellow
    Write-Host '          adjust the URL match in src/helpers/cdp-attach.ts later.' -ForegroundColor Yellow
    exit 1
}
