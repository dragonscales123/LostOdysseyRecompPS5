@echo off
setlocal
cd /d "%~dp0"
echo Lost Odyssey PS5 - shader cache builder
echo.
set "GAME=%~1"
if "%GAME%"=="" (
  echo Drag the folder that holds disc1, disc2, disc3 and disc4 into this window,
  echo then press Enter.
  set /p "GAME=> "
)
set "GAME=%GAME:"=%"
set "OUT=%~dp0shader-cache"
set "MODE="
choice /c YN /m "Include TAA motion-replay shaders (bigger file, about 1.9 GB; choose N if you never use TAA)"
if errorlevel 2 set "MODE=--no-motion-replay"
echo.

rem The game's own shaders: the shader pack this build of the PS5 port was
rem made for, from the upstream project's shader-packs release, checked by SHA-256.
set "RELEASE=https://github.com/freefrank/LostOdysseyRecomp/releases/download/shader-packs"
if not exist "%OUT%\shaders" mkdir "%OUT%\shaders"
call :fetch portable_vk-03ef078715ed8c8b.lospv portable_vk.lospv c637b19ee9e05e12055cd5b906d5d7116f3e8a3b69d9b749e7b37285f21cd722 || goto failed
call :fetch pipelines_corpus-183df2d935260e33.bin pipelines_corpus.bin 183df2d935260e331284fac54c4f630ef1b9bbc4e43dfb1ad89d3d1269bf0c4f || goto failed

echo.
LoShaderCacheBuilder.exe "%GAME%" "%OUT%" %MODE% || goto failed
echo.
echo Done. Copy the "cache" and "shaders" folders from
echo   %OUT%
echo into /data/homebrew/PPSA99212/ on the PS5.
pause
exit /b 0

:failed
echo.
echo Something went wrong; see the messages above.
pause
exit /b 1

rem :fetch <file on the release> <name in shaders\> <sha256>
:fetch
set "TARGET=%OUT%\shaders\%~2"
call :check "%TARGET%" %3 && (echo %~2 is already there.& exit /b 0)
echo Downloading %~2 (the game's shaders, from the upstream project) ...
curl.exe -fL --retry 3 -o "%TARGET%.download" "%RELEASE%/%~1" || (echo Download failed: check your internet connection.& exit /b 1)
call :check "%TARGET%.download" %3 || (echo %~2 did not download correctly; run this again.& del "%TARGET%.download" & exit /b 1)
move /y "%TARGET%.download" "%TARGET%" > nul
exit /b 0

rem :check <file> <sha256>
:check
if not exist "%~1" exit /b 1
powershell -NoProfile -Command "if ((Get-FileHash -Algorithm SHA256 -LiteralPath '%~1').Hash -ieq '%~2') { exit 0 } else { exit 1 }"
exit /b %errorlevel%
