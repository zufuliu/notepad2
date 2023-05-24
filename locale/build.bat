@ECHO OFF
@rem based on build\VS2017\build.bat

SETLOCAL ENABLEEXTENSIONS
CD /D %~dp0

@rem Check for the help switches
IF /I "%~1" == "help"   GOTO SHOWHELP
IF /I "%~1" == "/help"  GOTO SHOWHELP
IF /I "%~1" == "-help"  GOTO SHOWHELP
IF /I "%~1" == "--help" GOTO SHOWHELP
IF /I "%~1" == "/?"     GOTO SHOWHELP

@rem default arguments
SET "BUILDTYPE=Build"
SET "ARCH=all"
SET NO_32BIT=0
SET NO_ARM=0
SET "CONFIG=Release"

@rem Check for the first switch
IF "%~1" == "" GOTO StartWork
IF /I "%~1" == "Build"     SET "BUILDTYPE=Build"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "/Build"    SET "BUILDTYPE=Build"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "-Build"    SET "BUILDTYPE=Build"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "--Build"   SET "BUILDTYPE=Build"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "Clean"     SET "BUILDTYPE=Clean"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "/Clean"    SET "BUILDTYPE=Clean"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "-Clean"    SET "BUILDTYPE=Clean"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "--Clean"   SET "BUILDTYPE=Clean"   & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "Rebuild"   SET "BUILDTYPE=Rebuild" & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "/Rebuild"  SET "BUILDTYPE=Rebuild" & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "-Rebuild"  SET "BUILDTYPE=Rebuild" & SHIFT & GOTO CheckSecondArg
IF /I "%~1" == "--Rebuild" SET "BUILDTYPE=Rebuild" & SHIFT & GOTO CheckSecondArg


:CheckSecondArg
@rem Check for the second switch
IF "%~1" == "" GOTO StartWork
IF /I "%~1" == "x86"     SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/x86"    SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-x86"    SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--x86"   SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "Win32"   SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/Win32"  SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-Win32"  SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--Win32" SET "ARCH=Win32" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "x64"     SET "ARCH=x64"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/x64"    SET "ARCH=x64"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-x64"    SET "ARCH=x64"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--x64"   SET "ARCH=x64"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "AVX2"    SET "ARCH=AVX2"  & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/AVX2"   SET "ARCH=AVX2"  & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-AVX2"   SET "ARCH=AVX2"  & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--AVX2"  SET "ARCH=AVX2"  & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "ARM64"   SET "ARCH=ARM64" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/ARM64"  SET "ARCH=ARM64" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-ARM64"  SET "ARCH=ARM64" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--ARM64" SET "ARCH=ARM64" & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "ARM"     SET "ARCH=ARM"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/ARM"    SET "ARCH=ARM"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-ARM"    SET "ARCH=ARM"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--ARM"   SET "ARCH=ARM"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "all"     SET "ARCH=all"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/all"    SET "ARCH=all"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-all"    SET "ARCH=all"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--all"   SET "ARCH=all"   & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "No32bit"   SET "ARCH=all" & SET NO_ARM=1 & SET NO_32BIT=1 & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/No32bit"  SET "ARCH=all" & SET NO_ARM=1 & SET NO_32BIT=1 & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-No32bit"  SET "ARCH=all" & SET NO_ARM=1 & SET NO_32BIT=1 & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--No32bit" SET "ARCH=all" & SET NO_ARM=1 & SET NO_32BIT=1 & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "NoARM"   SET "ARCH=all"   & SET NO_ARM=1 & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "/NoARM"  SET "ARCH=all"   & SET NO_ARM=1 & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "-NoARM"  SET "ARCH=all"   & SET NO_ARM=1 & SHIFT & GOTO CheckThirdArg
IF /I "%~1" == "--NoARM" SET "ARCH=all"   & SET NO_ARM=1 & SHIFT & GOTO CheckThirdArg


