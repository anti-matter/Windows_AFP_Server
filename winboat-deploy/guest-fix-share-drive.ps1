# guest-fix-share-drive.ps1 - Self-heal guard for the AFP share drive letter.
# Runs at every logon (scheduled task AfpShareDriveGuard, installed by setup-guest.ps1).
# 1) If the MacFiles volume exists but lost its canonical letter D: (and D: is
#    free), reassign it back to D:.
# 2) If the HKCU share path's drive letter no longer matches where MacFiles
#    actually is, rewrite the path and restart afpServer so shares remount.

$Label     = 'MacFiles'
$WantLetter = 'D'
$RegKey     = 'HKCU:\Software\MacFile\afpserver\volumes\MacintoshHD'

Start-Sleep -Seconds 20   # let hotplug/mount settle after logon

$vol = Get-Volume -FileSystemLabel $Label -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vol) { exit 0 }   # drive absent (e.g. USB unplugged) - nothing to heal

$cur = $vol.DriveLetter

# (1) restore canonical letter if it drifted and D: is free
if ($cur -ne $WantLetter) {
    $dTaken = [System.IO.DriveInfo]::GetDrives() | Where-Object { $_.Name -like "$($WantLetter):*" }
    if (-not $dTaken) {
        try {
            Set-Partition -Partition ($vol | Get-Partition) -NewDriveLetter $WantLetter -ErrorAction Stop
            $cur = $WantLetter
        } catch { }   # e.g. dynamic disk oddities - fall through and just fix the path
    }
}

# (2) make the registry share path match reality; restart server if it changed
$props = Get-ItemProperty -Path $RegKey -ErrorAction SilentlyContinue
if ($props -and $props.path -and $props.path -notlike "$($cur):\*") {
    $new = $props.path -replace '^[A-Za-z]:', "$($cur):"
    Set-ItemProperty -Path $RegKey -Name 'path' -Value $new
    Stop-Process -Name 'afpServer' -Force -ErrorAction SilentlyContinue
    Start-ScheduledTask -TaskName 'PersonalAfpServer' -ErrorAction SilentlyContinue
}

exit 0
