param([string]$Repository = (Join-Path $PSScriptRoot '../..'), [switch]$CancelInstaller)
$ErrorActionPreference = 'Stop'
$repositoryPath = (Resolve-Path $Repository).Path
$root = Join-Path ([IO.Path]::GetTempPath()) ('bambu-native-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $root | Out-Null
$fixture = Join-Path $root 'fixture'
$install = Join-Path $root "Bambu [Studio] ' `$ é 漢"
$stage = Join-Path $root "stage [verified] ' `$ é 漢"
New-Item -ItemType Directory $fixture,$install,$stage | Out-Null
Set-Content -LiteralPath (Join-Path $install '.bambu-updater-fixture') -Value fixture
$dataDirectory = (Join-Path $root "profile [saved] ' `$ é 漢") + '\'
[IO.Directory]::CreateDirectory($dataDirectory) | Out-Null
$events = [Collections.Generic.List[string]]::new()
function Assert($condition, [string]$message) { if (!$condition) { throw $message } }
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class BambuNativeWindows {
 public delegate bool Callback(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback c, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
 public static string Text(IntPtr h) { var b=new StringBuilder(1024); GetWindowText(h,b,b.Capacity); return b.ToString(); }
 public static IntPtr Find(string title) { IntPtr found=IntPtr.Zero; EnumWindows((h,l)=>{if(Text(h).Contains(title)) found=h; return true;},IntPtr.Zero); return found; }
}
'@
$exe = Join-Path $fixture 'bambu-studio.exe'
# Compile a real, short-lived Windows program that records its installed launch path.
$program = @'
using System;
using System.IO;
public class BambuFixture {
 public static void Main(string[] args) { File.WriteAllText(System.Reflection.Assembly.GetExecutingAssembly().Location + ".launched", "launched\n" + string.Join("\n", args)); }
}
'@
Add-Type -TypeDefinition $program -OutputAssembly $exe -OutputType WindowsApplication
Set-Content -LiteralPath (Join-Path $fixture 'version.txt') -Value new -Encoding UTF8
Copy-Item -LiteralPath $exe -Destination (Join-Path $install 'bambu-studio.exe')
Set-Content -LiteralPath (Join-Path $install 'version.txt') -Value old -Encoding UTF8
$package = Join-Path $stage 'update.exe'
$makensis = 'C:\msys64\ucrt64\bin\makensis.exe'
& $makensis "/DAPPDIR=$fixture" "/DOUTFILE=$package" (Join-Path $repositoryPath 'tests/application_updater/fixtures/windows.nsi')
Assert ($LASTEXITCODE -eq 0) 'Native NSIS packaging failed'
$unmarked = Join-Path $root 'unmarked destination'
New-Item -ItemType Directory $unmarked | Out-Null
$refused = Start-Process -FilePath $package -ArgumentList ('/S /UPDATE /D=' + $unmarked) -Wait -PassThru
Assert ($refused.ExitCode -eq 2) 'Fixture installer accepted an unmarked destination'
Assert (!(Test-Path -LiteralPath (Join-Path $unmarked 'bambu-studio.exe'))) 'Refused installation wrote application files'
$refused = Start-Process -FilePath $package -ArgumentList ('/S /D=' + $install) -Wait -PassThru
Assert ($refused.ExitCode -eq 2) 'Fixture installer accepted a command without /UPDATE'
Assert ((Get-Content -LiteralPath (Join-Path $install 'version.txt') -Raw).Trim() -eq 'old') 'Refused installation changed existing files'
Write-Output 'PASS fixture installer refuses unmarked destinations and commands without /UPDATE'
$source = [IO.File]::ReadAllText((Join-Path $repositoryPath 'src/slic3r/Utils/DesktopUpdater.cpp'))
$helper = [regex]::Match($source, 'R"PS\(([\s\S]*?)\)PS"').Groups[1].Value
$helperFile = Join-Path $stage 'install.ps1'
[IO.File]::WriteAllText($helperFile, $helper, [Text.UTF8Encoding]::new($true))
# The real helper must wait for an actual process before launching the real installer.
$parent = Start-Process powershell.exe -ArgumentList '-NoProfile -Command Start-Sleep -Seconds 6' -PassThru
$application = Join-Path $install 'bambu-studio.exe'
$command = "& '" + $helperFile.Replace("'", "''") + "' -ParentId " + $parent.Id + " -Package '" + $package.Replace("'", "''") + "' -Application '" + $application.Replace("'", "''") + "'"
$command += " -DataDirectory '" + $dataDirectory.Replace("'", "''") + "'"
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
$failureShown = $false
$process = Start-Process powershell.exe -ArgumentList "-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $encoded" -PassThru
try {
 Start-Sleep -Milliseconds 500
 $parent.Refresh()
 Assert (!$parent.HasExited) 'Fixture parent exited before the wait check'
 Assert ([BambuNativeWindows]::Find('Bambu Studio update test fixture') -eq [IntPtr]::Zero) 'Installer launched while the parent was still running'
 $deadline = [DateTime]::UtcNow.AddSeconds(90)
 do {
  $failure = [BambuNativeWindows]::Find('Bambu Studio update failed')
  if ($failure -ne [IntPtr]::Zero) {
   [BambuNativeWindows]::PostMessage($failure, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
   $failureShown = $true
   if (!$CancelInstaller) { throw 'The production helper displayed its failure dialog' }
  }
  $window = [BambuNativeWindows]::Find('Bambu Studio update test fixture')
  if ($window -ne [IntPtr]::Zero) {
   $id = if ($CancelInstaller) { 2 } else { 1 }
   # NSIS cancellation can open a confirmation dialog with an IDYES button.
   if ($CancelInstaller -and [BambuNativeWindows]::GetDlgItem($window, 6) -ne [IntPtr]::Zero) { $id = 6 }
   $button = [BambuNativeWindows]::GetDlgItem($window, $id)
   if ($button -ne [IntPtr]::Zero -and [BambuNativeWindows]::IsWindowEnabled($button)) {
    $text = [BambuNativeWindows]::Text($button)
    $events.Add($text)
    [BambuNativeWindows]::PostMessage($window, 0x111, [IntPtr]$id, $button) | Out-Null
   }
  }
  Start-Sleep -Milliseconds 400
  $process.Refresh()
 } while (!$process.HasExited -and [DateTime]::UtcNow -lt $deadline)
 Assert $process.HasExited 'Helper did not complete in 90 seconds'
 Assert ($process.ExitCode -eq 0) 'Helper process failed'
 $expected = if ($CancelInstaller) { 'old' } else { 'new' }
 Assert ((Get-Content -LiteralPath (Join-Path $install 'version.txt') -Raw).Trim() -eq $expected) 'Installer did not replace the selected installation'
 Assert (!(Test-Path -LiteralPath $stage)) 'Verified package was not cleaned up'
 $marker = $application + '.launched'
 if ($CancelInstaller) {
  Assert $failureShown 'Cancelled installer did not display the failure message'
  Assert (!(Test-Path -LiteralPath $marker)) 'Cancelled installer relaunched the app'
  Write-Output 'PASS native Windows real installer cancellation preserved the installation, displayed failure, cleaned staging, and did not relaunch'
  return
 }
 for ($i=0; $i -lt 50 -and !(Test-Path -LiteralPath $marker); $i++) { Start-Sleep -Milliseconds 100 }
 Assert ([IO.File]::ReadAllText($marker) -ceq ("launched`n--datadir`n" + $dataDirectory)) 'Updated app did not preserve the exact custom data directory, including its trailing backslash'
 Assert ($events.Count -ge 2) 'Native installer pages were not exercised'
 Write-Output ('PASS native Windows NSIS install, parent wait, Unicode/spaced paths, cleanup, custom profile and relaunch. Buttons: ' + ($events -join ', '))
} finally {
 if (!$process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
 Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
