# Launch the fork with a separate profile and child environment.
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Executable,
    [string]$DataDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Executable) {
    $Executable = Join-Path $root 'build\src\RelWithDebInfo\bambu-studio.exe'
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Application not found: $Executable. Build and stage the runtime first."
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $DataDirectory) {
    $DataDirectory = Join-Path $root '.bambu-open\profile'
}
$DataDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DataDirectory)
if ($DataDirectory.Contains('"')) { throw 'The data directory cannot contain a double quote.' }
if (-not $PSCmdlet.ShouldProcess($Executable, "Launch with separate profile $DataDirectory")) {
    return
}

$roaming = Join-Path $DataDirectory 'Roaming'
$local = Join-Path $DataDirectory 'Local'
New-Item -ItemType Directory -Force -Path $DataDirectory, $roaming, $local | Out-Null
$previousAppData = $env:APPDATA
$previousLocalAppData = $env:LOCALAPPDATA
try {
    $env:APPDATA = $roaming
    $env:LOCALAPPDATA = $local
    $arguments = '--no-single-instance --datadir "' + $DataDirectory + '"'
    $process = Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable) `
        -ArgumentList $arguments -PassThru
    Write-Host "Started Bambu Open Studio (PID $($process.Id)). Profile: $DataDirectory"
} finally {
    $env:APPDATA = $previousAppData
    $env:LOCALAPPDATA = $previousLocalAppData
}
