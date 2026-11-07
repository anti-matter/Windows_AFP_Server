# guest-build.ps1 - one-shot AFP dev build inside the Windows guest, driven
# over SSH by ssh-dev.sh (installed to C:\dev by that script). Replaces the
# old \\host.lan\Data polling watcher (dev-loop.ps1 / AfpDevLoop).
#
# Expects the source tree already extracted at C:\dev\repo (ssh-dev.sh pipes a
# tarball and uploads C:\dev\repo.manifest). Build outputs (.vs, Release, obj,
# ...) live inside the tree and are never deleted, so MSBuild stays incremental.
#
# Prints the same KEY=VALUE markers the old build.result used, so the Linux
# side can grep them off the SSH stdout stream.

param(
    [string]$Version = '0.0.0',
    [switch]$Msi,
    [switch]$MsiInstall,
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Continue'

$Local     = 'C:\dev'
$Repo      = Join-Path $Local 'repo'
$ServerDir = Join-Path $Local 'server'
$Log       = Join-Path $Local 'build.log'

function Tail-File([string]$path, [int]$n) {
    if (Test-Path $path) { (Get-Content $path -Tail $n) -join "`r`n" } else { '(no log)' }
}

function Fail([string]$msg) {
    Write-Output "STATUS=FAIL"
    Write-Output $msg
    exit 1
}

function Find-Wix {
    foreach ($c in @((Join-Path $Local 'tools\wix.exe'),
                     (Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe'))) {
        if (Test-Path $c) { return $c }
    }
    $g = Get-Command wix.exe -EA SilentlyContinue
    if ($g) { return $g.Source }
    return $null
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

# 0) prune sources deleted on the host. The tar-over-ssh sync only adds and
#    overwrites; repo.manifest (forward-slash relative paths) is the truth.
#    Build outputs at any depth are exempt, exactly like the old robocopy /XD.
$manifest = Join-Path $Local 'repo.manifest'
if (Test-Path $manifest) {
    $keep = @{}
    Get-Content $manifest | ForEach-Object {
        $p = $_.Trim().Replace('\','/')
        if ($p) { $keep[$p.TrimStart('/')] = 1 }
    }
    if ($keep.Count -gt 0) {
        $skipDirs = @('.vs','Release','Debug','obj','x64','ipch','PersonalAfpServer','out')
        $repoFull = (Resolve-Path $Repo).Path.TrimEnd('\')
        Get-ChildItem $Repo -Recurse -File -Force -EA SilentlyContinue | ForEach-Object {
            $rel = $_.FullName.Substring($repoFull.Length + 1).Replace('\','/')
            # exempt build outputs at any depth (same list as the old robocopy /XD)
            $parts = $rel -split '/'
            if (($parts | Where-Object { $skipDirs -contains $_ }).Count -gt 0) { return }
            if (-not $keep.ContainsKey($rel)) { Remove-Item $_.FullName -Force -EA SilentlyContinue }
        }
        # remove emptied directories (keep build-output trees)
        Get-ChildItem $Repo -Recurse -Directory -Force -EA SilentlyContinue |
            Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
                $parts = $_.FullName.Replace('\','/') -split '/'
                if (($parts | Where-Object { $skipDirs -contains $_ }).Count -gt 0) { return }
                if (-not (Get-ChildItem $_.FullName -Force -EA SilentlyContinue)) {
                    Remove-Item $_.FullName -Force -Recurse -EA SilentlyContinue
                }
            }
    }
}

# 1) locate MSBuild
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild) { Fail 'MSBuild not found - install VS Build Tools (rerun setup-dev.ps1).' }

# 2) build (deploy.cmd is NOT run; we stage manually below)
# NB: redirect through cmd so msbuild's raw bytes are appended; PowerShell *>>
# would mix UTF-16 into the UTF-8 log.
"build started $(Get-Date -Format s) ver=$Version cfg=$Configuration msi=$([int][bool]$Msi) msiinstall=$([int][bool]$MsiInstall)" | Set-Content $Log -Encoding UTF8
cmd /c "`"$msbuild`" `"$(Join-Path $Repo 'afpServer.sln')`" /t:afpServer /p:Configuration=$Configuration /p:Platform=x64 /m /v:m >> `"$Log`" 2>&1"
$code = $LASTEXITCODE
if ($code -ne 0) { Fail "msbuild exit $code`r`n$(Tail-File $Log 40)" }

# 3) stage runnable set
$stage = Join-Path $Local 'staging'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $Repo "afpServer\$Configuration\AfpServer.exe") $stage -Force
$dll = Get-ChildItem (Join-Path $Repo 'afpServer\deps\openssl-1.1') -Recurse -Filter 'libcrypto*.dll' -EA SilentlyContinue | Select-Object -First 1
if ($dll) { Copy-Item $dll.FullName $stage -Force }

# 4) swap + restart
Get-Process -Name afpServer -EA SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
robocopy $stage $ServerDir /MIR /NJH /NJS /NDL /NFL | Out-Null
schtasks /End /TN PersonalAfpServer 2>$null | Out-Null
Start-Sleep -Seconds 1
schtasks /Run /TN PersonalAfpServer 2>$null | Out-Null
# The autostart task is InteractiveToken; with nobody logged on at the console
# (headless boot) /Run silently no-ops, and anything launched from this SSH
# session is killed when its channel closes (sshd job). A one-shot S4U task is
# launched by Task Scheduler instead: survives the SSH session, loads the
# same HKCU profile. (Verified 2026-11-06; guest auto-logon is the primary
# path again once the console session is back.)
function Test-ServerUp([int]$tries) {
    foreach ($try in 1..$tries) {
        Start-Sleep -Seconds 1
        if (Get-Process -Name afpServer -EA SilentlyContinue) { return $true }
    }
    return $false
}
if (Test-ServerUp 5) {
    $restartNote = 'server restarted (PersonalAfpServer task)'
} else {
    $sid = (whoami /user | findstr /I S-1-5-21) -replace '.*?(S-1-5-21[0-9-]+).*','$1'
    $a = New-ScheduledTaskAction -Execute (Join-Path $ServerDir 'AfpServer.exe') -WorkingDirectory $ServerDir
    $p = New-ScheduledTaskPrincipal -UserId $sid.Trim() -LogonType S4U -RunLevel Highest
    # End any stale instance first: Register -Force and Start both stall while
    # Task Scheduler still tracks a previous instance as Running, which pushes
    # the spawn past a short wait (seen 2026-11-06).
    schtasks /End /TN AfpDevStart 2>$null | Out-Null
    Register-ScheduledTask -TaskName 'AfpDevStart' -Action $a -Principal $p -Force -EA SilentlyContinue | Out-Null
    Start-ScheduledTask -TaskName 'AfpDevStart' -EA SilentlyContinue
    if (Test-ServerUp 15) { $restartNote = 'server restarted (S4U fallback task - no console session)' }
    else                  { $restartNote = 'WARNING: server did not start - check PersonalAfpServer task' }
}

$extra = ''

# 5) optional: WiX MSI (AfpServerInstaller\product.wxs)
if ($Msi) {
    $wix = Find-Wix
    if (-not $wix) { Fail 'exe built OK, but WiX tool not found - rerun setup-dev.ps1.' }
    $msiName = "AfpServerInstaller-$Version.msi"
    $msiOut  = Join-Path $Local "out\$msiName"
    New-Item -ItemType Directory -Force -Path (Split-Path $msiOut) | Out-Null
    # cd into the installer dir: product.wxs uses ..\ relative paths, which
    # Windows wix.exe resolves against the CWD.
    $instDir = Join-Path $Repo 'AfpServerInstaller'
    $wixBuild = "cd /d `"$instDir`" && `"$wix`" build -acceptEula wix7 -arch x64 -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext -d ProductVersion=$Version -out `"$msiOut`" product.wxs"
    cmd /c "$wixBuild >> `"$Log`" 2>&1"
    if (($LASTEXITCODE -ne 0 -or -not (Test-Path $msiOut)) -and
        ((Tail-File $Log 10) -match 'WIX0144')) {
        # extensions not installed yet - acquire from nuget.org, retry once
        "installing WiX extensions..." | Add-Content $Log
        cmd /c "`"$wix`" extension add -g WixToolset.UI.wixext -acceptEula wix7 >> `"$Log`" 2>&1"
        cmd /c "`"$wix`" extension add -g WixToolset.Util.wixext -acceptEula wix7 >> `"$Log`" 2>&1"
        cmd /c "$wixBuild >> `"$Log`" 2>&1"
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $msiOut)) {
        Fail "wix build exit $LASTEXITCODE`r`n$(Tail-File $Log 40)"
    }
    $sha = (Get-FileHash $msiOut -Algorithm SHA256).Hash
    $extra += "`r`nMSI=$msiName`r`nMSI_SHA256=$sha"

    # 6) optional: install it in the guest (MajorUpgrade removes the previous
    #    product automatically)
    if ($MsiInstall) {
        $msiLog = Join-Path $Local 'msi-install.log'
        cmd /c "msiexec /i `"$msiOut`" /qb /l*v `"$msiLog`"" | Out-Null
        if ($LASTEXITCODE -in 0, 3010) {
            $extra += "`r`nMSI_INSTALL=OK (exit $LASTEXITCODE)"
        } else {
            Fail "MSI built OK but guest install failed: msiexec exit $LASTEXITCODE`r`n$(Tail-File $msiLog 40)$extra"
        }
    }
}

Write-Output 'STATUS=PASS'
Write-Output "built in $([int]$sw.Elapsed.TotalSeconds)s"
Write-Output $restartNote
Write-Output (Tail-File $Log 12)
Write-Output $extra
