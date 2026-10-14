@echo off
rem ---------------------------------------------------------------
rem build.bat - build a step's C source with MSVC (cl.exe)
rem
rem This script is shared by every step, so the source name is an
rem argument instead of being hardcoded. Only step-specific files
rem carry the step1_ / step2_ prefix; this one does not.
rem
rem usage:
rem   build.bat                        : build the default step
rem   build.bat run                    : build the default step, then run
rem   build.bat step1_char_freq        : build that source
rem   build.bat step1_char_freq run    : build it, then run
rem
rem cl.exe is not on PATH, so vcvars64.bat must set up the
rem compiler environment first. vswhere.exe locates Visual Studio.
rem
rem cl options:
rem   /utf-8 : source is UTF-8. Without this, Korean output breaks.
rem   /W4    : high warning level
rem   /O2    : optimize
rem   /D_CRT_SECURE_NO_WARNINGS : MSVC flags standard fopen() as unsafe
rem       and pushes fopen_s(), which is Windows-only. We keep the
rem       source as portable standard C and silence the warning here.
rem
rem note: vswhere.exe lives under "Program Files (x86)". The "(x86)"
rem       parentheses break cmd's  for /f ... in ( ... )  parser, so
rem       its output goes through a temp file instead.
rem ---------------------------------------------------------------

setlocal
cd /d "%~dp0"

set "DEFAULT_SRC=step1_char_freq"

rem --- parse arguments: [source] [run] ---
set "SRC="
set "DORUN="
if /i "%~1"=="run" (
    set "SRC=%DEFAULT_SRC%"
    set "DORUN=1"
) else (
    if "%~1"=="" ( set "SRC=%DEFAULT_SRC%" ) else ( set "SRC=%~1" )
    if /i "%~2"=="run" set "DORUN=1"
)

rem strip a trailing .c if the caller typed one
if /i "%SRC:~-2%"==".c" set "SRC=%SRC:~0,-2%"

if not exist "%SRC%.c" (
    echo [ERROR] source not found: %SRC%.c
    echo         available sources:
    for %%f in (*.c) do echo           %%~nf
    exit /b 1
)

rem --- locate Visual Studio ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found
    exit /b 1
)

set "TMPOUT=%TEMP%\llmfs_vspath.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TMPOUT%"

set "VSPATH="
set /p VSPATH=<"%TMPOUT%"
del "%TMPOUT%" >nul 2>&1

if not defined VSPATH (
    echo [ERROR] No Visual Studio with C++ build tools found.
    echo         Add "Desktop development with C++" in Visual Studio Installer.
    exit /b 1
)

set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found
    echo         %VCVARS%
    exit /b 1
)

echo compiler env : %VCVARS%
echo building     : %SRC%.c -^> %SRC%.exe
echo.

call "%VCVARS%" >nul
cl /nologo /utf-8 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS "%SRC%.c" /Fe:"%SRC%.exe"
if errorlevel 1 (
    echo.
    echo [FAILED] compile error
    exit /b 1
)

echo.
echo [OK] %SRC%.exe built

if defined DORUN (
    echo.
    echo --- run ---
    chcp 65001 >nul
    "%SRC%.exe"
)

endlocal
