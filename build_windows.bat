@echo off
REM Builds Viper Engine on Windows. Needs CMake plus either Visual Studio Build Tools or MinGW-w64 (gcc).
where cmake >nul 2>nul || (echo CMake not found. Install it from https://cmake.org/download/ & exit /b 1)
where gcc >nul 2>nul
if %errorlevel%==0 (
  cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
  cmake --build build -j || exit /b 1
  set OUT=build
) else (
  cmake -S . -B build -A x64 || exit /b 1
  cmake --build build --config Release || exit /b 1
  set OUT=build\Release
)
echo.
echo Built: %OUT%\viper.exe  (scenes\ and scripts\ are copied next to it)
echo Run:   cd %OUT% ^&^& viper.exe
