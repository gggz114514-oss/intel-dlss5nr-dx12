@echo off
rem Intel Vulkan NR host tests.
rem Usage: tests\intel\run.cmd build^|gpu^|gpu-quick [out-dir]
rem   build     : compile only (no GPU, no runtime).
rem   gpu       : full matrix: Full and Small, native and 540p, NR-off control,
rem               post-SR refusal, recording replay and a Full->Small switch.
rem   gpu-quick : three representative scenarios only.
rem   gpu-structure : 1024 queued Small frames with per-frame structure 0..2 sweep.
rem Needs INTEL_NR_STAGE (default: exports\intel-host\stage) containing
rem   nrvk_bridge.dll, nr_hdr_proxy.hlsl and nr-intel\{full,small}\{manifest,assets}.
rem Stage building is a developer step: tools\intel-nr\stage-host.cmd.
setlocal EnableExtensions
cd /d "%~dp0..\.."
set "REPO=%CD%"
set "TIER=%~1"
set "OUT=%~2"
if not defined OUT set "OUT=exports\test-run\intel"
for %%I in ("%OUT%") do set "OUT=%%~fI"
if not exist "%OUT%" mkdir "%OUT%"
if not defined INTEL_NR_STAGE set "INTEL_NR_STAGE=%REPO%\exports\intel-host\stage"
rem CI and developer prompts already have cl. On this host the toolbox lives in a
rem non-default location, so allow MSVC_VCVARS to name vcvarsall.bat explicitly.
where cl.exe >nul 2>&1 || if defined MSVC_VCVARS call "%MSVC_VCVARS%" x64 >nul
call "%REPO%\tests\_lib\msvc-env.cmd" 2>nul || exit /b 1
rem Repo-rooted includes (#include "OptiScaler-...") go at the END of INCLUDE: /I is
rem searched before the SDK/STL directories and would shadow <version> on this disk.
set "EXT=%REPO%\OptiScaler-DLSSNR-PreSR-Multipass-main\external"
set "PROD=%REPO%\OptiScaler-DLSSNR-PreSR-Multipass-main\OptiScaler"
set "INCLUDE=%INCLUDE%;%PROD%;%PROD%\include;%EXT%\nvngx_dlss_sdk;%EXT%\xess\inc\xess;%EXT%\xess\inc\xell;%EXT%\xess\inc\xess_fg;%EXT%\FidelityFX-SDK\ffx-api\include\ffx_api;%EXT%\simpleini;%EXT%\unordered_dense\include;%EXT%\spdlog\include;%EXT%\freetype;%EXT%\streamline;%EXT%\streamline1;%EXT%\nvapi;%EXT%\nlohmann;%EXT%\fakenvapi;%EXT%\magic_enum\include\magic_enum;%EXT%\AntiLag2-SDK;%EXT%\latencyflex;%EXT%\vulkan\include;%REPO%"
set "DETOURS=%PROD%\library\detours\detours.lib"
set "CXX=cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DNOMINMAX /D_WIN32_WINNT=0x0A00"
set "EXE=%OUT%\intel_host_gpu.exe"
if /i "%TIER%"=="build" goto build
if /i "%TIER%"=="input-contract" goto inputcontract
if /i "%TIER%"=="input-shaders" goto inputshaders
if /i "%TIER%"=="input-gpu-build" goto inputgpubuild
if /i "%TIER%"=="input-gpu" goto inputgpu
if /i "%TIER%"=="input-feedback" goto inputfeedback
if /i "%TIER%"=="gpu" goto gpu
if /i "%TIER%"=="gpu-quick" goto gpuquick
if /i "%TIER%"=="gpu-queued" goto gpuqueued
if /i "%TIER%"=="gpu-structure" goto gpustructure
if /i "%TIER%"=="gpu-retirement" goto gpuretirement
if /i "%TIER%"=="gpu-retained" goto gpuretained
echo usage: tests\intel\run.cmd build^|gpu^|gpu-quick [out-dir]
exit /b 2

:build
call :Compile || goto fail
echo intel build: PASS
exit /b 0

:inputfeedback
call :Compile || goto fail
call :StageCheck || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 6 --resize || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 6 --resize --small || goto fail
exit /b 0

:inputgpu
call :InputGpuCompile || goto fail
"%OUT%\input_adapter_gpu.exe" "%PROD%\dlssnr\backend\intel\nr_input_adapter.hlsl" || goto fail
exit /b 0

:inputgpubuild
call :InputGpuCompile || goto fail
exit /b 0

:InputGpuCompile
%CXX% tests\intel\input_adapter_gpu.cpp /Fe"%OUT%\input_adapter_gpu.exe" /Fo"%OUT%\input_adapter_gpu.obj" /link d3d12.lib dxgi.lib d3dcompiler.lib || exit /b 1
exit /b 0

:inputshaders
%CXX% tests\intel\input_shader_compile.cpp /Fe"%OUT%\input_shader_compile.exe" /Fo"%OUT%\input_shader_compile.obj" /link d3dcompiler.lib || goto fail
"%OUT%\input_shader_compile.exe" "%PROD%\dlssnr\backend\intel\nr_input_adapter.hlsl" || goto fail
exit /b 0

:inputcontract
%CXX% tests\intel\input_contract.cpp /Fe"%OUT%\input_contract.exe" /Fo"%OUT%\input_contract.obj" || goto fail
"%OUT%\input_contract.exe" || goto fail
exit /b 0

:gpuquick
call :Compile || goto fail
call :StageCheck || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 3 || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 3 --small || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 3 --no-nr || goto fail
echo intel gpu-quick: PASS
exit /b 0

:gpuqueued
call :Compile || goto fail
call :StageCheck || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 4096 --small --queued || goto fail
echo intel gpu-queued: PASS
exit /b 0

:gpu
call :Compile || goto fail
call :StageCheck || goto fail
rem Full: native input keeps the game's own render size.
"%EXE%" "%INTEL_NR_STAGE%" --size 1920x1080 --frames 4 || goto fail
rem Full: explicit 540p network, output still returns at the SR input size.
"%EXE%" "%INTEL_NR_STAGE%" --size 1920x1080 --height 540 --frames 4 || goto fail
rem Small network at the same geometry.
"%EXE%" "%INTEL_NR_STAGE%" --size 1920x1080 --frames 4 --small || goto fail
rem Controls: NR-off and post-SR must reproduce the input and never pretend to run.
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 2 --no-nr || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 2 --post-sr || goto fail
rem Same closed recording re-executed while the first execution is still in flight.
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 3 --replay || goto fail
rem Full -> Small inside one session, without dropping frames.
"%EXE%" "%INTEL_NR_STAGE%" --size 1600x900 --frames 6 --switch || goto fail
echo intel gpu: PASS
exit /b 0

:gpuretained
call :Compile || goto fail
call :StageCheck || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 12 --retain-recordings || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 12 --retain-recordings --small || goto fail
echo intel gpu-retained: PASS
exit /b 0

:gpuretirement
call :Compile || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --retirement-probe || goto fail
exit /b 0

:gpustructure
call :Compile || goto fail
call :StageCheck || goto fail
"%EXE%" "%INTEL_NR_STAGE%" --size 1280x720 --frames 1024 --small --same-frame --queued --structure-sweep || goto fail
echo intel structure sweep: PASS
exit /b 0

:Compile
%CXX% tests\intel\intel_host_gpu.cpp "%PROD%\dlssnr\backend\IntelBackend.cpp" "%PROD%\dlssnr\backend\intel\nr_hdr_proxy.cpp" /Fe"%EXE%" /Fo"%OUT%\\" /link d3d12.lib dxgi.lib dxguid.lib "%DETOURS%" d3dcompiler.lib bcrypt.lib || exit /b 1
exit /b 0

:StageCheck
if exist "%INTEL_NR_STAGE%\nrvk_bridge.dll" if exist "%INTEL_NR_STAGE%\nr_hdr_proxy.hlsl" if exist "%INTEL_NR_STAGE%\nr-intel\full\manifest\port_network_runtime.txt" if exist "%INTEL_NR_STAGE%\nr-intel\small\manifest\port_network_runtime.txt" exit /b 0
echo FAIL: INTEL_NR_STAGE="%INTEL_NR_STAGE%" is incomplete.
echo       Run tools\intel-nr\stage-host.cmd to build it from the NR packages.
exit /b 1

:fail
echo intel %TIER%: FAIL
exit /b 1