:CheckThirdArg
@rem Check for the third switch
IF "%~1" == "" GOTO StartWork
IF /I "%~1" == "Debug"     SET "CONFIG=Debug"   & SHIFT & GOTO StartWork
IF /I "%~1" == "/Debug"    SET "CONFIG=Debug"   & SHIFT & GOTO StartWork
IF /I "%~1" == "-Debug"    SET "CONFIG=Debug"   & SHIFT & GOTO StartWork
IF /I "%~1" == "--Debug"   SET "CONFIG=Debug"   & SHIFT & GOTO StartWork
IF /I "%~1" == "Release"   SET "CONFIG=Release" & SHIFT & GOTO StartWork
IF /I "%~1" == "/Release"  SET "CONFIG=Release" & SHIFT & GOTO StartWork
IF /I "%~1" == "-Release"  SET "CONFIG=Release" & SHIFT & GOTO StartWork
IF /I "%~1" == "--Release" SET "CONFIG=Release" & SHIFT & GOTO StartWork
IF /I "%~1" == "all"       SET "CONFIG=all"     & SHIFT & GOTO StartWork
IF /I "%~1" == "/all"      SET "CONFIG=all"     & SHIFT & GOTO StartWork
IF /I "%~1" == "-all"      SET "CONFIG=all"     & SHIFT & GOTO StartWork
IF /I "%~1" == "--all"     SET "CONFIG=all"     & SHIFT & GOTO StartWork


:StartWork
SET "EXIT_ON_ERROR=%~1"

SET NEED_ARM64=0
SET NEED_ARM=0
IF /I "%ARCH%" == "all" SET NEED_ARM64=1
IF /I "%ARCH%" == "ARM64" SET NEED_ARM64=1
IF /I "%ARCH%" == "all" SET /A NEED_ARM=1 - %NO_ARM%
IF /I "%ARCH%" == "ARM" SET NEED_ARM=1
CALL :SubVSPath
IF NOT EXIST "%VS_PATH%" CALL :SUBMSG "ERROR" "Visual Studio 2017, 2019 or 2022 NOT FOUND, please check VS_PATH environment variable!"

IF /I "%processor_architecture%" == "AMD64" (
	SET "HOST_ARCH=amd64"
) ELSE (
	SET "HOST_ARCH=x86"
)

IF %NO_32BIT% == 1 GOTO x64
IF /I "%ARCH%" == "AVX2" GOTO x64
IF /I "%ARCH%" == "x64" GOTO x64
IF /I "%ARCH%" == "Win32" GOTO Win32
IF /I "%ARCH%" == "ARM64" GOTO ARM64
IF /I "%ARCH%" == "ARM" GOTO ARM


:Win32
SETLOCAL
CALL "%VS_PATH%\Common7\Tools\vsdevcmd" -no_logo -arch=x86 -host_arch=%HOST_ARCH%
IF /I "%CONFIG%" == "all" (CALL :SUBMSVC %BUILDTYPE% Debug Win32 && CALL :SUBMSVC %BUILDTYPE% Release Win32) ELSE (CALL :SUBMSVC %BUILDTYPE% %CONFIG% Win32)
ENDLOCAL
IF /I "%ARCH%" == "Win32" GOTO END


:x64
SETLOCAL
CALL "%VS_PATH%\Common7\Tools\vsdevcmd" -no_logo -arch=amd64 -host_arch=%HOST_ARCH%
IF /I "%CONFIG%" == "all" (CALL :SUBMSVC %BUILDTYPE% Debug x64 && CALL :SUBMSVC %BUILDTYPE% Release x64) ELSE (CALL :SUBMSVC %BUILDTYPE% %CONFIG% x64)
ENDLOCAL
IF /I "%ARCH%" == "x64" GOTO END
IF /I "%CONFIG%" == "all" (CALL :COPY_x64_AVX2 Debug && CALL :COPY_x64_AVX2 Release) ELSE (CALL :COPY_x64_AVX2 %CONFIG%)
IF /I "%ARCH%" == "AVX2" GOTO END


:ARM64
SETLOCAL
CALL "%VS_PATH%\Common7\Tools\vsdevcmd" -no_logo -arch=arm64 -host_arch=%HOST_ARCH%
IF /I "%CONFIG%" == "all" (CALL :SUBMSVC %BUILDTYPE% Debug ARM64 && CALL :SUBMSVC %BUILDTYPE% Release ARM64) ELSE (CALL :SUBMSVC %BUILDTYPE% %CONFIG% ARM64)
ENDLOCAL
IF /I "%ARCH%" == "ARM64" GOTO END
IF %NO_ARM% == 1 GOTO END


