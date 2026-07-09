@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

F:

cd F:\project\levilamina-rs\levilamina-rust-loader\

set VCPKG_ROOT=
set CONAN_HOME=
set HTTPS_PROXY=http://127.0.0.1:7890
set HTTP_PROXY=http://127.0.0.1:7890

@REM echo D:\Compiler\xmake\xmake.exe repo -u
@REM D:\Compiler\xmake\xmake.exe repo -u

echo D:\Compiler\xmake\xmake.exe f -c -m release -y -v
D:\Compiler\xmake\xmake.exe f -c -m release -y -v

echo D:\Compiler\xmake\xmake.exe -r
D:\Compiler\xmake\xmake.exe -r

echo.
echo Press any key to exit...
pause >nul
exit /b 0