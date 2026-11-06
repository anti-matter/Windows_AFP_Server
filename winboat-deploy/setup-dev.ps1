# setup-dev.ps1 - One-time setup of the Linux->VM development loop, run inside
# the WinBoat Windows guest FROM AN ELEVATED PowerShell:
#
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   & '\\host.lan\Data\setup-dev.ps1'
#
# Does: installs VS Build Tools (Desktop C++ workload) and the WiX toolset
# (.NET SDK + "wix" global tool) silently, stages C:\dev\server from the
# current production install, points the PersonalAfpServer autostart task at
# the dev build, and registers the AfpDevLoop watcher (starts at logon;
# watches \\host.lan\Data\dev).
#
#   & '\\host.lan\Data\setup-dev.ps1' -Undo    # revert to production install
#
param(
    [switch]$Undo,
    # VS 2026 (v18) uses the "stable" channel - NOT "release" (that aka.ms link
    # does not exist and silently resolves to a Bing HTML page). Verified 2026-11-05:
    #   aka.ms/vs/18/stable/vs_buildtools.exe -> download.visualstudio.microsoft.com/.../vs_BuildTools.exe
    [string]$BootstrapperUrl = 'https://aka.ms/vs/18/stable/vs_buildtools.exe'
)
$ErrorActionPreference = 'Stop'

$DevDir     = 'C:\dev'
$ServerDir  = Join-Path $DevDir 'server'
$LoopSrc    = Join-Path $PSScriptRoot 'dev-loop.ps1'
$LoopDst    = Join-Path $DevDir 'dev-loop.ps1'
$ProdPathFile = Join-Path $DevDir 'prod-task-path.txt'
$VsWhere    = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

function New-HighestPrincipal {
    New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
}

if ($Undo) {
    Write-Host '=== Undo: back to production build ==='
    Unregister-ScheduledTask -TaskName 'AfpDevLoop' -Confirm:$false -EA SilentlyContinue
    if (Test-Path $ProdPathFile) {
        $prod = (Get-Content $ProdPathFile -First 1).Trim()
        $action = New-ScheduledTaskAction -Execute $prod -WorkingDirectory (Split-Path $prod)
        Set-ScheduledTask -TaskName 'PersonalAfpServer' -Action $action | Out-Null
        Get-Process -Name afpServer -EA SilentlyContinue | Stop-Process -Force
        Start-ScheduledTask -TaskName 'PersonalAfpServer'
        Write-Host "PersonalAfpServer task repointed at production: $prod"
    }
    Write-Host 'Done. C:\dev kept for reference; delete manually if desired.'
    exit 0
}

Write-Host '=== 1/5 VS Build Tools (Desktop C++ workload) ==='
if (Test-Path $VsWhere) {
    $found = & $VsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($found) { Write-Host "  MSBuild present: $found" }
}
if (-not $found) {
    $boot = Join-Path $env:TEMP 'vs_buildtools.exe'
    Write-Host "  downloading bootstrapper: $BootstrapperUrl"
    try {
        if ($BootstrapperUrl -like 'file://*') {
            Copy-Item ($BootstrapperUrl -replace '^file://','') $boot -Force
        } else {
            Invoke-WebRequest $BootstrapperUrl -OutFile $boot
        }
    } catch {
        Write-Host "  ERROR: could not fetch bootstrapper ($_)`n  Get 'Build Tools for Visual Studio' manually, then rerun with -BootstrapperUrl file:///C:/path/vs_buildtools.exe"
        exit 1
    }
    Write-Host '  installing (silent, 10-30 min)...'
    # ATL/ATLMFC: kept as belt-and-suspenders. (afpserver.rc used to need
    # afxres.h from these; it now uses the SDK's winres.h, but nothing else
    # here hurts.)
    # Sanity-check the download: aka.ms dead links resolve to HTML, and
    # Start-Process on such a file fails with "corrupted and unreadable".
    $head = [System.IO.File]::OpenRead($boot); $b = New-Object byte[] 2
    [void]$head.Read($b, 0, 2); $head.Close()
    $sz = (Get-Item $boot).Length
    if ($b[0] -ne 0x4D -or $b[1] -ne 0x5A -or $sz -lt 500KB) {
        Write-Host "  ERROR: downloaded file is not a PE executable (magic=$([char[]]$b -join '') size=$sz bytes)."
        Write-Host "  The URL likely redirected to an error page. Get 'Build Tools for Visual Studio 2026'"
        Write-Host "  from visualstudio.microsoft.com/downloads and rerun with:"
        Write-Host "    & '\\host.lan\Data\setup-dev.ps1' -BootstrapperUrl 'file:///C:/path/vs_buildtools.exe'"
        exit 1
    }
    $p = Start-Process $boot -ArgumentList '--quiet','--wait','--norestart','--nocache',
            '--add','Microsoft.VisualStudio.Workload.VCTools;includeRecommended',
            '--add','Microsoft.VisualStudio.Component.VC.ATL',
            '--add','Microsoft.VisualStudio.Component.VC.ATLMFC' -Wait -PassThru
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010 -and $p.ExitCode -ne 1638) {
        Write-Host "  ERROR: build tools install exit $($p.ExitCode) - see %TEMP%\\vs_bootstrapper*.log"; exit 1
    }
}

