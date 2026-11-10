# Run in a logged-in disposable Windows desktop, rather than the SSH session.
param([Parameter(Mandatory=$true)][string]$Application,
      [Parameter(Mandatory=$true)][string]$Results,
      [switch]$SoftwareRendering)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Application).Path
$results = [IO.Path]::GetFullPath($Results)
if (Test-Path -LiteralPath $results) { throw 'Results directory must be new; never reuse a personal profile' }
[IO.Directory]::CreateDirectory($results) | Out-Null
Start-Transcript -Path (Join-Path $results 'test.log') -Force
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class BambuSmokeWindow {
 public delegate bool Callback(IntPtr h, IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback c, IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder b, int n);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$profile = Join-Path $results 'profile'
[IO.Directory]::CreateDirectory($profile) | Out-Null
$config = @{ app=@{language='en_US'; region='North America'}; firstguide=@{finish=$true; privacyuse=$false}; models=@(@{model='Bambu Lab A1';vendor='BBL';nozzle_diameter='0.4'}) } | ConvertTo-Json -Depth 5
# The Windows config loader expects a newline between the final JSON brace and
# its optional checksum. Match the app's on-disk format even without a checksum.
[IO.File]::WriteAllText((Join-Path $profile 'BambuStudio.conf'), $config + "`n", [Text.UTF8Encoding]::new($false))
$process = $null
$failure = $null
$previousPath = $env:PATH
$previousGalliumDriver = $env:GALLIUM_DRIVER
try {
 $appArguments = @('--datadir', ('"'+$profile+'"'))
 if ($SoftwareRendering) {
  # Requires Mesa's x64 DLLs in the application's existing mesa/ fallback path.
  $env:GALLIUM_DRIVER = 'llvmpipe'
  $env:PATH = (Join-Path (Split-Path -Parent $exe) 'mesa') + ';' + $env:PATH
 }
 $process = Start-Process -FilePath $exe -ArgumentList $appArguments -PassThru -RedirectStandardOutput (Join-Path $results 'stdout.log') -RedirectStandardError (Join-Path $results 'stderr.log')
 $script:windows = [Collections.Generic.List[object]]::new()
 $deadline = [DateTime]::UtcNow.AddSeconds(75)
 do {
  Start-Sleep -Seconds 2
  $process.Refresh()
  if ($process.HasExited) { throw "Application exited: $($process.ExitCode)" }
  $script:windows.Clear()
  [BambuSmokeWindow]::EnumWindows({ param($h,$l)
   [uint32]$owner=0
   [BambuSmokeWindow]::GetWindowThreadProcessId($h,[ref]$owner) | Out-Null
   if ($owner -eq $process.Id -and [BambuSmokeWindow]::IsWindowVisible($h)) {
    $b=[Text.StringBuilder]::new(1024); [BambuSmokeWindow]::GetWindowText($h,$b,1024) | Out-Null
    $r=[BambuSmokeWindow+Rect]::new(); [BambuSmokeWindow]::GetWindowRect($h,[ref]$r) | Out-Null
    $script:windows.Add([pscustomobject]@{Handle=$h.ToInt64();Title=$b.ToString();Width=$r.Right-$r.Left;Height=$r.Bottom-$r.Top})
   }
   return $true
  },[IntPtr]::Zero) | Out-Null
  $main=@($script:windows | Where-Object { $_.Title -eq 'Untitled - BambuStudio' -and $_.Width -gt 700 -and $_.Height -gt 400 })
 } while (!$main.Count -and [DateTime]::UtcNow -lt $deadline)
 $script:windows | ConvertTo-Json | Set-Content (Join-Path $results 'windows.json')
 if (!$main.Count) { throw 'No visible Untitled - BambuStudio application main window' }
 Start-Sleep -Seconds 10
 $process.Refresh()
 if ($process.HasExited) { throw 'Application exited after main-window creation' }
} catch {
 $failure = $_
 $_ | Out-String | Set-Content (Join-Path $results 'failure.txt')
} finally {
 try {
 $bounds=[Windows.Forms.Screen]::PrimaryScreen.Bounds
 $bitmap=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
 $graphics=[Drawing.Graphics]::FromImage($bitmap)
 $graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size)
 $bitmap.Save((Join-Path $results 'startup.png'),[Drawing.Imaging.ImageFormat]::Png)
 $graphics.Dispose();$bitmap.Dispose()
 } catch { Write-Warning "Screenshot failed: $_" }
 if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
 $env:PATH = $previousPath
 $env:GALLIUM_DRIVER = $previousGalliumDriver
 Stop-Transcript
}
# Publish completion only after the screenshot and process cleanup, so a task
# runner cannot stop this script while its finally block is still executing.
if ($failure) {
 [IO.File]::WriteAllText((Join-Path $results 'status.txt'),'FAIL '+$failure.Exception.Message)
 throw $failure
}
[IO.File]::WriteAllText((Join-Path $results 'status.txt'),'PASS native Windows full-app main window startup')
Write-Output 'PASS native Windows full-app main window startup'
