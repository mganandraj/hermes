REM python hermes\utils\build\build_llvm.py --build-system="Visual Studio 16 2019" --32-bit
pushd llvm_build_32
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=RelWithDebInfo
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=Debug
popd

REM python hermes\utils\build\build_llvm.py --build-system="Visual Studio 16 2019" --cmake-flags="-A x64"
pushd llvm_build
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=RelWithDebInfo
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=Debug
popd

REM python hermes\utils\build\configure.py --build-system="Visual Studio 16 2019" --cmake-flags="-DLLVM_ENABLE_LTO=OFF -DHERMESVM_PLATFORM_LOGGING=ON" --32-bit
pushd build_32
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=RelWithDebInfo
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=Debug
popd

REM python hermes\utils\build\configure.py --build-system="Visual Studio 16 2019" --cmake-flags="-A x64 -DLLVM_ENABLE_LTO=OFF -DHERMESVM_PLATFORM_LOGGING=ON"
pushd build
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=RelWithDebInfo
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=Debug
popd

robocopy llvm_build_32 llvm_build_32_uwp /E
robocopy llvm_build llvm_build_uwp /E

REM python hermes\utils\build\configure.py --build-system="Visual Studio 16 2019" --cmake-flags="-DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 -DLLVM_ENABLE_LTO=OFF -DHERMESVM_PLATFORM_LOGGING=ON" --32-bit --uwp
pushd build_32_uwp
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=RelWithDebInfo
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=Debug
popd

REM python hermes\utils\build\configure.py --build-system="Visual Studio 16 2019" --cmake-flags="-DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 -A x64 -DLLVM_ENABLE_LTO=OFF -DHERMESVM_PLATFORM_LOGGING=ON" --uwp
pushd build_uwp
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=RelWithDebInfo
MSBuild.exe ALL_BUILD.vcxproj /p:Configuration=Debug
popd

