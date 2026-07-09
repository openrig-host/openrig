@echo off
echo Setting up DaveCore in Projucer...
echo.

REM Check if Projucer exists
if exist "C:\JUCE\Projucer.exe" (
    echo Opening Projucer with DaveCore project...
    start "" "C:\JUCE\Projucer.exe" "%~dp0DaveCore.jucer"
    echo.
    echo ============================================
    echo NEXT STEPS:
    echo 1. In Projucer, click "File" -^> "Save Project"
    echo 2. Then click "File" -^> "Save Project and Open in IDE"
    echo 3. This will generate Visual Studio project files
    echo ============================================
    echo.
    echo Waiting for you to complete the Projucer setup...
    echo Press any key when you've saved the project and generated the VS files...
    pause
    
    REM Check if VS project was generated
    if exist "Builds\VisualStudio2022\DaveCore.sln" (
        echo.
        echo SUCCESS! Visual Studio project generated.
        echo Opening Visual Studio...
        start "" "Builds\VisualStudio2022\DaveCore.sln"
    ) else (
        echo.
        echo WARNING: Visual Studio project not found.
        echo Please make sure you clicked "Save Project and Open in IDE"
        echo The project files should be in: Builds\VisualStudio2022\
    )
) else (
    echo ERROR: Projucer not found at C:\JUCE\Projucer.exe
    echo Please make sure JUCE is properly installed.
)

echo.
pause