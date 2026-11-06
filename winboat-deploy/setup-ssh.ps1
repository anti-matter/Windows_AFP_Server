# setup-ssh.ps1 - one-time SSH access setup for the WinBoat Windows guest,
# so the Linux host can drive builds/debugging over OpenSSH instead of the
# \\host.lan\Data SMB polling loop. Run INSIDE the guest from an elevated
# PowerShell (ssh-dev.sh can also run this remotely via password auth):
#
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   & '\\host.lan\Data\setup-ssh.ps1'
#
# Does:
#   * installs the OpenSSH Server feature if absent, sets sshd automatic, starts it
#   * ensures the firewall rule for TCP 22
#   * installs the Linux host's public key (host_id_ed25519.pub, expected next
#     to this script) into administrators_authorized_keys with the strict ACL
#     sshd demands (no inheritance, only SYSTEM + Administrators)
#   * retires the old AfpDevLoop share-watcher task (SSH replaces it; the
#     build itself is now C:\dev\guest-build.ps1, invoked over SSH)
#
param(
    [string]$PubKeyFile = (Join-Path $PSScriptRoot 'host_id_ed25519.pub'),
    [switch]$KeepDevLoop
)
$ErrorActionPreference = 'Stop'

Write-Host '=== 1/4 OpenSSH server ==='
if (-not (Get-Service sshd -EA SilentlyContinue)) {
    Write-Host '  installing OpenSSH.Server capability...'
    Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0 | Out-Null
}
Set-Service sshd -StartupType Automatic
if ((Get-Service sshd).Status -ne 'Running') { Start-Service sshd }
Write-Host "  sshd: $((Get-Service sshd).Status), $((Get-Service sshd).StartType)"

if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -EA SilentlyContinue)) {
    New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (in)' `
        -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
}

Write-Host '=== 2/4 host key -> administrators_authorized_keys ==='
if (-not (Test-Path $PubKeyFile)) {
    Write-Host "ERROR: public key not found: $PubKeyFile"
    Write-Host 'Copy the Linux host''s ~/.ssh/id_ed25519.pub to the share next to this script.'
    exit 1
}
$pubkey = (Get-Content $PubKeyFile -First 1).Trim()
if ($pubkey -notmatch '^ssh-(ed25519|rsa|ecdsa)\s') { Write-Host "ERROR: $PubKeyFile does not look like a public key"; exit 1 }
$ak = 'C:\ProgramData\ssh\administrators_authorized_keys'
$lines = @()
if (Test-Path $ak) { $lines = @(Get-Content $ak | Where-Object { $_.Trim() }) }
$fp = ($pubkey -split '\s+')[1]
if (($lines | ForEach-Object { ($_ -split '\s+')[1] }) -contains $fp) {
    Write-Host '  key already installed'
} else {
    $lines += $pubkey
    Set-Content -Path $ak -Value $lines -Encoding ASCII
    Write-Host '  key added'
}
# sshd refuses this file unless inheritance is off and only SYSTEM/Administrators have access
icacls $ak /inheritance:r /grant 'NT AUTHORITY\SYSTEM:F' /grant 'BUILTIN\Administrators:F' | Out-Null
Write-Host "  ACL set: $(icacls $ak | Select-Object -Last 1)"

Write-Host '=== 3/4 retire the SMB polling watcher ==='
if (-not $KeepDevLoop) {
    Unregister-ScheduledTask -TaskName 'AfpDevLoop' -Confirm:$false -EA SilentlyContinue
    Write-Host '  AfpDevLoop unregistered (builds now run via C:\dev\guest-build.ps1 over SSH)'
} else {
    Write-Host '  keeping AfpDevLoop as requested'
}

Write-Host '=== 4/4 done ==='
Write-Host 'From the Linux host:  ssh -p 2222 <guest-user>@127.0.0.1'
Write-Host 'Then:                 winboat-deploy/ssh-dev.sh   # build loop over SSH'
