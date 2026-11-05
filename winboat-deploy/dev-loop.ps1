# dev-loop.ps1 - AFP server development-loop watcher (runs inside the WinBoat
# Windows guest as scheduled task AfpDevLoop; installed by setup-dev.ps1).
#
# Watches \\host.lan\Data\dev\build.request (written by dev-sync.sh on the Linux
# host). On request: mirror sources to C:\dev\repo (keeping build outputs so
# MSBuild stays incremental), build Release|x86, stage the runnable set into
# C:\dev\server, restart the PersonalAfpServer task, and report the outcome in
# \\host.lan\Data\dev\build.result for the Linux side to pick up.

$ErrorActionPreference = 'Continue'

$Share     = '\\host.lan\Data\dev'
$Local     = 'C:\dev'
$Repo      = Join-Path $Local 'repo'
$ServerDir = Join-Path $Local 'server'
$Log       = Join-Path $Local 'build.log'

function Write-Result([string]$status, [string]$tail) {
    $body = "STATUS=$status`r`nTIME=$(Get-Date -Format s)`r`n--- msbuild tail ---`r`n$tail`r`n"
    try { Set-Content -Path (Join-Path $Share 'build.result') -Value $body -Encoding UTF8 -Force } catch { }
}

function Tail-File([string]$path, [int]$n) {
    if (Test-Path $path) { (Get-Content $path -Tail $n) -join "`r`n" } else { '(no log)' }
}

while ($true) {
    try {
        if (Test-Path (Join-Path $Share 'build.request')) {
            Remove-Item (Join-Path $Share 'build.request') -Force
            $sw = [System.Diagnostics.Stopwatch]::StartNew()

            # 1) mirror sources; /XD keeps build outputs alive at ANY depth so
            #    MSBuild reuses Release\ obj files (incremental builds).
            robocopy (Join-Path $Share 'repo') $Repo /MIR /NJH /NJS /NDL /NFL `
                /XD .git .vs Release Debug obj x64 ipch /XF build.request build.result | Out-Null

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
            "build started $(Get-Date -Format s)" | Set-Content $Log -Encoding UTF8
            cmd /c "`"$msbuild`" `"$(Join-Path $Repo 'afpServer.sln')`" /t:afpServer /p:Configuration=Release /p:Platform=x86 /m /v:m >> `"$Log`" 2>&1"
            $code = $LASTEXITCODE

            if ($code -eq 0) {
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
                Write-Result 'PASS' "built in $([int]$sw.Elapsed.TotalSeconds)s`r`n$(Tail-File $Log 12)"
            } else {
                Write-Result 'FAIL' "msbuild exit $code`r`n$(Tail-File $Log 40)"
            }
        }
    } catch {
        try { "$(Get-Date -Format s) $($_.Exception.Message)" | Add-Content (Join-Path $Local 'dev-loop.err.log') } catch { }
    }
    Start-Sleep -Seconds 5
}
