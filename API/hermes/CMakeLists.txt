# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

set(LLVM_OPTIONAL_SOURCES
  hermes.cpp
  hermes_tracing.cpp
  hermes_tracing_compat.cpp
  DebuggerAPI.cpp
  SynthTrace.cpp
  SynthTraceParser.cpp
  TraceInterpreter.cpp
  TracingRuntime.cpp
  CompileJS.cpp
  hermes_dbg.cpp
  ws_session.cpp
  )

set(api_sources
  hermes.cpp
  DebuggerAPI.cpp
  hermes_tracing_compat.cpp 
  )

if (HERMES_ENABLE_INSPECTOR_WIN)
list(APPEND api_sources ws_session.cpp hermes_dbg.cpp)
include_directories(../../chrome_inspector/react-native-src/ReactCommon/)
endif()

include_directories(../../etw/)

add_llvm_library(hermesapi
        ${api_sources}
        LINK_LIBS jsi hermesVMRuntime)

if (HERMES_ENABLE_INSPECTOR_WIN)
target_compile_options(hermesapi PUBLIC "/permissive-")
endif()

set(hermesapi_compile_flags "")
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR
    "${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
  string(APPEND hermesapi_compile_flags " -Wno-non-virtual-dtor")
endif()


set_property(TARGET hermesapi PROPERTY
  CXX_STANDARD 14)
set_property(TARGET hermesapi APPEND_STRING PROPERTY
  COMPILE_FLAGS ${hermesapi_compile_flags})
set_property(TARGET hermesapi APPEND_STRING PROPERTY
  COMPILE_DEFINITIONS "JSI_CREATE_SHARED_LIBRARY")

set(LLVM_LINK_COMPONENTS
  Core
  Support
  )

# Disable EH and RTTI for compileJS
set(LLVM_REQUIRES_EH OFF)
set(LLVM_REQUIRES_RTTI OFF)

add_llvm_library(compileJS STATIC CompileJS.cpp)

# Restore EH and RTTI (Note: At the time of writing, there is no usage of
# add_llvm_library either after this line in this file or in a sub directory.
# However, the values are restored anyways for the sake of sanity.)
set(LLVM_REQUIRES_EH ON)
set(LLVM_REQUIRES_RTTI ON)

add_library(libhermes SHARED ${api_sources})

set (libhermes_target_link_libraries
  jsi
  hermesVMRuntime
  ${CORE_FOUNDATION}
)

if (HERMES_ENABLE_INSPECTOR_WIN)
list(APPEND libhermes_target_link_libraries chrome_inspector)

target_compile_options(libhermes PUBLIC "/permissive-")
endif()

target_link_libraries(libhermes
  ${libhermes_target_link_libraries}
)

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR
    "${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
  # Export all API symbols
  set(compile_flags "-fvisibility=default")
  # The linker doesn't support -flto -Os, so make sure it's linked with -O3.
  # This only affects linking, not compilation, so without -flto it's a no-op.
  # In other words, if the compiler flags are -O0 -fno-lto, then the resulting
  # library will be -O0 in spite of setting -O3 here.
  set(link_flags "-O3 ${OPTIONAL_GC_SECTIONS} ${OPTIONAL_JNI_ONLOAD}")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  # Same as above, but for windows. Note that there is no equivalent of -fvisibility=default.
  set(compile_flags "")
  set(link_flags "/O2 ${OPTIONAL_GC_SECTIONS} ${OPTIONAL_JNI_ONLOAD}")
endif()

set_target_properties(libhermes PROPERTIES
  COMPILE_FLAGS "${compile_flags}"
  LINK_FLAGS "${link_flags}"
  # Declare our dependency on std::make_unique
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED 14
  # Avoid becoming liblibhermes (and there's already a target called 'hermes')
  OUTPUT_NAME hermes
  )
hermes_link_icu(libhermes)