Write-Host '=== 2/5 WiX toolset (open-source MSI builder, dotnet global tool) ==='
$WixExe = Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe'
if (Test-Path $WixExe) {
    Write-Host "  already installed: $WixExe"
} else {
    # `dotnet tool install` requires the SDK (runtime-only is not enough).
    $dotnet = Get-Command dotnet -EA SilentlyContinue
    if (-not $dotnet) {
        Write-Host '  installing .NET SDK (LTS) via dotnet-install.ps1 (~5 min)...'
        $di = Join-Path $env:TEMP 'dotnet-install.ps1'
        # Source ladder: a copy staged next to this script on the share costs
        # zero network; then curl.exe (native winsock DNS - the .NET Framework
        # resolver in PS 5.1 has failed to resolve the dot.net shortlink in
        # this guest); then plain Invoke-WebRequest as last resort.
        $local = Join-Path $PSScriptRoot 'dotnet-install.ps1'
        if (Test-Path $local) {
            Copy-Item $local $di -Force
        } elseif ((Get-Command curl.exe -EA SilentlyContinue) -and
                 (curl.exe -fsSL https://dot.net/v1/dotnet-install.ps1 -o $di -w '%{http_code}' 2>$null) -eq '200') {
            # fetched via curl
        } else {
            Invoke-WebRequest 'https://dot.net/v1/dotnet-install.ps1' -OutFile $di
        }
        & $di -Channel LTS
        if ($LASTEXITCODE -ne 0) { Write-Host '  ERROR: .NET SDK install failed'; exit 1 }
        $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
                    [Environment]::GetEnvironmentVariable('Path','User')
    }
    Write-Host '  dotnet tool install --global wix ...'
    & dotnet tool install --global wix
    if (-not (Test-Path $WixExe)) { Write-Host "  ERROR: $WixExe not found after install"; exit 1 }
}
# WiX v7+ requires an explicit EULA acceptance (free for organizations under
# $10k annual revenue); the dev-loop watcher also passes -acceptEula wix7.
try { & $WixExe eula accept wix7 2>$null | Out-Null } catch { }
# WiX v7 no longer auto-fetches -ext extensions; product.wxs needs UI + Util.
# (dev-loop.ps1 self-heals this too, but doing it here keeps first builds clean.)
foreach ($ext in 'WixToolset.UI.wixext', 'WixToolset.Util.wixext') {
    if (-not ((& $WixExe extension list -g 2>$null | Out-String) -match ([regex]::Escape($ext)))) {
        Write-Host "  wix extension add -g $ext ..."
        & $WixExe extension add -g $ext -acceptEula wix7
    }
}
Write-Host "  wix ready: $WixExe"

Write-Host '=== 3/5 stage C:\dev\server from production install ==='
New-Item -ItemType Directory -Force -Path $ServerDir | Out-Null
$prodExe = Get-ChildItem 'C:\Program Files (x86)','C:\Program Files' -Filter AfpServer.exe -Recurse -EA SilentlyContinue | Select-Object -First 1
if ($prodExe) {
    robocopy $prodExe.DirectoryName $ServerDir /E /NJH /NJS /NDL | Out-Null
} else {
    Write-Host '  (no production install found - dev build will create it on first success)'
}

Write-Host '=== 4/5 watcher + task repoint ==='
if (-not (Test-Path $LoopSrc)) { Write-Host "ERROR: dev-loop.ps1 must sit next to this script ($LoopSrc)"; exit 1 }
Copy-Item $LoopSrc $LoopDst -Force
$task = Get-ScheduledTask -TaskName 'PersonalAfpServer' -EA SilentlyContinue
if ($task) {
    $cur = $task.Actions[0].Execute
    if (-not (Test-Path $ProdPathFile)) { Set-Content $ProdPathFile $cur }
    $action = New-ScheduledTaskAction -Execute (Join-Path $ServerDir 'AfpServer.exe') -WorkingDirectory $ServerDir
    Set-ScheduledTask -TaskName 'PersonalAfpServer' -Action $action | Out-Null
    Write-Host "  PersonalAfpServer -> $ServerDir\AfpServer.exe (production path saved)"
}
$loopAction = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$LoopDst`""
Stop-ScheduledTask -TaskName 'AfpDevLoop' -EA SilentlyContinue   # kill live instance (holds old script in memory)
Register-ScheduledTask -TaskName 'AfpDevLoop' -Action $loopAction `
    -Trigger (New-ScheduledTaskTrigger -AtLogOn) -Principal (New-HighestPrincipal) -Force | Out-Null
Get-ScheduledTask -TaskName 'AfpDevLoop' | Start-ScheduledTask

Write-Host @"
=== 5/5 done ===
Dev loop is live. On the Linux host:
    ~/afp-deploy/dev-sync.sh              # edit -> MSBuild -> swap -> server restart -> result
    ~/afp-deploy/dev-sync.sh --msi        # ... also builds the WiX MSI -> ~/afp-deploy/dev/out/
    ~/afp-deploy/dev-sync.sh --msi-install # ... and installs it in the guest (major upgrade)
Revert anytime (elevated, guest):  & '\\host.lan\Data\setup-dev.ps1' -Undo
"@