:ARM
SETLOCAL
CALL "%VS_PATH%\Common7\Tools\vsdevcmd" -no_logo -arch=arm -host_arch=%HOST_ARCH%
IF /I "%CONFIG%" == "all" (CALL :SUBMSVC %BUILDTYPE% Debug ARM && CALL :SUBMSVC %BUILDTYPE% Release ARM) ELSE (CALL :SUBMSVC %BUILDTYPE% %CONFIG% ARM)
ENDLOCAL


:END
TITLE Building Notepad2 DLL with MSVC - Finished!
ENDLOCAL
EXIT /B


:SubVSPath
@rem Check the building environment
@rem VSINSTALLDIR is set by vsdevcmd_start.bat
IF EXIST "%VSINSTALLDIR%\Common7\IDE\VC\VCTargets\Platforms\%ARCH%\PlatformToolsets" (
	SET "VS_PATH=%VSINSTALLDIR%"
	EXIT /B
)

SET VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
SET "VS_COMPONENT=Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
IF "%NEED_ARM64%" == 1 SET "VS_COMPONENT=%VS_COMPONENT% Microsoft.VisualStudio.Component.VC.Tools.ARM64"
IF "%NEED_ARM%" == 1 SET "VS_COMPONENT=%VS_COMPONENT% Microsoft.VisualStudio.Component.VC.Tools.ARM"
FOR /f "delims=" %%A IN ('"%VSWHERE%" -property installationPath -prerelease -version [15.0^,18.0^) -requires %VS_COMPONENT%') DO SET "VS_PATH=%%A"
IF EXIST "%VS_PATH%" (
	SET "VSINSTALLDIR=%VS_PATH%\"
	EXIT /B
)
@rem Visual Studio Build Tools
FOR /f "delims=" %%A IN ('"%VSWHERE%" -products Microsoft.VisualStudio.Product.BuildTools -property installationPath -prerelease -version [15.0^,18.0^) -requires %VS_COMPONENT%') DO SET "VS_PATH=%%A"
IF EXIST "%VS_PATH%" SET "VSINSTALLDIR=%VS_PATH%\"
EXIT /B


:SUBMSVC
ECHO.
TITLE Building Notepad2 DLL with MSVC - %~1 "%~2|%~3"...
CD /D %~dp0
"MSBuild.exe" /nologo Locale.sln /target:Notepad2_zh-Hans_;%~1 /property:Configuration=%~2;Platform=%~3^ /consoleloggerparameters:Verbosity=minimal /maxcpucount /nodeReuse:true
IF %ERRORLEVEL% NEQ 0 CALL :SUBMSG "ERROR" "Compilation failed!"
EXIT /B


:COPY_x64_AVX2
XCOPY /Q /S /Y "..\build\bin\%1\x64\locale" "..\build\bin\%1\AVX2\locale\"
EXIT /B


:SHOWHELP
TITLE %~nx0 %1
ECHO. & ECHO.
ECHO Usage: %~nx0 [Clean^|Build^|Rebuild] [Win32^|x64^|AVX2^|ARM64^|ARM^|all^|NoARM^|No32bit] [Debug^|Release^|all]
ECHO.
ECHO Notes: You can also prefix the commands with "-", "--" or "/".
ECHO        The arguments are not case sensitive.
ECHO. & ECHO.
ECHO Executing %~nx0 without any arguments is equivalent to "%~nx0 build all release"
ECHO.
ECHO If you skip the second argument the default one will be used.
ECHO The same goes for the third argument. Examples:
ECHO "%~nx0 rebuild" is the same as "%~nx0 rebuild all release"
ECHO "%~nx0 rebuild Win32" is the same as "%~nx0 rebuild Win32 release"
ECHO.
ECHO WARNING: "%~nx0 Win32" or "%~nx0 debug" won't work.
ECHO.
ENDLOCAL
EXIT /B


:SUBMSG
ECHO. & ECHO ______________________________
ECHO [%~1] %~2
ECHO ______________________________ & ECHO.
IF /I "%~1" == "ERROR" (
  IF "%EXIT_ON_ERROR%" == "" PAUSE
  EXIT /B
) ELSE (
  EXIT /B
)
