# dev-loop.ps1 - AFP server development-loop watcher (runs inside the WinBoat
# Windows guest as scheduled task AfpDevLoop; installed by setup-dev.ps1).
#
# Watches \\host.lan\Data\dev\build.request (written by dev-sync.sh on the Linux
# host). On request: mirror sources to C:\dev\repo (keeping build outputs so
# MSBuild stays incremental), build Release|x86, stage the runnable set into
# C:\dev\server, restart the PersonalAfpServer task, and report the outcome in
# \\host.lan\Data\dev\build.result for the Linux side to pick up.
#
# build.request may carry KEY=VALUE options (one per line):
#   VERSION=x.y.z     product version for the MSI (default 0.0.0)
#   MSI=1             after the exe build, build the WiX MSI installer and
#                     copy it to \\host.lan\Data\dev\out\
#   MSIINSTALL=1      additionally install that MSI in the guest (implies MSI)

$ErrorActionPreference = 'Continue'

$Share     = '\\host.lan\Data\dev'
$Local     = 'C:\dev'
$Repo      = Join-Path $Local 'repo'
$ServerDir = Join-Path $Local 'server'
$Log       = Join-Path $Local 'build.log'

function Write-Result([string]$status, [string]$tail) {
    $body = "STATUS=$status`r`nTIME=$(Get-Date -Format s)`r`n--- build tail ---`r`n$tail`r`n"
    try { Set-Content -Path (Join-Path $Share 'build.result') -Value $body -Encoding UTF8 -Force } catch { }
}

