@echo off
setlocal
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist %VCVARS% (
  echo VsDevCmd.bat not found
  exit /b 1
)
call %VCVARS% -arch=x64 >nul
set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
%CMAKE% -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
%CMAKE% --build build
if errorlevel 1 exit /b 1
echo.
echo Built build\jptxt.exe
)
