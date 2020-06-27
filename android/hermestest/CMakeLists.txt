# Sets the minimum version of CMake required to build your native library.
# This ensures that a certain set of CMake features is available to
# your build.

cmake_minimum_required(VERSION 3.4.1)

# Specifies a library name, specifies whether the library is STATIC or
# SHARED, and provides relative paths to the source code. You can
# define multiple libraries by adding multiple add_library() commands,
# and CMake builds them for you. When you build your app, Gradle
# automatically packages shared libraries with your APK.

add_library( hermes
        SHARED
        IMPORTED )

set_target_properties( # Specifies the target library.
        hermes

        # Specifies the parameter you want to define.
        PROPERTIES IMPORTED_LOCATION

        # Provides the path to the library you want to import.
        /home/mganandraj/code/hermes_ws/build_android/hermes/intermediates/cmake/debug/obj/${ANDROID_ABI}/libhermes.so )

include_directories(
        /home/mganandraj/code/hermes_ws/hermes/API
        /home/mganandraj/code/hermes_ws/hermes/API/jsi
        /home/mganandraj/code/hermes_ws/hermes/public
)


add_library( # Specifies the name of the library.
        native-lib

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        src/main/cpp/native-lib.cpp
        /home/mganandraj/code/hermes_ws/hermes/API/jsi/jsi/jsi.cpp
        )

target_link_libraries( native-lib hermes )



## FBJNI stuff


# set(build_DIR ${CMAKE_SOURCE_DIR}/build)
set(build_DIR /home/mganandraj/code/hermes_ws/build_android/hermestest)

file(GLOB libfbjni_link_DIRS "${build_DIR}/fbjni*.aar/jni/${ANDROID_ABI}")
file(GLOB libfbjni_include_DIRS "${build_DIR}/fbjni-*-headers.jar/")

find_library(FBJNI_LIBRARY fbjni PATHS ${libfbjni_link_DIRS} NO_CMAKE_FIND_ROOT_PATH)

#target_include_directories(${PACKAGE_NAME} PRIVATE
#  // Additional header directories here
#  ${libfbjni_include_DIRS}
#)
# target_link_libraries(${PACKAGE_NAME} ${FBJNI_LIBRARY} mylibrary)


#### FBJNI end


add_library( # Specifies the name of the library.
        jsijni

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        /home/mganandraj/code/hermes_ws/hermes/android/test/java/com/facebook/hermes/test/HermesEpilogue.cpp
        /home/mganandraj/code/hermes_ws/hermes/android/test/java/com/facebook/hermes/test/HermesRuntimeFactory.cpp
        /home/mganandraj/code/hermes_ws/hermes/android/test/java/com/facebook/hermes/test/JSRuntime.cpp
        /home/mganandraj/code/hermes_ws/hermes/android/test/java/com/facebook/hermes/test/MappedFileBuffer.cpp
        /home/mganandraj/code/hermes_ws/hermes/android/test/java/com/facebook/hermes/test/OnLoad.cpp
#        /home/mganandraj/code/hermes_ws/hermes/android/test/java/com/facebook/hermes/test/OnLoadEpilogue.cpp
        /home/mganandraj/code/hermes_ws/hermes/API/jsi/jsi/jsi.cpp
        )

target_include_directories(jsijni PRIVATE
  // Additional header directories here
  ${libfbjni_include_DIRS}
  /home/mganandraj/code/hermes_ws/hermes/android/test/java/com/facebook/hermes/test
)
target_link_libraries(jsijni ${FBJNI_LIBRARY} hermes)