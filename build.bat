@echo off

setlocal

cd %~dp0

if not exist build mkdir build
cd build

if "%Platform%" neq "x64" (
	echo ERROR: Platform is not "x64" - please run this from the MSVC x64 native tools command prompt.
	goto end
)

set "ignored_warnings=/wd4201 /wd4200 /wd4100"
set "common_compile_options= /nologo /W4 %ignored_warnings% /arch:AVX2 /I.. /I../vendor"
set "common_link_options= /incremental:no /opt:ref /subsystem:windows user32.lib gdi32.lib opengl32.lib"

if "%1"=="debug" (
	set "compile_options=%common_compile_options% /Od /Z7 /Zo /RTC1 /DAZUR_DEBUG"
	set "link_options=%common_link_options% /DEBUG:FULL libucrtd.lib libvcruntimed.lib"
) else if "%1"=="release" (
	set "compile_options=%common_compile_options% /O2 /Z7 /Zo"
	set "link_options=%common_link_options% libvcruntime.lib"
) else (
	goto invalid_arguments
)

set /A build_platform=0
set /A build_engine=0

if "%2"=="platform" (
	set /A build_platform=1
) else if "%2"=="engine" (
	set /A build_engine=1
) else if "%2"=="all" (
	set /A build_platform=1
	set /A build_engine=1
) else (
	goto invalid_arguments
)

if "%3" neq "" goto invalid_arguments

if /I "%build_platform%" equ "1" (
	cl %compile_options% ..\src\platform.c /link %link_options% /pdb:azur.pdb /out:azur.exe
)

if /I "%build_platform%" equ "1" (
	cl %compile_options% ..\src\game.c /LD /link %link_options% /pdb:azur_game.pdb /out:azur_game.dll
)

goto end

:invalid_arguments
echo Invalid arguments^. Usage: build ^[debug ^| release^] ^[platform ^| game ^| all^]
goto end

:end
endlocal
