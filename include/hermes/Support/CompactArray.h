/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#ifndef HERMES_SUPPORT_COMPACTARRAY_H
#define HERMES_SUPPORT_COMPACTARRAY_H

#include "hermes/Support/CheckedMalloc.h"

#include <cassert>
#include <limits>

namespace hermes {

/// Array of uint32_t, but size-optimized for small values.
///
/// The underlying storage type is automatically scaled up to accommodate
/// inserted values. The number of elements cannot be changed, except by
/// swapping contents with another instance.
class CompactArray {
 public:
  enum Scale { UINT8 = 0, UINT16 = 1, UINT32 = 2 };

  /// Array with \p count zeroes.
  CompactArray(uint32_t count, Scale initScale = UINT8)
      : size_(count),
        scale_(initScale),
        raw_(static_cast<char *>(checkedCalloc(count, 1 << initScale))) {}
  ~CompactArray() {
    ::free(raw_);
  }
  /// swap is the only bulk transfer method.
  CompactArray(const CompactArray &) = delete;
  CompactArray &operator=(const CompactArray &) = delete;
  void swap(CompactArray &other) {
    std::swap(size_, other.size_);
    std::swap(scale_, other.scale_);
    std::swap(raw_, other.raw_);
  }
  uint32_t get(uint32_t idx) const {
    assert(idx < size_);
    switch (scale_) {
      case UINT8:
        return Fixed<uint8_t>::get(raw_, idx);
      case UINT16:
        return Fixed<uint16_t>::get(raw_, idx);
      case UINT32:
        return Fixed<uint32_t>::get(raw_, idx);
    }
  }
  /// Set the element at index \p idx to \p value.
  void set(uint32_t idx, uint32_t value) {
    // Note: Scales up twice if value > 64k and current scale is UINT8.
    // We assume this happens rarely.
    while (!trySet(idx, value))
      scaleUp();
  }
  uint32_t size() const {
    return size_;
  }
  Scale getCurrentScale() const {
    return scale_;
  }

 private:
  template <typename T>
  struct Fixed {
    static uint32_t get(char *raw, uint32_t idx) {
      return reinterpret_cast<T *>(raw)[idx];
    }
    /// Returns true iff \p value fits into T.
    static bool trySet(char *raw, uint32_t idx, uint32_t value) {
      if (value > std::numeric_limits<T>::max())
        return false;
      reinterpret_cast<T *>(raw)[idx] = static_cast<T>(value);
      return true;
    }
  };
  /// Upgrade to next scale.
  void scaleUp();
  /// Returns true iff \p value fit at current scale.
  bool trySet(uint32_t idx, uint32_t value) {
    assert(idx < size_);
    switch (scale_) {
      case UINT8:
        return Fixed<uint8_t>::trySet(raw_, idx, value);
      case UINT16:
        return Fixed<uint16_t>::trySet(raw_, idx, value);
      case UINT32:
        return Fixed<uint32_t>::trySet(raw_, idx, value);
    }
  }
  /// Number of elements.
  uint32_t size_;
  /// Underlying element storage type.
  Scale scale_;
  /// Contiguous allocation holding all elements.
  char *raw_;
};

} // namespace hermes

#endif // HERMES_SUPPORT_COMPACTARRAY_H
