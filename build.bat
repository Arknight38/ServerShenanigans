@echo off
setlocal enabledelayedexpansion

REM === CONFIG ===
set CLIENT_NAME=client
set SERVER_NAME=server
set CLIENT_SRC=client.cpp
set SERVER_SRC=server.cpp
set TRIPLET=x64-mingw-dynamic
set BUILD_DIR=builds

echo.
echo ========================================
echo   FILE SHARING -  BUILDING
echo ========================================
echo.

REM === CREATE BUILD DIRECTORY ===
if not exist "%BUILD_DIR%" (
    echo [*] Creating builds directory...
    mkdir "%BUILD_DIR%"
    echo [+] Builds directory created.
)

REM === FIND OR INSTALL VCPKG ===
echo [*] Checking for vcpkg installation...

REM Check if vcpkg exists in current directory
if exist "vcpkg\vcpkg.exe" (
    set VCPKG_ROOT=%CD%\vcpkg
    echo [+] Found vcpkg at: !VCPKG_ROOT!
    goto :vcpkg_found
)

REM Check if vcpkg exists in parent directory
if exist "..\vcpkg\vcpkg.exe" (
    set VCPKG_ROOT=%CD%\..\vcpkg
    echo [+] Found vcpkg at: !VCPKG_ROOT!
    goto :vcpkg_found
)

REM Check if VCPKG_ROOT environment variable is set
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        echo [+] Found vcpkg at: %VCPKG_ROOT%
        goto :vcpkg_found
    )
)

REM vcpkg not found - install it
echo [!] vcpkg not found. Installing vcpkg...
git clone https://github.com/microsoft/vcpkg.git
if errorlevel 1 (
    echo [!] Failed to clone vcpkg repository.
    echo [!] Please ensure git is installed and accessible.
    pause
    exit /b 1
)

cd vcpkg
call bootstrap-vcpkg.bat
if errorlevel 1 (
    echo [!] Failed to bootstrap vcpkg.
    pause
    exit /b 1
)
cd ..
set VCPKG_ROOT=%CD%\vcpkg
echo [+] vcpkg installed successfully at: !VCPKG_ROOT!

:vcpkg_found
set VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe

REM === INSTALL REQUIRED PACKAGES ===
echo [*] Installing required packages...
echo [*] This may take a while on first run...

call "%VCPKG_EXE%" install openssl:%TRIPLET%
if errorlevel 1 (
    echo [!] Failed to install openssl.
    pause
    exit /b 1
)

call "%VCPKG_EXE%" install zlib:%TRIPLET%
if errorlevel 1 (
    echo [!] Failed to install zlib.
    pause
    exit /b 1
)

echo [+] All packages installed successfully.

REM === SET PATHS ===
set INCLUDE_PATH=%VCPKG_ROOT%\installed\%TRIPLET%\include
set LIB_PATH=%VCPKG_ROOT%\installed\%TRIPLET%\lib
set BIN_PATH=%VCPKG_ROOT%\installed\%TRIPLET%\bin

REM === BUILD CLIENT ===
echo.
echo [*] Building %CLIENT_NAME%.exe...
g++ -std=c++17 %CLIENT_SRC% -o "%BUILD_DIR%\%CLIENT_NAME%.exe" -I"%INCLUDE_PATH%" -L"%LIB_PATH%" -lssl -lcrypto -lzlib -lws2_32
if errorlevel 1 (
    echo [!] Client build failed.
    pause
    exit /b 1
)
echo [+] Client build successful.

REM === BUILD SERVER ===
echo.
echo [*] Building %SERVER_NAME%.exe ...
g++ -std=c++17 %SERVER_SRC% -o "%BUILD_DIR%\%SERVER_NAME%.exe" -I"%INCLUDE_PATH%" -L"%LIB_PATH%" -lssl -lcrypto -lzlib -lws2_32
if errorlevel 1 (
    echo [!] Server build failed.
    pause
    exit /b 1
)
echo [+] Server build successful.

REM === COPY MENU HEADER ===
echo.
echo [*] Copying menu.h to builds directory...
copy menu.h "%BUILD_DIR%\" >nul 2>&1
if errorlevel 1 (
    echo [!] Warning: menu.h not copied (not needed for executables, only for development)
) else (
    echo [+] menu.h copied.
)

REM === COPY DLLs ===
echo.
echo [*] Copying runtime DLLs to builds directory...
copy "%BIN_PATH%\*.dll" "%BUILD_DIR%\" >nul 2>&1
if errorlevel 1 (
    echo [!] Some DLLs may not have been copied.
) else (
    echo [+] All DLLs copied successfully.
)

REM === DONE ===
echo.
echo ========================================
echo [+] BUILD COMPLETE!
echo ========================================
echo.
echo Built executables in %BUILD_DIR%\:
echo   - %CLIENT_NAME%.exe
echo   - %SERVER_NAME%.exe
echo.
echo New Features:
echo   - Arrow key navigation
echo   - Interactive file browser
echo   - Built-in search (press '/' in menus)
echo   - Color-coded interface
echo   - Smooth scrolling
echo.
echo You can now run either executable from the builds directory.
echo.
pause
endlocal