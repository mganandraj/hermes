
#include <windows.h>#include <windows.h>/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/Platform/Logging.h"

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#ifdef __ANDROID__
#include <android/log.h>
#elif defined(__APPLE__)
#include <asl.h>
#elif defined(_MSC_VER)
#define NOMINMAX
#include <hermes_etw.h>
#include <windows.h>
#include <cstdio>
#else
#include <cstdio>
#endif

#include <cstdarg>

namespace hermes {

void hermesLog(const char *componentName, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
#ifdef __ANDROID__
  __android_log_vprint(ANDROID_LOG_INFO, componentName, fmt, args);
#elif defined(__APPLE__)
  asl_vlog(NULL, NULL, ASL_LEVEL_NOTICE, fmt, args);
#elif defined(_MSC_VER)
  if (EventEnabledGENERIC_EVENT()) { // Avoid expensive potentially expensive string formatting if provider not enabled.
    char buffer[256];
    vsprintf_s(buffer, fmt, args);
    EventWriteGENERIC_EVENT(
        "generic",
        componentName,
        buffer,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);
  }
#else
  fprintf(stderr, "%s: ", componentName);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
#endif
  va_end(args);
}

} // namespace hermes
