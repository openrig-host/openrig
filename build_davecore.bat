@echo off
echo Building DaveCore...
echo.

REM Check if Projucer is available
if exist "C:\JUCE\Projucer.exe" (
    echo Found Projucer at C:\JUCE\Projucer.exe
    echo.
    echo Please manually open the Projucer and:
    echo 1. Open DaveCore.jucer
    echo 2. Click "Save Project and Open in IDE" 
    echo 3. This will generate the Visual Studio project files
    echo.
    echo After that, come back here and press any key to continue...
    pause
    
    REM Try to open the generated solution
    if exist "Builds\VisualStudio2022\DaveCore.sln" (
        echo Opening Visual Studio solution...
        start "Builds\VisualStudio2022\DaveCore.sln"
    ) else (
        echo ERROR: Visual Studio solution not found!
        echo Please check if Projucer successfully generated the project files.
    )
) else (
    echo ERROR: Projucer not found at C:\JUCE\Projucer.exe
    echo Please install JUCE or update the path.
)

echo.
echo Build instructions:
echo 1. In Visual Studio, select "Debug" or "Release" configuration
echo 2. Build -^> Build Solution (or press F7)
echo 3. The executable will be in Builds\VisualStudio2022\x64\Debug\ or Release\
echo.
pause