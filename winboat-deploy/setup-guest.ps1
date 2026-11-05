# setup-guest.ps1 - One-shot setup for Personal AFP Server inside the WinBoat Windows guest.
# Run FROM AN ELEVATED PowerShell:  powershell -ExecutionPolicy Bypass -File setup-guest.ps1
# (copy this file and AfpServerInstaller.msi out of the mapped Linux home drive first)

$ErrorActionPreference = 'Stop'

Write-Host '=== 1/6 AFP server firewall rule (TCP 548) ==='
netsh advfirewall firewall delete rule name="AFP Server (TCP 548)" | Out-Null
netsh advfirewall firewall add rule name="AFP Server (TCP 548)" dir=in action=allow protocol=TCP localport=548

Write-Host '=== 2/6 Locate MacFiles USB drive by volume label ==='
$vol = Get-Volume -FileSystemLabel 'MacFiles' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vol) {
    Write-Host "ERROR: No mounted volume with label 'MacFiles'. Is the USB drive passed through and mounted?"
    exit 1
}
$dl = ($vol.DriveLetter + ':\')
Write-Host "Found MacFiles at $dl"

Write-Host '=== 3/6 Create share folders on USB drive ==='
$shareRoot = Join-Path $dl 'Shares'
$share1 = Join-Path $shareRoot 'MacintoshHD'
New-Item -ItemType Directory -Force -Path $share1 | Out-Null
Write-Host "Share root:  $shareRoot"
Write-Host "Share 1:     $share1  (will appear as AFP volume 'MacintoshHD')"

Write-Host '=== 4/6 Install VC++ x86 runtime + AfpServerInstaller.msi (silent) ==='
# The MSI does not bundle the MSVC runtime; a clean Windows image needs the x86
# redist or afpServer.exe fails to start with a missing-vcruntime error.
if (-not (Test-Path 'C:\Windows\SysWOW64\vcruntime140.dll')) {
    Write-Host '  vcruntime140 (x86) missing - downloading VC++ 2015-2022 redist (x86)...'
    $rd = Join-Path $env:TEMP 'vc_redist.x86.exe'
    Invoke-WebRequest 'https://aka.ms/vs/17/release/vc_redist.x86.exe' -OutFile $rd
    $p = Start-Process $rd -ArgumentList '/install','/quiet','/norestart' -Wait -PassThru
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 1638 -and $p.ExitCode -ne 3010) {
        Write-Host "  WARNING: vc_redist exit code $($p.ExitCode) - continuing anyway"
    }
} else {
    Write-Host '  vcruntime140 (x86) already present'
}
$msi = Join-Path $PSScriptRoot 'AfpServerInstaller.msi'
if (Test-Path $msi) {
    $p = Start-Process msiexec.exe -ArgumentList "/i `"$msi`" /qn" -Wait -PassThru
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { Write-Host "MSI install failed, exit $($p.ExitCode)"; exit 1 }
} else {
    Write-Host 'MSI not found next to script - skipping install (assumed already installed).'
}

# Locate AfpServer.exe
$exe = Get-ChildItem 'C:\Program Files (x86)','C:\Program Files' -Filter AfpServer.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) { Write-Host 'ERROR: AfpServer.exe not found after install.'; exit 1 }
Write-Host "AfpServer.exe: $($exe.FullName)"

Write-Host '=== 5/6 Pre-seed HKCU share config + autostart ==='
$regBase = 'HKCU:\Software\MacFile\afpserver'
New-Item -Force -Path $regBase | Out-Null
$volKey = Join-Path $regBase 'volumes\MacintoshHD'
New-Item -Force -Path $volKey | Out-Null
New-ItemProperty -Force -Path $volKey -Name 'path' -Value $share1 -PropertyType String | Out-Null

# Autostart via scheduled task at logon, highest privileges (satisfies requireAdministrator).
$action = New-ScheduledTaskAction -Execute $exe.FullName -WorkingDirectory $exe.DirectoryName
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
Register-ScheduledTask -TaskName 'PersonalAfpServer' -Action $action -Trigger $trigger -Principal $principal -Force | Out-Null

Write-Host '=== 6/6 Drive-letter self-heal guard ==='
$guardDir = 'C:\ProgramData\AfpServerGuard'
$guardSrc = Join-Path $PSScriptRoot 'guest-fix-share-drive.ps1'
if (Test-Path $guardSrc) {
    New-Item -ItemType Directory -Force -Path $guardDir | Out-Null
    Copy-Item $guardSrc (Join-Path $guardDir 'guest-fix-share-drive.ps1') -Force
    $ga = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$guardDir\guest-fix-share-drive.ps1`""
    Register-ScheduledTask -TaskName 'AfpShareDriveGuard' -Action $ga -Trigger $trigger -Principal $principal -Force | Out-Null
    Write-Host "  guard installed: AfpShareDriveGuard (re-asserts D: and fixes share paths at logon)"
} else {
    Write-Host "  NOTE: guest-fix-share-drive.ps1 not found next to this script - guard not installed"
}

Write-Host ''
Write-Host 'DONE. Start the server now with:  schtasks /Run /TN PersonalAfpServer'
Write-Host 'Verify listen socket:  netstat -an | findstr :548'
Write-Host 'NOTE: create additional AFP users as local Windows accounts (Computer Management -> Local Users and Groups);'
Write-Host '      Mac AFP clients log in with those credentials. Enable guest access via the server UI if wanted.'
