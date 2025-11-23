@echo off
REM Build and deploy the Example Plugin (Windows)

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set QGC_ROOT=%SCRIPT_DIR%..\..
set BUILD_DIR=%QGC_ROOT%\build
set PLUGIN_DEST=%LOCALAPPDATA%\QGroundControl\QGroundControl Daily\plugins

echo Building Example Plugin for Windows...

REM Touch QML file to force rebuild (update timestamp)
copy /b "%SCRIPT_DIR%ExamplePluginView.qml" +,, > nul

REM Build the plugin
cd /d "%QGC_ROOT%"
cmake --build "%BUILD_DIR%" --config Debug --target ExamplePlugin -j %NUMBER_OF_PROCESSORS%

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)

REM Copy to plugin directory
echo Deploying plugin...
if not exist "%PLUGIN_DEST%" mkdir "%PLUGIN_DEST%"
copy /y "%BUILD_DIR%\Debug\plugins\ExamplePlugin.dll" "%PLUGIN_DEST%\" > nul

echo.
echo ✓ Plugin built and deployed successfully!
echo   Plugin location: %PLUGIN_DEST%\ExamplePlugin.dll
echo   Restart QGroundControl to load the updated plugin.