function Tail-File([string]$path, [int]$n) {
    if (Test-Path $path) { (Get-Content $path -Tail $n) -join "`r`n" } else { '(no log)' }
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

while ($true) {
    try {
        $reqFile = Join-Path $Share 'build.request'
        if (Test-Path $reqFile) {
            # parse request options before withdrawing it
            $opt = @{}
            Get-Content $reqFile -EA SilentlyContinue | ForEach-Object {
                if ($_ -match '^\s*([A-Za-z]+)\s*=\s*(.*?)\s*$') { $opt[$matches[1].ToUpper()] = $matches[2] }
            }
            Remove-Item $reqFile -Force

            $ver        = if ($opt['VERSION']) { $opt['VERSION'] } else { '0.0.0' }
            $wantMsi    = $opt['MSI'] -eq '1' -or $opt['MSIINSTALL'] -eq '1'
            $installMsi = $opt['MSIINSTALL'] -eq '1'
            $sw = [System.Diagnostics.Stopwatch]::StartNew()

            # 1) mirror sources; /XD keeps build outputs alive at ANY depth so
            #    MSBuild reuses Release\ obj files (incremental builds).
            #    /IS /IT force-copy even "same/older-stamped" files: the Samba
            #    share occasionally serves stale mtimes to this process, which
            #    otherwise silently skips changed sources (seen 2026-11-05).
            #    /R:1 /W:1 fail fast instead of the /R:1000000 default hang.
            #    Exit code is checked - never build a stale mirror silently.
            robocopy (Join-Path $Share 'repo') $Repo /MIR /NJH /NJS /NDL /NFL `
                /XD .git .vs Release Debug obj x64 ipch PersonalAfpServer `
                /XF build.request build.result PersonalAfpServer.zip `
                /IS /IT /FFT /R:1 /W:1 | Out-Null
            $robo = $LASTEXITCODE
            if ($robo -ge 8) {
                Write-Result 'FAIL' "source mirror failed (robocopy exit $robo) - check \\\\host.lan share"
                continue
            }

            # 2) locate MSBuild
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            $msbuild = $null
            if (Test-Path $vswhere) {
                $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                            -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
            }
            if (-not $msbuild) {
                Write-Result 'FAIL' 'MSBuild not found - install VS Build Tools (rerun setup-dev.ps1).'
                continue
            }

            # 3) build (deploy.cmd is NOT run; we stage manually below)
            # NB: redirect through cmd so msbuild's raw bytes are appended;
            # PowerShell *>> would mix UTF-16 into the UTF-8 log.
            "build started $(Get-Date -Format s) ver=$ver msi=$([int]$wantMsi) msiinstall=$([int]$installMsi)" | Set-Content $Log -Encoding UTF8
            cmd /c "`"$msbuild`" `"$(Join-Path $Repo 'afpServer.sln')`" /t:afpServer /p:Configuration=Release /p:Platform=x86 /m /v:m >> `"$Log`" 2>&1"
            $code = $LASTEXITCODE

            if ($code -ne 0) {
                Write-Result 'FAIL' "msbuild exit $code`r`n$(Tail-File $Log 40)"
                continue
            }

            # 4) stage runnable set
            $stage = Join-Path $Local 'staging'
            New-Item -ItemType Directory -Force -Path $stage | Out-Null
            Copy-Item (Join-Path $Repo 'afpServer\Release\AfpServer.exe') $stage -Force
            $dll = Get-ChildItem (Join-Path $Repo 'afpServer\deps\openssl-1.1') -Recurse -Filter 'libcrypto*.dll' -EA SilentlyContinue | Select-Object -First 1
            if ($dll) { Copy-Item $dll.FullName $stage -Force }

            # 5) swap + restart
            Get-Process -Name afpServer -EA SilentlyContinue | Stop-Process -Force
            Start-Sleep -Seconds 1
            robocopy $stage $ServerDir /MIR /NJH /NJS /NDL /NFL | Out-Null
            Start-ScheduledTask -TaskName 'PersonalAfpServer' -EA SilentlyContinue

            $extra = ''

            # 6) optional: WiX MSI (AfpServerInstaller\product.wxs)
            if ($wantMsi) {
                $wix = Find-Wix
                if (-not $wix) {
                    Write-Result 'FAIL' 'exe built OK, but WiX tool not found - rerun setup-dev.ps1.'
                    continue
                }
                $msiName = "AfpServerInstaller-$ver.msi"
                $msiOut  = Join-Path $Local "out\$msiName"
                New-Item -ItemType Directory -Force -Path (Split-Path $msiOut) | Out-Null
                # cd into the installer dir: product.wxs uses ..\ relative
                # paths, which Windows wix.exe resolves against the CWD.
                $instDir = Join-Path $Repo 'AfpServerInstaller'
                $wixBuild = "cd /d `"$instDir`" && `"$wix`" build -acceptEula wix7 -arch x86 -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext -d ProductVersion=$ver -out `"$msiOut`" product.wxs"
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
                    Write-Result 'FAIL' "wix build exit $LASTEXITCODE`r`n$(Tail-File $Log 40)"
                    continue
                }
                # publish to the Linux-visible out dir
                $outShare = Join-Path $Share 'out'
                New-Item -ItemType Directory -Force -Path $outShare | Out-Null
                Copy-Item $msiOut (Join-Path $outShare $msiName) -Force
                $sha = (Get-FileHash $msiOut -Algorithm SHA256).Hash
                $extra += "`r`nMSI=$msiName`r`nMSI_SHA256=$sha"

                # 7) optional: install it in the guest (MajorUpgrade removes the
                #    previous product automatically)
                if ($installMsi) {
                    $msiLog = Join-Path $Local 'msi-install.log'
                    cmd /c "msiexec /i `"$msiOut`" /qb /l*v `"$msiLog`"" | Out-Null
                    if ($LASTEXITCODE -in 0, 3010) {
                        $extra += "`r`nMSI_INSTALL=OK (exit $LASTEXITCODE)"
                    } else {
                        Write-Result "PASS-MSI-INSTALL-FAIL" "msiexec exit $LASTEXITCODE`r`n$(Tail-File $msiLog 40)$extra"
                        continue
                    }
                }
            }

            Write-Result 'PASS' "built in $([int]$sw.Elapsed.TotalSeconds)s`r`n$(Tail-File $Log 12)$extra"
        }
    } catch {
        try { "$(Get-Date -Format s) $($_.Exception.Message)" | Add-Content (Join-Path $Local 'dev-loop.err.log') } catch { }
    }
    Start-Sleep -Seconds 5
}
