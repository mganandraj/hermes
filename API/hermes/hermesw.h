#pragma once

#include <jsi/jsi.h>

__declspec(dllexport)
    std::unique_ptr<facebook::jsi::Runtime> makeDebugHermesRuntime();