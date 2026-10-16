param([string]$Executable = (Join-Path $PSScriptRoot '..\dist\Film-Index-Studio-1.0.4\FilmIndexStudio.exe'))
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$directory = Split-Path $exe -Parent
if (Get-Process FilmIndexStudio -ErrorAction SilentlyContinue) { throw 'Close Film Index Studio before running this integration test.' }
$checkRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('film-index-instance-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $checkRoot | Out-Null
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FilmIndexWindowProbe {
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
}
'@
$owned = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
function Start-Primary {
    $process = Start-Process -FilePath $exe -WorkingDirectory $directory -WindowStyle Hidden -PassThru
    $owned.Add($process)
    if ($process.WaitForExit(700)) { throw 'Primary instance exited unexpectedly.' }
    return $process
}
function Assert-SecondaryExits {
    param([System.Diagnostics.Process]$Primary)
    $sentinel = Join-Path $checkRoot ([guid]::NewGuid().ToString('N'))
    $process = Start-Process -FilePath $exe -WorkingDirectory $directory -ArgumentList @('--smoke-test', ('"' + $sentinel + '"')) -WindowStyle Hidden -PassThru
    $owned.Add($process)
    if (!$process.WaitForExit(5000)) { throw 'Secondary instance stayed alive.' }
    if ($process.ExitCode -ne 0) { throw 'Secondary instance failed.' }
    if (Test-Path -LiteralPath $sentinel) { throw 'Secondary instance entered the rendering workflow.' }
    $Primary.Refresh()
    if ($Primary.HasExited) { throw 'Primary instance was closed by a secondary launch.' }
}
function Wait-VisibleWindow {
    param([System.Diagnostics.Process]$Process)
    for ($i=0; $i -lt 50; $i++) {
        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) { return }
        Start-Sleep -Milliseconds 100
    }
    throw 'Existing window was not activated.'
}
try {
    $primary = Start-Primary
    Assert-SecondaryExits $primary
    Wait-VisibleWindow $primary
    [FilmIndexWindowProbe]::ShowWindow($primary.MainWindowHandle, 6) | Out-Null
    Assert-SecondaryExits $primary
    for ($i=0; $i -lt 50 -and [FilmIndexWindowProbe]::IsIconic($primary.MainWindowHandle); $i++) { Start-Sleep -Milliseconds 100 }
    if ([FilmIndexWindowProbe]::IsIconic($primary.MainWindowHandle)) { throw 'Minimized window was not restored.' }
    $primary.CloseMainWindow() | Out-Null
    if (!$primary.WaitForExit(5000)) { throw 'Normal exit did not finish.' }
    $afterClose = Start-Primary
    Assert-SecondaryExits $afterClose
    # Only terminate the empty process started by this test; verify OS lock cleanup after abrupt exit.
    $afterClose.Kill()
    $afterClose.WaitForExit()
    $afterCrash = Start-Primary
    Assert-SecondaryExits $afterCrash
    Wait-VisibleWindow $afterCrash
    $afterCrash.CloseMainWindow() | Out-Null
    if (!$afterCrash.WaitForExit(5000)) { throw 'Post-crash instance did not close.' }
    'PASS: single instance, activation, minimized restore, normal restart and abrupt-exit recovery.'
} finally {
    foreach ($process in $owned) {
        if (!$process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            if (!$process.WaitForExit(3000)) { $process.Kill(); $process.WaitForExit() }
        }
        $process.Dispose()
    }
}
