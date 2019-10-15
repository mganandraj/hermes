/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#ifdef HERMESVM_SERIALIZE
#include "hermes/VM/Deserializer.h"
#include "hermes/VM/GCPointer-inline.h"
#include "hermes/VM/GCPointer.h"
#include "hermes/VM/JSArrayBuffer.h"
#include "hermes/VM/JSDataView.h"
#include "hermes/VM/JSNativeFunctions.h"
#include "hermes/VM/JSTypedArray.h"
#include "hermes/VM/JSWeakMapImpl.h"
#include "hermes/VM/PrimitiveBox.h"
#include "hermes/VM/Runtime.h"

#include "JSLib/JSLibInternal.h"

#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "serialize"

namespace hermes {
namespace vm {

using DeserializeCallBack = void(Deserializer &d, CellKind kind);

static DeserializeCallBack *deserializeImpl[] = {
#define CELL_KIND(name) name##Deserialize,
#include "hermes/VM/CellKinds.def"
#undef CELL_KIND
};

void Deserializer::deserializeCell(uint8_t kind) {
  deserializeImpl[kind](*this, (CellKind)kind);
}

void Deserializer::flushRelocationQueue() {
  while (!relocationQueue_.empty()) {
    auto entry = relocationQueue_.front();
    relocationQueue_.pop_front();
    assert(entry.id < objectTable_.size() && "invalid relocation id");
    void *ptr = objectTable_[entry.id];
    assert(ptr && "pointer relocation cannot be resolved");
    updateAddress(entry.address, ptr, entry.kind);
  }
}

void Deserializer::init() {
  // Do the sanity check of the header first.
  readHeader();

  // Relocation table size and string buffers are all at the end of the
  // MemoryBuffer. Let's start reading from the back.
  const char *ptr = buffer_->getBufferEnd();

  uint32_t size;
  // Read map size and resize relocation table.
  size = readBackwards(ptr);
  objectTable_.resize(size);

  // Read size of char16Buf_
  size = readBackwards(ptr);
  // Move ptr to the beginning of char16Buf_.
  ptr -= size;
  if (size > 0) {
    // Has char16Buf_, reconstruct the buffer here.
    assert(ptr >= buffer_->getBufferStart() && "wrong char16Buf_ size");
    // \p size is buffer size in bytes. Let's calculate the end first before
    // casting to char16_t *.
    char16Buf_ = ArrayRef<char16_t>(
        (const char16_t *)ptr, (const char16_t *)(ptr + size));
  }

  // Read size of charBuf_.
  size = readBackwards(ptr);
  // Move ptr to the beginning of charBuf_.
  ptr -= size;
  if (size > 0) {
    // Has charBuf_, reconstruct the buffer here.
    assert(ptr >= buffer_->getBufferStart() && "wrong charBuf_ size");
    charBuf_ = ArrayRef<char>(ptr, size);
  }

  // Map nullptr to 0.
  objectTable_[0] = 0;

  // Populate relocation table for native functions and constructors.
  size_t idx = 1;
#define NATIVE_FUNCTION(func)                                                \
  assert(!objectTable_[idx]);                                                \
  objectTable_[idx] = (void *)func;                                          \
  LLVM_DEBUG(                                                                \
      llvm::dbgs() << idx << ", " << #func << ", " << (void *)func << "\n"); \
  idx++;

#define NATIVE_FUNCTION_TYPED(func, type)                           \
  assert(!objectTable_[idx]);                                       \
  objectTable_[idx] = (void *)func<type>;                           \
  LLVM_DEBUG(                                                       \
      llvm::dbgs() << idx << ", " << #func << "<" << #type << ">, " \
                   << (void *)func<type> << "\n");                  \
  idx++;

#define NATIVE_FUNCTION_TYPED_2(func, type, type2)                           \
  assert(!objectTable_[idx]);                                                \
  objectTable_[idx] = (void *)func<type, type2>;                             \
  LLVM_DEBUG(                                                                \
      llvm::dbgs() << idx << ", " << #func << "<" << #type << ", " << #type2 \
                   << ">, " << ((void *)func<type, type2>) << "\n");         \
  idx++;

  using CreatorFunction = CallResult<HermesValue>(Runtime *, Handle<JSObject>);
  CreatorFunction *funcPtr;
#define NATIVE_CONSTRUCTOR(func)                                      \
  funcPtr = func;                                                     \
  assert(!objectTable_[idx]);                                         \
  objectTable_[idx] = (void *)funcPtr;                                \
  LLVM_DEBUG(                                                         \
      llvm::dbgs() << idx << ", " << #func << ", " << (void *)funcPtr \
                   << "\n");                                          \
  idx++;

#define NATIVE_CONSTRUCTOR_TYPED(classname, type, type2, func)            \
  funcPtr = classname<type, type2>::func;                                 \
  assert(!objectTable_[idx]);                                             \
  objectTable_[idx] = (void *)funcPtr;                                    \
  LLVM_DEBUG(                                                             \
      llvm::dbgs() << idx << ", " << #classname << "<" << #type << ", "   \
                   << #type2 << ">::" << #func << ", " << (void *)funcPtr \
                   << "\n");                                              \
  idx++;
#include "hermes/VM/NativeFunctions.def"
#undef NATIVE_CONSTRUCTOR
}

void Deserializer::readHeader() {
  SerializeHeader readHeader;
  readData(&readHeader, sizeof(SerializeHeader));

  if (readHeader.magic != SD_MAGIC) {
    hermes_fatal("Not a serialize file or endianness do not match");
  }
  if (readHeader.version != SD_HEADER_VERSION) {
    hermes_fatal("Serialize header versions do not match");
  }
  if (readHeader.nativeFunctionTableVersion != NATIVE_FUNCTION_VERSION) {
    hermes_fatal("Native function table versions do not match");
  }
  if (runtime_->getHeap().size() < readHeader.heapSize) {
    hermes_fatal(
        (llvm::Twine("Deserialize heap size less than Serialize heap size(") +
         llvm::StringRef(std::to_string(readHeader.heapSize)) +
         llvm::Twine(" bytes), try increase initial heap size"))
            .str());
  }

#define CHECK_HEADER_SET(header, field)                         \
  if (!header.field) {                                          \
    hermes_fatal("Serialize/Deserialize configs do not match"); \
  }

#define CHECK_HEADER_UNSET(header, field)                       \
  if (header.field) {                                           \
    hermes_fatal("Serialize/Deserialize configs do not match"); \
  }

#ifndef NDEBUG
  CHECK_HEADER_SET(readHeader, isDebug); // isDebug
#else
  CHECK_HEADER_UNSET(readHeader, isDebug);
#endif

#ifdef HERMES_ENABLE_DEBUGGER
  CHECK_HEADER_SET(readHeader, isEnableDebugger); // isEnableDebugger.
#else
  CHECK_HEADER_UNSET(readHeader, isEnableDebugger);
#endif

  runtime_->checkHeaderRuntimeConfig(readHeader);
}

void Deserializer::readAndCheckOffset() {
  uint32_t currentOffset = offset_;
  uint32_t bytes = readInt<uint32_t>();
  if (currentOffset != bytes) {
    hermes_fatal("Deserializer sanity check failed: offset don't match");
  }
}

void Deserializer::updateAddress(
    void *address,
    void *ptrVal,
    RelocationKind kind) {
  switch (kind) {
    case RelocationKind::NativePointer:
      *(void **)address = ptrVal;
      break;
    case RelocationKind::GCPointer:
      ((GCPointerBase *)address)->set(runtime_, ptrVal, &runtime_->getHeap());
      break;
    case RelocationKind::HermesValue:
      ((HermesValue *)address)->unsafeUpdatePointer(ptrVal);
      break;
    default:
      llvm_unreachable("Invalid relocation kind");
  }
}

} // namespace vm
} // namespace hermes
#endif
