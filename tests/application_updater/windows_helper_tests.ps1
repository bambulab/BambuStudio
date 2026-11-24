param([string]$Repository = (Join-Path $PSScriptRoot '../..'))
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Join-Path $Repository 'src/slic3r/Utils/DesktopUpdater.cpp'))
$match = [regex]::Match($source, 'R"PS\(([\s\S]*?)\)PS"')
if (!$match.Success) { throw 'Windows helper source missing' }
$helper = $match.Groups[1].Value.Replace("`r`n", "`n")
$tokens = $null; $parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseInput($helper, [ref]$tokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
# Substitute only the GUI alert, so tests can run headlessly on any OS.
$start = $helper.IndexOf('    Add-Type -AssemblyName PresentationFramework')
$end = $helper.IndexOf(' | Out-Null', $start) + ' | Out-Null'.Length
if ($start -lt 0 -or $end -lt $start) { throw 'Alert substitution missing' }
$helper = $helper.Substring(0, $start) + '    $script:Alerts.Add($_.Exception.Message)' + $helper.Substring($end)
$script:Application = Join-Path ([IO.Path]::GetTempPath()) "Bambu [Studio] ' `$ é/bambu-studio.exe"
$script:Package = Join-Path ([IO.Path]::GetTempPath()) "Bambu staged ' `$ é/update.exe"
function Assert($Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function Get-Process([int]$Id, [string]$ErrorAction) {
    Assert ($Id -eq 12345) 'Wrong parent PID'
    if ($script:ParentPresent) { [pscustomobject]@{Id=$Id} }
}
function Wait-Process {
    param([Parameter(ValueFromPipeline=$true)]$InputObject, [int]$Timeout)
    process {
        Assert ($InputObject.Id -eq 12345 -and $Timeout -eq 120) 'Wrong wait target or timeout'
        $script:Events.Add('wait')
        if ($script:WaitFails) { throw 'Parent did not exit' }
    }
}
function Start-Process([string]$FilePath, [string]$ArgumentList, [switch]$Wait, [switch]$PassThru) {
    if ($Wait) {
        Assert ($FilePath -ceq $script:Package) 'Wrong installer path'
        Assert $PassThru 'Installer exit status must be observed'
        Assert ($ArgumentList -ceq ('/UPDATE /D=' + [IO.Path]::GetDirectoryName($script:Application))) 'Installation directory was changed or misquoted'
        if ($script:ParentPresent) { Assert ($script:Events.Contains('wait')) 'Installer launched before parent exit' }
        $script:Events.Add('install')
        return [pscustomobject]@{ExitCode=$script:InstallerExitCode}
    }
    Assert ($FilePath -ceq $script:Application) 'Wrong app path'
    Assert ($script:InstallerExitCode -eq 0) 'App restarted after failed installation'
    Assert ($ArgumentList -ceq $script:ExpectedArguments) 'Custom data directory was dropped or misquoted'
    $script:Events.Add('launch')
}
function Remove-Item([string]$LiteralPath, [switch]$Recurse, [switch]$Force, [string]$ErrorAction) {
    Assert ($LiteralPath -ceq [IO.Path]::GetDirectoryName($script:Package)) 'Cleanup escaped the staging directory'
    Assert ($Recurse -and $Force) 'Cleanup flags missing'
    $script:Events.Add('cleanup')
}
foreach ($case in 'success', 'already-exited', 'installer-cancelled', 'parent-timeout') {
    foreach ($customProfile in $false, $true) {
        $script:DataDirectory = if ($customProfile) { 'C:\Bambu profile [saved] '' $ é 漢\' } else { '' }
        $script:ExpectedArguments = if ($customProfile) { '--datadir "C:\Bambu profile [saved] '' $ é 漢\\"' } else { '' }
        $script:Events = [Collections.Generic.List[string]]::new()
        $script:Alerts = [Collections.Generic.List[string]]::new()
        $script:ParentPresent = $case -ne 'already-exited'
        $script:WaitFails = $case -eq 'parent-timeout'
        $script:InstallerExitCode = if ($case -eq 'installer-cancelled') { 1 } else { 0 }
        & ([scriptblock]::Create($helper)) -ParentId 12345 -Package $script:Package -Application $script:Application -DataDirectory $script:DataDirectory
        Assert ($script:Events.Contains('cleanup')) "$case did not clean up"
        if ($case -eq 'success' -or $case -eq 'already-exited') {
            Assert ($script:Events.Contains('launch') -and $script:Alerts.Count -eq 0) "$case failed to restart"
        } else {
            Assert (!$script:Events.Contains('launch') -and $script:Alerts.Count -eq 1) "$case did not report failure"
        }
        if ($case -eq 'parent-timeout') { Assert (!$script:Events.Contains('install')) 'Installed while app was running' }
    }
}
Write-Output 'Windows helper: syntax and 8 process/installer scenarios passed.'
