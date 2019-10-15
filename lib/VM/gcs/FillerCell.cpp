/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "hermes/VM/FillerCell.h"

#include "hermes/VM/BuildMetadata.h"

#include "llvm/Support/Debug.h"
#define DEBUG_TYPE "serialize"

namespace hermes {
namespace vm {

const VTable FillerCell::vt{CellKind::FillerCellKind, 0};

// Empty to prevent linker errors, doesn't need to do anything.
void UninitializedBuildMeta(const GCCell *, Metadata::Builder &) {}
void FillerCellBuildMeta(const GCCell *, Metadata::Builder &) {}

void UninitializedSerialize(Serializer &s, const GCCell *cell) {
  LLVM_DEBUG(
      llvm::dbgs() << "Serialize function not implemented for Uinitialized\n");
}

void FillerCellSerialize(Serializer &s, const GCCell *cell) {
  auto *self = vmcast<const FillerCell>(cell);
  s.writeInt<uint32_t>(self->getSize());
  s.endObject(self);
}

void UninitializedDeserialize(Deserializer &d, CellKind kind) {
  LLVM_DEBUG(
      llvm::dbgs()
      << "Deserialize function not implemented for Uninitialized\n");
}

void FillerCellDeserialize(Deserializer &d, CellKind kind) {
  assert(kind == CellKind::FillerCellKind && "Expected FillerCell");
  uint32_t size = d.readInt<uint32_t>();
  FillerCell *cell = FillerCell::create(d.getRuntime(), size);
  d.endObject((void *)cell);
}

} // namespace vm
} // namespace hermes
