echo off

if exist C:\Users\micha\source\repos\AfpServer\PersonalAfpServer\ (
  echo Deleting existing deploy folder...
  rd /s /q C:\Users\micha\source\repos\AfpServer\PersonalAfpServer
) else (
  echo Didn't find existing deploy folder
)

echo Creating new directory...
mkdir C:\Users\micha\source\repos\AfpServer\PersonalAfpServer

echo Copy files...
copy /y "C:\Users\micha\source\repos\AfpServer\afpServer\Release\AfpServer.exe" C:\Users\micha\source\repos\AfpServer\PersonalAfpServer\
copy /y "C:\Users\micha\source\repos\AfpServer\afpServer\AfpServer.man" C:\Users\micha\source\repos\AfpServer\PersonalAfpServer\
copy /y "C:\Users\micha\source\repos\AfpServer\install.cmd" C:\Users\micha\source\repos\AfpServer\PersonalAfpServer\
copy /y "C:\Users\micha\source\repos\AfpServer\afpServer\deps\openssl-1.1\lib\libcrypto-1_1.dll" C:\Users\micha\source\repos\AfpServer\PersonalAfpServer\