@echo off
REM Build mit Ninja statt Visual Studio/MSBuild
REM Schneller, weniger Speicher, keine IDE noetig
REM Voraussetzung: Deps bereits gebaut, Ninja installiert (mit VS oder choco install ninja)

setlocal
set "DEPS=%~dp0deps\build\out_deps"
set "BUILD_DIR=%~dp0build_ninja"

echo.
echo *** Ninja Build (Release) ***
echo Deps: %DEPS%
echo.

REM Strawberry GMP/MPFR ausblenden
set "STRAWBERRY_LIB=C:\Strawberry\c\lib"
if exist "%STRAWBERRY_LIB%\libgmp.a" (
    echo Verstecke Strawberry GMP/MPFR
    ren "%STRAWBERRY_LIB%\libgmp.a" libgmp.a.bak 2>nul
    ren "%STRAWBERRY_LIB%\libmpfr.a" libmpfr.a.bak 2>nul
    set "RESTORE=1"
)

REM VS-Umgebung laden (fuer cl.exe, link.exe)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\vsdevcmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 (
    echo Fehler: Visual Studio 2022 nicht gefunden.
    goto :restore
)

cd /d "%~dp0"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

if not exist "build.ninja" (
    echo CMake konfiguriert (Ninja)
    cmake .. -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_PREFIX_PATH="%DEPS%\usr\local" ^
        -DGMP_ROOT="%DEPS%\usr\local" ^
        -DMPFR_ROOT="%DEPS%\usr\local" ^
        -DBBL_RELEASE_TO_PUBLIC=1 ^
        -DBBL_INTERNAL_TESTING=0
    if errorlevel 1 goto :restore
)

echo Baue mit Ninja (parallel)
cmake --build . --config Release --parallel
set "BUILD_OK=%ERRORLEVEL%"

:restore
if defined RESTORE (
    echo Stelle Strawberry wieder her
    if exist "%STRAWBERRY_LIB%\libgmp.a.bak" ren "%STRAWBERRY_LIB%\libgmp.a.bak" libgmp.a
    if exist "%STRAWBERRY_LIB%\libmpfr.a.bak" ren "%STRAWBERRY_LIB%\libmpfr.a.bak" libmpfr.a
)

cd /d "%~dp0"
if %BUILD_OK% equ 0 (
    echo.
    echo *** Build erfolgreich! ***
    echo EXE: %BUILD_DIR%\src\bambu-studio.exe
) else (
    echo.
    echo *** Build fehlgeschlagen ***
)
exit /b %BUILD_OK%
