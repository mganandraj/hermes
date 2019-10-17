set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_llvm_library(chrome_inspector STATIC
        react-native-src/ReactCommon/hermes/inspector/inspector.cpp
        react-native-src/ReactCommon/hermes/inspector/InspectorState.cpp
        react-native-src/ReactCommon/hermes/inspector/RuntimeAdapter.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/AutoAttachUtils.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/Connection.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/ConnectionDemux.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/MessageConverters.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/MessageTypes.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/Registration.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/RemoteObjectsTable.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/tests/AsyncHermesRuntime.cpp
        react-native-src/ReactCommon/hermes/inspector/chrome/tests/SyncConnection.cpp
        react-native-src/ReactCommon/hermes/inspector/detail/SerialExecutor.cpp
        react-native-src/ReactCommon/hermes/inspector/detail/Thread.cpp)

include_directories(./react-native-src/ReactCommon/
        ../API/
        ../API/jsi/
)

list(APPEND chrome_inspector_compile_flags "/EHsc")
list(APPEND chrome_inspector_compile_flags "/permissive-")

target_compile_options(chrome_inspector PUBLIC ${chrome_inspector_compile_flags})