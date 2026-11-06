:: Install/Uninstall components for the Personal AFP Server
:: This script performs the following:
::   1. Sets up the configuration needed to enable event logging (viewed from the EventViewer app)
::      and counters in the Performance Monitor (PerfMon).
::   2. Puts a shortcut to the AFP Server on the user's desktop.
::
:: To uninstall, use the -u switch, e.g. install.cmd -u

echo off

:: BatchGotAdmin - from https://stackoverflow.com/questions/1894967/how-to-request-administrator-access-inside-a-batch-file
:-------------------------------------
REM  --> Check for permissions
    IF "%PROCESSOR_ARCHITECTURE%" EQU "amd64" (
>nul 2>&1 "%SYSTEMROOT%\SysWOW64\cacls.exe" "%SYSTEMROOT%\SysWOW64\config\system"
) ELSE (
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"
)

REM --> If error flag set, we do not have admin.
if '%errorlevel%' NEQ '0' (
    echo Requesting administrative privileges...
    goto UACPrompt
) else ( goto gotAdmin )

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    set params= %*
    echo UAC.ShellExecute "cmd.exe", "/c ""%~s0"" %params:"=""%", "", "runas", 1 >> "%temp%\getadmin.vbs"

    "%temp%\getadmin.vbs"
    del "%temp%\getadmin.vbs"
    exit /B

:gotAdmin
    pushd "%CD%"
    CD /D "%~dp0"
:--------------------------------------

:: Get the location of the desktop folder for this user.
for /f "usebackq tokens=1,2,*" %%B IN (`reg query "HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders" /v Desktop`) do set DESKTOP=%%D

:: Locations and names of our items
set productname=Personal AFP Server
set manufacturer=AfpServer

if "%~1" == "-u" goto uninstall

echo Installing Personal AFP Server for Windows...
echo Installing into %~dp0

:: Register only when an installation is found next to this script (works for
:: the default path AND custom MSI install directories).
if exist "%~dp0afpServer.exe" (
    goto setuptools
) else (
    echo No existing installation found
    goto end
)

:setuptools

:: Enable PerfMon and Event Viewer instruments
echo Setting up PerfMon counters and Event Viewer output...
wevtutil im afpServer.man
lodctr.exe /M:afpServer.man

echo Installation complete!

goto end

:: Uninstall components
:uninstall
echo Uninstalling Personal AFP Server for Windows tools...

wevtutil.exe um afpServer.man
unlodctr.exe /M:afpServer.man

echo .
echo Uninstall complete.

:end
