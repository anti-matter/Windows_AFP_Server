
set solutiondir=%~1
set deployfolder=%solutiondir%\MacFile

pushd %solutiondir%

if exist %deployfolder%\ (
  echo Deleting existing deploy folder...
  rd /s /q %deployfolder%
) else (
  echo Didn't find existing deploy folder
)

echo Creating new directory...
mkdir %deployfolder%

echo Copy files...
copy /y "%solutiondir%\afpServer\Release\AfpServer.exe" %deployfolder%\
copy /y "%solutiondir%\afpServer\AfpServer.man" %deployfolder%\
copy /y "install.cmd" %deployfolder%\
copy /y "afpServer\deps\openssl-1.1\lib\libcrypto-1_1-x64.dll" %deployfolder%\

tar -a -cf %solutiondir%\MacFile.zip MacFile\*.*

popd

:end
