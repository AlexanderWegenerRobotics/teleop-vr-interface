@echo off
setlocal enabledelayedexpansion
REM ===========================================================================
REM package.bat -- build a frozen packaged build for data collection.
REM
REM   package.bat              -> archives to ..\_packaged\teleop_vr_YYYYMMDD_HHMM
REM   package.bat mytag        -> archives to ..\_packaged\teleop_vr_mytag
REM
REM Does three things the Editor's File > Package menu does not:
REM   1. records the git hash of this repo into the archive, so an episode can
REM      be traced back to the exact build that produced it;
REM   2. verifies after staging that the runtime files which are NOT cooked
REM      assets actually landed -- the configs and ffmpeg.exe. Those are the
REM      ones that went missing last time, and the failure is silent at
REM      runtime: a missing config aborts startup, a missing ffmpeg.exe just
REM      produces empty mp4s;
REM   3. fails loudly instead of leaving a half-staged directory.
REM
REM Packaging settings themselves live in Config/DefaultGame.ini
REM ([/Script/UnrealEd.ProjectPackagingSettings]) so they are versioned rather
REM than living in one machine's editor preferences.
REM ===========================================================================

set "UE_ROOT=C:\Program Files\Epic Games\UE_5.4"
set "PROJECT_DIR=%~dp0"
set "PROJECT=%PROJECT_DIR%teleop_vr_interface.uproject"

if "%~1"=="" (
    for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /value') do set "DT=%%I"
    set "TAG=!DT:~0,8!_!DT:~8,4!"
) else (
    set "TAG=%~1"
)
set "ARCHIVE=%PROJECT_DIR%..\_packaged\teleop_vr_%TAG%"

echo.
echo ===========================================================
echo   project : %PROJECT%
echo   archive : %ARCHIVE%
echo   engine  : %UE_ROOT%
echo ===========================================================
echo.

if not exist "%UE_ROOT%\Build\BatchFiles\RunUAT.bat" (
    echo [ERROR] RunUAT.bat not found under %UE_ROOT%. Fix UE_ROOT at the top of this script.
    exit /b 1
)
if not exist "%PROJECT%" (
    echo [ERROR] %PROJECT% not found.
    exit /b 1
)

REM -- Record what is being frozen --------------------------------------------
set "GITHASH=unknown"
set "GITDIRTY="
for /f %%H in ('git -C "%PROJECT_DIR%." rev-parse --short HEAD 2^>nul') do set "GITHASH=%%H"
for /f %%S in ('git -C "%PROJECT_DIR%." status --porcelain 2^>nul') do set "GITDIRTY=1"
if defined GITDIRTY (
    echo [WARN] working tree is DIRTY -- this build will not correspond to any commit.
    echo        Commit first if this package is meant to be reproducible.
    echo.
)

REM -- Build / cook / stage / archive -----------------------------------------
call "%UE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%PROJECT%" ^
    -noP4 -utf8output ^
    -platform=Win64 -clientconfig=Development ^
    -build -cook -allmaps -stage -pak -archive ^
    -archivedirectory="%ARCHIVE%"

if errorlevel 1 (
    echo.
    echo [ERROR] BuildCookRun failed. Nothing usable was produced.
    exit /b 1
)

REM -- Verify the non-cooked runtime files landed -----------------------------
set "STAGED=%ARCHIVE%\Windows\teleop_vr_interface"
set "FAIL="

echo.
echo --- staging check -----------------------------------------
call :check "%STAGED%\Config\TeleOp\config.json"          "TeleOp config"
call :check "%STAGED%\Config\TeleOp\network.json"          "network config"
call :check "%STAGED%\Config\TeleOp\stream_local.json"     "stream config"
call :check "%STAGED%\ThirdParty\ffmpeg\ffmpeg.exe"        "bundled ffmpeg"
call :check "%STAGED%\ThirdParty\voice_annotator\voice_annotator.py"  "voice annotator"
call :check "%ARCHIVE%\Windows\teleop_vr_interface.exe"    "executable"
echo -----------------------------------------------------------

if defined FAIL (
    echo.
    echo [ERROR] The package is incomplete. Check the
    echo         DirectoriesToAlwaysStageAsNonUFS entries in Config/DefaultGame.ini.
    exit /b 1
)

REM -- Stamp the build --------------------------------------------------------
> "%ARCHIVE%\BUILD_INFO.txt" (
    echo teleop_vr_interface packaged build
    echo tag          : %TAG%
    echo built        : %DATE% %TIME%
    echo git          : %GITHASH%
    if defined GITDIRTY ( echo tree         : DIRTY - does not match a commit ) else ( echo tree         : clean )
    echo config       : Development ^(UE_LOG active^)
    echo engine       : %UE_ROOT%
    echo.
    echo Logs are written to Windows\teleop_vr_interface\Saved\Logs\TeleOp\
    echo Runtime JSON config is loose under Windows\teleop_vr_interface\Config\TeleOp\
    echo and can be edited without repackaging.
)

echo.
echo [OK] %ARCHIVE%
echo      git %GITHASH%  -- see BUILD_INFO.txt
echo.
echo Run it with:
echo    "%ARCHIVE%\Windows\teleop_vr_interface.exe"
echo bStartInVR=True is set, so no -vr flag is needed. If it ever opens
echo flat, add -vr to force it and check the OpenXR runtime is SteamVR.
exit /b 0

:check
if exist %1 (
    echo   [ok]      %~2
) else (
    echo   [MISSING] %~2   %~1
    set "FAIL=1"
)
exit /b 0
