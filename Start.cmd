@echo off
if exist "%~dp0build\FreeLinkVR.exe" (
    start "" "%~dp0build\FreeLinkVR.exe"
) else (
    start "" "%~dp0FreeLinkVR.exe"
)
