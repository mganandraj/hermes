/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#ifndef HERMES_VM_DICTPROPERTYMAP_H
#define HERMES_VM_DICTPROPERTYMAP_H

#include "hermes/VM/IdentifierTable.h"
#include "hermes/VM/PropertyDescriptor.h"
#include "hermes/VM/Runtime.h"

#include "llvm/Support/TrailingObjects.h"

namespace hermes {
namespace vm {

/// This class provides simple property metadata storage for JavaScript
/// objects. It maps from SymbolID to PropertyDescriptor and provides
/// iteration in insertion order.
///
/// The object contains two data structures:
/// - an open addressing hash table mapping from SymbolID to an integer index.
/// - a descriptor array containing pairs of SymbolID and PropertyDescriptor.
///
/// Fast property lookup is supported by the hash table - it maps from a
/// SymbolID to an index in the descriptor array.
///
/// New properties are inserted in the hash table and appended sequentially to
/// the end of the descriptor array, thus encoding the original insertion order.
///
/// Deleted properties are removed from the hash table and marked by a "deleted"
/// SymbolID in the descriptor array. Then the desctiptor is added to a list of
/// deleted property slots - PropertyDescriptor::flags is overloaded to serve as
/// the "next deleted" link. We remove an entry from the "deleted" list when we
/// need to allocate a slot for a new property - then finally the entry is
/// marked with an "invalid" SymbolID.
///
/// Iteration simply walks the descriptor array from start to end, skipping
/// deleted and invalid properties, preserving the original insertion order.
///
/// The object has to be reallocated when any of these conditions occur:
/// - the descriptor array is full (it never shrinks, even after deletions)
/// - the hash table occupancy is above a certain threshold (note that deletions
///   don't decrease the hash table occupancy).
///
/// Reallocation first scans the descriptor array and inserts valid (and not
/// deleted) properties in the new hash table and descriptor array. We must also
/// preserve the list of deleted properties, so then it walks the deleted list
/// and appends the descriptors to the new desctiptor array.
///
/// A property descriptor is always in one of these states:
///  - "uninitialized". It is beyond \c numDescriptors.
///  - "valid". It contains a valid SymbolID and descriptor.
///  - "deleted". It contains ReservedSymbolId::deleted and is part of the
///  "deleted" list.
///  - "invalid". It contains SymbolID::empty(). It used to be "deleted"
///  but its slot was re-used by a new property.
///
class DictPropertyMap final : public VariableSizeRuntimeCell,
                              private llvm::TrailingObjects<
                                  DictPropertyMap,
                                  std::pair<SymbolID, NamedPropertyDescriptor>,
                                  std::pair<SymbolID, uint32_t>> {
  friend TrailingObjects;
  friend void DictPropertyMapBuildMeta(
      const GCCell *cell,
      Metadata::Builder &mb);

  using HashPair = std::pair<SymbolID, uint32_t>;

 public:
  using DescriptorPair = std::pair<SymbolID, NamedPropertyDescriptor>;

  using size_type = uint32_t;
  static const size_type DEFAULT_CAPACITY = 2;

  /// An opaque class representing a reference to a valid property in the
  /// property map.
  class PropertyPos {
    friend class DictPropertyMap;
    size_type hashPairIndex;

    PropertyPos(size_type hashPairIndex) : hashPairIndex(hashPairIndex) {}
    /// Default constructor so this class can be used in OptValue.
    PropertyPos() = default;
    friend class OptValue<PropertyPos>;
  };

  static VTable vt;

  static bool classof(const GCCell *cell) {
    return cell->getKind() == CellKind::DictPropertyMapKind;
  }

  /// Return the maximum possible capacity of DictPropMap.
  static size_type getMaxCapacity();

  /// Create an instance of DictPropertyMap with the specified capacity.
  static CallResult<PseudoHandle<DictPropertyMap>> create(
      Runtime *runtime,
      size_type capacity = DEFAULT_CAPACITY);

  /// Return the number of non-deleted properties in the map.
  size_type size() const {
    return numProperties_;
  }

  /// Call the supplied callback pass each property's \c SymbolID and \c
  /// NamedPropertyDescriptor as parameters.
  /// Obviously the callback shouldn't be doing naughty things like modifying
  /// the property map or creating new hidden classes (even implicitly).
  /// A marker for the current gcScope is obtained in the beginning and the
  /// scope is flushed after every callback.
  template <typename CallbackFunction>
  static void forEachProperty(
      Handle<DictPropertyMap> selfHandle,
      Runtime *runtime,
      const CallbackFunction &callback);

  /// Same as forEachProperty() but the callback returns true to continue or
  /// false to stop immediately.
  /// A marker for the current gcScope is obtained in the beginning and the
  /// scope is flushed after every callback.
  /// \return false if the callback returned false, true otherwise.
  template <typename CallbackFunction>
  static bool forEachPropertyWhile(
      Handle<DictPropertyMap> selfHandle,
      Runtime *runtime,
      const CallbackFunction &callback);

  /// Iterate over all the valid properties in the map, supplying a reference of
  /// the property descriptor to the \p callback. The callback is allowed to
  /// mutate the property descriptor.
  template <typename CallbackFunction>
  static void forEachMutablePropertyDescriptor(
      Handle<DictPropertyMap> selfHandle,
      Runtime *runtime,
      const CallbackFunction &callback);

  static DescriptorPair *getDescriptorPair(
      DictPropertyMap *self,
      PropertyPos pos);

  /// Find a property by \p id. On success return a reference to the found
  /// property.
  static OptValue<PropertyPos> find(const DictPropertyMap *self, SymbolID id);

  /// Find a property, or add it (with a random PropertyDescriptor) if it
  /// doesn't exist.
  /// \return a pair consisting of pointer to the property descriptor and a pool
  ///   denoting whether a new property was added.
  static CallResult<std::pair<NamedPropertyDescriptor *, bool>> findOrAdd(
      MutableHandle<DictPropertyMap> &selfHandleRef,
      Runtime *runtime,
      SymbolID id);

  /// Add a new property with id \p id and descriptor \p desc, which must not
  /// already exist. This method may cause reallocation, in which case the new
  /// address will be updated in \p selfHandleRef.
  /// \p selfHandleRef pointer to the self handle, which may be updated if the
  ///     object is re-allocated.
  static ExecutionStatus add(
      MutableHandle<DictPropertyMap> &selfHandleRef,
      Runtime *runtime,
      SymbolID id,
      NamedPropertyDescriptor desc);

  /// Remove the property at the specified position. This invalidates all
  /// positions.
  static void erase(DictPropertyMap *self, PropertyPos pos);

  /// Allocate a new property slot. Either pop the first entry in the deleted
  /// list, or, if the deleted list is empty, return slot \c numProperties_,
  /// which is the next slot at the end of the currently allocated storage.
  static SlotIndex allocatePropertySlot(DictPropertyMap *self);

  void dump();

  friend void DictPropertyMapSerialize(Serializer &s, const GCCell *cell);
  friend void DictPropertyMapDeserialize(Deserializer &d, CellKind kind);

 private:
  /// Total size of the descriptor array.
  const size_type descriptorCapacity_;
  /// Total size of the hash table. It will always be a power of 2.
  const size_type hashCapacity_;

  /// How many entries have been added to the descriptor array (including
  /// deleted).
  size_type numDescriptors_{0};

  /// Number of valid properties in the map.
  size_type numProperties_{0};

  /// A constant used to signify end of deleted list.
  static constexpr size_type END_OF_LIST =
      std::numeric_limits<size_type>::max();

  // Ensure that we can overload PropertyFlags::_flags to store the next
  // deleted index.
  static_assert(
      std::is_same<decltype(PropertyFlags::_flags), size_type>::value,
      "size_type must correspond to PropertyFlags::_flags");

  /// Index of the most recently deleted PropertyDescriptor. Its
  /// PropertyFlags::_flags field contains the index of the next deleted and so
  /// on.
  size_type deletedListHead_{END_OF_LIST};

  /// Number of entries in the deleted list.
  size_type deletedListSize_{0};

  /// Derive the size of the hash table so it can hold \p cap elements without
  /// many collisions. The result must also be a power of 2.
  static size_type calcHashCapacity(size_type cap) {
    assert(
        (cap <= std::numeric_limits<size_type>::max() / 4) &&
        "size will cause integer overflow in calcHashCapacity");

    return llvm::PowerOf2Ceil(cap * 4 / 3 + 1);
  }

  /// A const-expr version of \c calcHashCapacity() using 64-bit arithmetic.
  /// NOTE: it must not be used at runtime since it might be slow.
  static constexpr uint64_t constCalcHashCapacity64(uint64_t cap) {
    return constPowerOf2Ceil(cap * 4 / 3 + 1);
  }

  /// A constexpr compatible version of llvm::PowerOf2Ceil().
  /// NOTE: it must not be used at runtime since it might be slow.
  static constexpr uint64_t constPowerOf2Ceil(uint64_t A, uint64_t ceil = 1) {
    return ceil >= A ? ceil : constPowerOf2Ceil(A, ceil << 1);
  }

  /// Hash a symbol ID. For now it is the identity hash.
  static unsigned hash(SymbolID symbolID) {
    return symbolID.unsafeGetRaw();
  }

  DictPropertyMap(
      Runtime *runtime,
      size_type descriptorCapacity,
      size_type hashCapacity)
      : VariableSizeRuntimeCell(
            &runtime->getHeap(),
            &vt,
            allocationSize(descriptorCapacity, hashCapacity)),
        descriptorCapacity_(descriptorCapacity),
        hashCapacity_(hashCapacity) {
    // Clear the hash table.
    std::fill_n(getHashPairs(), hashCapacity_, HashPair{SymbolID::empty(), 0});
  }

  DescriptorPair *getDescriptorPairs() {
    return getTrailingObjects<DescriptorPair>();
  }
  const DescriptorPair *getDescriptorPairs() const {
    return getTrailingObjects<DescriptorPair>();
  }
  HashPair *getHashPairs() {
    return getTrailingObjects<HashPair>();
  }

  /// Store the next deleted index in a deleted descriptor pair. The index
  /// is stored in the PropertyFlags field.
  static void setNextDeletedIndex(
      DescriptorPair *descPair,
      size_type nextIndex) {
    assert(
        descPair->first == SymbolID::deleted() &&
        "Descriptor pair is not deleted");
    descPair->second.flags._flags = nextIndex;
  }

  /// Obtain the previous deleted index from a deleted descriptor pair. The
  /// index is kept in the PropertyFlags field.
  static size_type getNextDeletedIndex(const DescriptorPair *descPair) {
    assert(
        descPair->first == SymbolID::deleted() &&
        "Descriptor pair is not deleted");
    return descPair->second.flags._flags;
  }

  /// A placeholder function to keep track of the number of deleted entries
  /// in the hash table. We don't actually need to do it for now.
  LLVM_ATTRIBUTE_ALWAYS_INLINE void incDeletedHashCount() {}
  /// A placeholder function to keep track of the number of deleted entries
  /// in the hash table. We don't actually need to do it for now.
  LLVM_ATTRIBUTE_ALWAYS_INLINE void decDeletedHashCount() {}

  /// Search the hash table for \p symbolID. If found, return true and the
  /// and a pointer to the hash pair. If not found, return false and a pointer
  /// to the hash pair where it ought to be inserted.
  std::pair<bool, HashPair *> static lookupEntryFor(
      DictPropertyMap *self,
      SymbolID symbolID);

  /// Allocate a new property map with the specified capacity, copy the existing
  /// valid entries into it. If the specified capacity exceeds the maximum
  /// possible capacity, an exception will be raised.
  /// \param[in,out] selfHandleRef the original object handle on input, the new
  ///   object handle on output.
  /// \param newCapacity the capacity of the new object's descriptor array.
  static ExecutionStatus grow(
      MutableHandle<DictPropertyMap> &selfHandleRef,
      Runtime *runtime,
      size_type newCapacity);

  /// Gets the amount of memory required by this object for a given capacity.
  static uint32_t allocationSize(
      size_type descriptorCapacity,
      size_type hashCapacity) {
    return totalSizeToAlloc<DescriptorPair, HashPair>(
        descriptorCapacity, hashCapacity);
  }

  /// Calculate the maximum capacity of DictPropertyMap at compile time using
  /// binary search in the solution space, since we can't solve the equation
  /// directly.
  ///
  /// Some of these constexpr functions can be made more readable and or more
  /// efficient c++14 by using variables (T31421960).
  /// @{

  /// The maximum alignment padding a compiler might insert before a field or at
  /// the end of a struct. We use this for a conservative (but reasonable)
  /// estimate of the allocation size of the object.
  static constexpr uint64_t kAlignPadding = alignof(std::max_align_t) - 1;

  /// Calculate a conservative approximate size of DictPropertyMap in bytes,
  /// given a capacity. The calculation is performed using 64-bit arithmetic to
  /// avoid overflow.
  /// NOTE: it must not be used at runtime since it might be slow.
  static constexpr uint64_t constApproxAllocSize64(uint32_t cap) {
    static_assert(
        alignof(DictPropertyMap) <= kAlignPadding + 1,
        "DictPropertyMap exceeds supported alignment");
    static_assert(
        alignof(DictPropertyMap::DescriptorPair) <= kAlignPadding + 1,
        "DictPropertyMap::DescriptorPair exceeds supported alignment");
    static_assert(
        alignof(DictPropertyMap::HashPair) <= kAlignPadding + 1,
        "DictPropertyMap::HashPair exceeds supported alignment");

    return sizeof(DictPropertyMap) + kAlignPadding +
        sizeof(DictPropertyMap::DescriptorPair) * (uint64_t)cap +
        kAlignPadding +
        sizeof(DictPropertyMap::HashPair) * constCalcHashCapacity64(cap) +
        kAlignPadding;
  }

  /// Return true if DictPropertyMap with the specified capacity is guaranteed
  /// to fit within the GC's maxumum allocation size. The check is conservative:
  /// it might a few return false negatives at the end of the range.
  /// NOTE: it must not be used at runtime since it might be slow.
  static constexpr bool constWouldFitAllocation(uint32_t cap) {
    return constApproxAllocSize64(cap) <= GC::maxAllocationSize();
  }

  /// In the range of capacity values [first ... first + len), find the largest
  /// value for which wouldFitAllocation() returns true.
  /// NOTE: it must not be used at runtime since it might be slow.
  static constexpr uint32_t constFindMaxCapacity(uint32_t first, uint32_t len) {
    return len == 0
        ? first - 1
        : (constWouldFitAllocation(first + len / 2)
               ? constFindMaxCapacity(first + len / 2 + 1, len - len / 2 - 1)
               : constFindMaxCapacity(first, len / 2));
  }

  /// A place to put things in order to avoid restructins on using constexpr
  /// functions declared in the same class.
  struct detail;
  friend struct detail;

  /// @}

 protected:
  size_t numTrailingObjects(OverloadToken<DescriptorPair>) const {
    return descriptorCapacity_;
  }
};

//===----------------------------------------------------------------------===//
// DictPropertyMap inline methods.

template <typename CallbackFunction>
void DictPropertyMap::forEachProperty(
    Handle<DictPropertyMap> selfHandle,
    Runtime *runtime,
    const CallbackFunction &callback) {
  GCScopeMarkerRAII gcMarker{runtime};
  for (size_type i = 0, e = selfHandle->numDescriptors_; i != e; ++i) {
    auto const *descPair = selfHandle->getDescriptorPairs() + i;
    if (descPair->first.isValid()) {
      callback(descPair->first, descPair->second);
      gcMarker.flush();
    }
  }
}

template <typename CallbackFunction>
bool DictPropertyMap::forEachPropertyWhile(
    Handle<DictPropertyMap> selfHandle,
    Runtime *runtime,
    const CallbackFunction &callback) {
  GCScopeMarkerRAII gcMarker{runtime};
  for (size_type i = 0, e = selfHandle->numDescriptors_; i != e; ++i) {
    auto const *descPair = selfHandle->getDescriptorPairs() + i;
    if (descPair->first.isValid()) {
      if (!callback(runtime, descPair->first, descPair->second))
        return false;
      gcMarker.flush();
    }
  }
  return true;
}

template <typename CallbackFunction>
void DictPropertyMap::forEachMutablePropertyDescriptor(
    Handle<DictPropertyMap> selfHandle,
    Runtime *runtime,
    const CallbackFunction &callback) {
  for (size_type i = 0, e = selfHandle->numDescriptors_; i != e; ++i) {
    auto *descPair = selfHandle->getDescriptorPairs() + i;
    if (descPair->first.isValid()) {
      callback(descPair->second);
    }
  }
}

inline DictPropertyMap::DescriptorPair *DictPropertyMap::getDescriptorPair(
    DictPropertyMap *self,
    PropertyPos pos) {
  assert(
      pos.hashPairIndex < self->hashCapacity_ && "property pos out of range");

  auto *hashPair = self->getHashPairs() + pos.hashPairIndex;
  assert(hashPair->first.isValid() && "accessing invalid property");

  auto descIndex = hashPair->second;
  assert(descIndex < self->numDescriptors_ && "descriptor index out of range");

  auto *res = self->getDescriptorPairs() + descIndex;
  assert(
      res->first == hashPair->first && "accessing incorrect descriptor pair");

  return res;
}

inline OptValue<DictPropertyMap::PropertyPos> DictPropertyMap::find(
    const DictPropertyMap *self,
    SymbolID id) {
  // We want to keep the public interface of find() clean, so it accepts a
  // const self pointer, but internally we don't want to duplicate the code
  // only to propagate the "constness". So we cast the const away and promise
  // not to mutate it.
  auto *mutableSelf = const_cast<DictPropertyMap *>(self);
  auto found = lookupEntryFor(mutableSelf, id);
  if (!found.first)
    return llvm::None;
  return PropertyPos{(size_type)(found.second - mutableSelf->getHashPairs())};
}

inline ExecutionStatus DictPropertyMap::add(
    MutableHandle<DictPropertyMap> &selfHandleRef,
    Runtime *runtime,
    SymbolID id,
    NamedPropertyDescriptor desc) {
  auto found = findOrAdd(selfHandleRef, runtime, id);
  if (LLVM_UNLIKELY(found == ExecutionStatus::EXCEPTION)) {
    return ExecutionStatus::EXCEPTION;
  }
  assert(found->second && "trying to add an existing property");
  *found->first = desc;
  return ExecutionStatus::RETURNED;
}

} // namespace vm
} // namespace hermes

#endif // HERMES_VM_DICTPROPERTYMAP_H
