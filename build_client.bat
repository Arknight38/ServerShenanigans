@echo off
setlocal

REM === CONFIG ===
set PROJECT_NAME=client
set SRC=client.cpp
set VCPKG_PATH=C:\Users\Ark\Documents\ServerShenanigans\vcpkg
set TRIPLET=x64-mingw-dynamic

REM === PATHS ===
set INCLUDE_PATH=%VCPKG_PATH%\installed\%TRIPLET%\include
set LIB_PATH=%VCPKG_PATH%\installed\%TRIPLET%\lib
set BIN_PATH=%VCPKG_PATH%\installed\%TRIPLET%\bin

REM === BUILD ===
echo [*] Building %PROJECT_NAME%.exe ...
g++ -std=c++17 %SRC% -o %PROJECT_NAME%.exe -I%INCLUDE_PATH% -L%LIB_PATH% -lssl -lcrypto -lzlib -lws2_32

if errorlevel 1 (
    echo [!] Build failed.
    pause
    exit /b
)
echo [+] Build successful.

REM === COPY DLLs ===
echo [*] Copying runtime DLLs ...
copy "%BIN_PATH%\*.dll" . >nul

if errorlevel 1 (
    echo [!] Some DLLs may not have been copied.
) else (
    echo [+] All DLLs copied successfully.
)

REM === DONE ===
echo [*] Launching %PROJECT_NAME%.exe ...
%PROJECT_NAME%.exe

pause
endlocal
