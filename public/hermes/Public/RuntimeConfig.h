/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef HERMES_PUBLIC_RUNTIMECONFIG_H
#define HERMES_PUBLIC_RUNTIMECONFIG_H

#include "hermes/Public/CrashManager.h"
#include "hermes/Public/CtorConfig.h"
#include "hermes/Public/GCConfig.h"

#include <memory>

#ifdef HERMESVM_SERIALIZE
#include <vector>

namespace llvh {
class MemoryBuffer;
class raw_ostream;
} // namespace llvh
#endif

namespace hermes {
namespace vm {

enum CompilationMode {
  SmartCompilation,
  ForceEagerCompilation,
  ForceLazyCompilation
};

class PinnedHermesValue;
#ifdef HERMESVM_SERIALIZE
class Serializer;
class Deserializer;
#endif

// Parameters for Runtime initialisation.  Check documentation in README.md
// constexpr indicates that the default value is constexpr.
#define RUNTIME_FIELDS_BASE(F)                                                 \
  /* Parameters to be passed on to the GC. */                                  \
  F(HERMES_NON_CONSTEXPR, vm::GCConfig, GCConfig)                              \
                                                                               \
  /* Pre-allocated Register Stack */                                           \
  F(constexpr, PinnedHermesValue *, RegisterStack, nullptr)                    \
                                                                               \
  /* Register Stack Size */                                                    \
  F(constexpr, unsigned, MaxNumRegisters, 1024 * 1024)                         \
                                                                               \
  /* Whether or not the JIT is enabled */                                      \
  F(constexpr, bool, EnableJIT, false)                                         \
                                                                               \
  /* Whether to allow eval and Function ctor */                                \
  F(constexpr, bool, EnableEval, true)                                         \
                                                                               \
  /* Whether to verify the IR generated by eval and Function ctor */           \
  F(constexpr, bool, VerifyEvalIR, false)                                      \
                                                                               \
  /* Whether to optimize the code inside eval and Function ctor */             \
  F(constexpr, bool, OptimizedEval, false)                                     \
                                                                               \
  /* Support for ES6 Proxy. */                                                 \
  F(constexpr, bool, ES6Proxy, true)                                           \
                                                                               \
  /* Support for ES6 Symbol. */                                                \
  F(constexpr, bool, ES6Symbol, true)                                          \
                                                                               \
  /* Enable synth trace. */                                                    \
  F(constexpr, bool, TraceEnabled, false)                                      \
                                                                               \
  /* Scratch path for synth trace. */                                          \
  F(HERMES_NON_CONSTEXPR, std::string, TraceScratchPath, "")                   \
                                                                               \
  /* Result path for synth trace. */                                           \
  F(HERMES_NON_CONSTEXPR, std::string, TraceResultPath, "")                    \
                                                                               \
  /* Callout to register an interesting (e.g. lead to crash) */                \
  /* and completed trace. */                                                   \
  F(HERMES_NON_CONSTEXPR,                                                      \
    std::function<bool()>,                                                     \
    TraceRegisterCallback,                                                     \
    nullptr)                                                                   \
                                                                               \
  /* Enable sampling certain statistics. */                                    \
  F(constexpr, bool, EnableSampledStats, false)                                \
                                                                               \
  /* Whether to enable sampling profiler */                                    \
  F(constexpr, bool, EnableSampleProfiling, false)                             \
                                                                               \
  /* Whether to randomize stack placement etc. */                              \
  F(constexpr, bool, RandomizeMemoryLayout, false)                             \
                                                                               \
  /* Eagerly read bytecode into page cache. */                                 \
  F(constexpr, unsigned, BytecodeWarmupPercent, 0)                             \
                                                                               \
  /* Signal-based I/O tracking. Slows down execution. If enabled, */           \
  /* all bytecode buffers > 64 kB passed to Hermes must be mmap:ed. */         \
  F(constexpr, bool, TrackIO, false)                                           \
                                                                               \
  /* Enable contents of HermesInternal */                                      \
  F(constexpr, bool, EnableHermesInternal, true)                               \
                                                                               \
  /* Enable methods exposed to JS for testing */                               \
  F(constexpr, bool, EnableHermesInternalTestMethods, false)                   \
                                                                               \
  /* Allows Function.toString() to return the original source code */          \
  /* if available. For this to work code must have been compiled at */         \
  /* runtime with CompileFlags::allowFunctionToStringWithRuntimeSource set. */ \
  F(constexpr, bool, AllowFunctionToStringWithRuntimeSource, false)            \
                                                                               \
  /* Choose lazy/eager compilation mode. */                                    \
  F(constexpr,                                                                 \
    CompilationMode,                                                           \
    CompilationMode,                                                           \
    CompilationMode::SmartCompilation)                                         \
                                                                               \
  /* Choose whether generators are enabled. */                                 \
  F(constexpr, bool, EnableGenerator, true)                                    \
                                                                               \
  /* An interface for managing crashes. */                                     \
  F(HERMES_NON_CONSTEXPR,                                                      \
    std::shared_ptr<CrashManager>,                                             \
    CrashMgr,                                                                  \
    new NopCrashManager)                                                       \
                                                                               \
  /* The flags passed from a VM experiment */                                  \
  F(constexpr, uint32_t, VMExperimentFlags, 0)                                 \
  /* RUNTIME_FIELDS END */

#ifdef HERMESVM_SERIALIZE
using ExternalPointersVectorFunction = std::vector<void *>();
#define RUNTIME_FIELDS_SD(F)                                       \
  /* Should serialize after initialization */                      \
  F(HERMES_NON_CONSTEXPR,                                          \
    std::shared_ptr<llvh::raw_ostream>,                            \
    SerializeAfterInitFile,                                        \
    nullptr)                                                       \
  /* Should deserialize instead of initialization */               \
  F(HERMES_NON_CONSTEXPR,                                          \
    std::shared_ptr<llvh::MemoryBuffer>,                           \
    DeserializeFile,                                               \
    nullptr)                                                       \
  /* A function to get pointer values not visible to Runtime. e.g. \
   * function pointers defined in ConsoleHost*/                    \
  F(constexpr,                                                     \
    ExternalPointersVectorFunction *,                              \
    ExternalPointersVectorCallBack,                                \
    nullptr)

#define RUNTIME_FIELDS(F) \
  RUNTIME_FIELDS_BASE(F)  \
  RUNTIME_FIELDS_SD(F)
#else // ifndef HERMESVM_SERIALIZE
#define RUNTIME_FIELDS(F) RUNTIME_FIELDS_BASE(F)
#endif // HERMESVM_SERIALIZE

_HERMES_CTORCONFIG_STRUCT(RuntimeConfig, RUNTIME_FIELDS, {});

#undef RUNTIME_FIELDS

} // namespace vm
} // namespace hermes

#endif // HERMES_PUBLIC_RUNTIMECONFIG_H
