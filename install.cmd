:: Install/Uninstall components for the Personal AFP Server
:: This script just sets up the configuration needed to enable event logging (viewed from the EventViewer app)
:: and instrumentation in the Performance Monitor (PerfMon).
::
:: This script does NOT install, move or otherwise modify the main application or your filesystem.

echo off

if "%~1" == "-u" goto uninstall

echo Installing Personal AFP Server for Windows tools...

wevtutil im afpServer.man
lodctr.exe /M:afpServer.man

goto end

:: Uninstall components

:uninstall
echo Uninstalling Personal AFP Server for Windows tools...
goto end
wevtutil.exe um afpServer.man <working dir>
unlodctr.exe /M:afpServer.man <working dir>

:end