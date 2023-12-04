:: Install/Uninstall components for the Personal AFP Server
:: This script performs the following:
::   1. Copies the server binary, necessary support DLL, and other misc. files to Program Files (x86)
::   2. Sets up the configuration needed to enable event logging (viewed from the EventViewer app)
::      and counters in the Performance Monitor (PerfMon).
::   3. Puts a shortcut to the AFP Server on the user's desktop.
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
set productname=AfpServer
set servername=AfpServer
set installpath=%ProgramFiles(x86)%\%productname%\
set linkpath=%DESKTOP%\%servername%.lnk

:: Cannot install or uninstall if the server currently running.
tasklist /fi "ImageName eq %servername%.exe" /fo csv 2>NUL | find /I "%servername%.exe">NUL
if "%ERRORLEVEL%"=="0" (
    echo ERROR: Program is running, please shutdown the server and try again.
    goto end
)

if "%~1" == "-u" goto uninstall

echo Installing Personal AFP Server for Windows...
echo Installing into %installpath%

if exist "%installpath%" (
    goto copyfiles
) else (
    echo No existing installation found
)

mkdir "%installpath%"

:: Copy the files to the correct location, overwrite any existing.
:copyfiles
copy /y *.* "%installpath%"

:: Create a shortcut on the desktop using VB
:: From https://superuser.com/questions/455364/how-to-create-a-shortcut-using-a-batch-script
echo Creating shortcut on the Desktop...

set SCRIPT="%TEMP%\afp-shrtcut.vbs"

echo Set oWS = WScript.CreateObject("WScript.Shell") >> %SCRIPT%
echo sLinkFile = "%DESKTOP%\%servername%.lnk" >> %SCRIPT%
echo Set oLink = oWS.CreateShortcut(sLinkFile) >> %SCRIPT%
echo oLink.TargetPath = "%installpath%%servername%.exe" >> %SCRIPT%
echo oLink.Save >> %SCRIPT%

cscript /nologo %SCRIPT%
del %SCRIPT%

:: Enable PerfMon and Event Viewer instruments
echo Setting up PerfMon counters and Event Viewer output...
wevtutil im afpServer.man >nul
lodctr.exe /M:afpServer.man >nul

echo Installation complete!

goto end

:: Uninstall components
:uninstall
echo Uninstalling Personal AFP Server for Windows tools...

wevtutil.exe um afpServer.man
unlodctr.exe /M:afpServer.man

del /q "%DESKTOP%\%servername%.lnk"
rd /s /q "%installpath%"

echo .
echo Uninstall complete.

:end