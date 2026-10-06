@echo off
rem Builds smpack GUI into 'release'.
rem 'tools\smpack.exe' is copied next to 'SmpackGui.exe' automatically.
dotnet publish src\SmpackGui\SmpackGui.csproj -c Release -r win-x64 --self-contained false -o release || exit /b 1
echo.
echo Done.
