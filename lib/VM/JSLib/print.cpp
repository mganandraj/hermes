/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "JSLibInternal.h"

#include "hermes/VM/Operations.h"
#include "hermes/VM/StringView.h"

#include "llvh/Support/raw_ostream.h"

#include <android/log.h>

namespace hermes {
namespace vm {

/// Convert all arguments to string and print them followed by new line.
CallResult<HermesValue> print(void *, Runtime *runtime, NativeArgs args) {
  GCScope scope(runtime);
  auto marker = scope.createMarker();
  bool first = true;

  for (Handle<> arg : args.handles()) {
    scope.flushToMarker(marker);
    auto res = toString_RJS(runtime, arg);
    if (res == ExecutionStatus::EXCEPTION)
      return ExecutionStatus::EXCEPTION;

    if (!first)
      llvh::outs() << " ";
    SmallU16String<32> tmp;
    llvh::outs() << StringPrimitive::createStringView(
                        runtime, runtime->makeHandle(std::move(*res)))
                        .getUTF16Ref(tmp);

    UTF16Ref str = StringPrimitive::createStringView(
            runtime, runtime->makeHandle(std::move(*res)))
            .getUTF16Ref(tmp);

    std::string str8 (str.begin(), str.end());
    __android_log_print(ANDROID_LOG_ERROR,"HERMES_PRINT", "%s", str8.c_str()); \

    first = false;
  }

  llvh::outs() << "\n";
  llvh::outs().flush();
  return HermesValue::encodeUndefinedValue();
}

} // namespace vm
} // namespace hermes
