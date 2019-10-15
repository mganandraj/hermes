/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#define DEBUG_TYPE "class"
#include "hermes/VM/HiddenClass.h"

#include "hermes/VM/ArrayStorage.h"
#include "hermes/VM/JSArray.h"
#include "hermes/VM/JSObject.h"
#include "hermes/VM/Operations.h"
#include "hermes/VM/StringView.h"

#include "llvm/Support/Debug.h"

using llvm::dbgs;

namespace hermes {
namespace vm {

VTable HiddenClass::vt{CellKind::HiddenClassKind,
                       sizeof(HiddenClass),
                       _finalizeImpl,
                       _markWeakImpl,
                       _mallocSizeImpl};

void HiddenClassBuildMeta(const GCCell *cell, Metadata::Builder &mb) {
  const auto *self = static_cast<const HiddenClass *>(cell);
  mb.addField(&self->symbolID_);
  mb.addField("@parent", &self->parent_);
  mb.addField("@family", &self->family_);
  mb.addField("@propertyMap", &self->propertyMap_);
  mb.addField("@forInCache", &self->forInCache_);
}

void HiddenClassSerialize(Serializer &s, const GCCell *cell) {}

void HiddenClassDeserialize(Deserializer &d, CellKind kind) {}

void HiddenClass::_markWeakImpl(GCCell *cell, GC *gc) {
  auto *self = vmcast_during_gc<HiddenClass>(cell, gc);
  self->transitionMap_.markWeakRefs(gc);
}

void HiddenClass::_finalizeImpl(GCCell *cell, GC *) {
  auto *self = vmcast<HiddenClass>(cell);
  self->~HiddenClass();
}

size_t HiddenClass::_mallocSizeImpl(GCCell *cell) {
  auto *self = vmcast<HiddenClass>(cell);
  return self->transitionMap_.getMemorySize();
}

CallResult<HermesValue> HiddenClass::createRoot(Runtime *runtime) {
  return create(
      runtime,
      ClassFlags{},
      runtime->makeNullHandle<HiddenClass>(),
      SymbolID{},
      PropertyFlags{},
      0);
}

CallResult<HermesValue> HiddenClass::create(
    Runtime *runtime,
    ClassFlags flags,
    Handle<HiddenClass> parent,
    SymbolID symbolID,
    PropertyFlags propertyFlags,
    unsigned numProperties) {
  void *mem = runtime->allocLongLived<HasFinalizer::Yes>(sizeof(HiddenClass));
  return HermesValue::encodeObjectValue(new (mem) HiddenClass(
      runtime, flags, parent, symbolID, propertyFlags, numProperties));
}

Handle<HiddenClass> HiddenClass::convertToDictionary(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime) {
  assert(!selfHandle->isDictionary() && "class already in dictionary mode");

  auto newFlags = selfHandle->flags_;
  newFlags.dictionaryMode = true;

  /// Allocate a new class without a parent.
  auto newClassHandle = runtime->makeHandle<HiddenClass>(
      runtime->ignoreAllocationFailure(HiddenClass::create(
          runtime,
          newFlags,
          runtime->makeNullHandle<HiddenClass>(),
          SymbolID{},
          PropertyFlags{},
          selfHandle->numProperties_)));

  // Optionally allocate the property map and move it to the new class.
  if (LLVM_UNLIKELY(!selfHandle->propertyMap_))
    initializeMissingPropertyMap(selfHandle, runtime);

  newClassHandle->propertyMap_.set(
      runtime, selfHandle->propertyMap_.get(runtime), &runtime->getHeap());
  selfHandle->propertyMap_ = nullptr;

  LLVM_DEBUG(
      dbgs() << "Converted Class:" << selfHandle->getDebugAllocationId()
             << " to dictionary Class:"
             << newClassHandle->getDebugAllocationId() << "\n");

  return newClassHandle;
}

OptValue<HiddenClass::PropertyPos> HiddenClass::findProperty(
    PseudoHandle<HiddenClass> self,
    Runtime *runtime,
    SymbolID name,
    PropertyFlags expectedFlags,
    NamedPropertyDescriptor &desc) {
  // Lazily create the property map.
  if (LLVM_UNLIKELY(!self->propertyMap_)) {
    // If expectedFlags is valid, we can check if there is an outgoing
    // transition with name and the flags. The presence of such a transition
    // indicates that this is a new property and we don't have to build the map
    // in order to look for it (since we wouldn't find it anyway).
    if (expectedFlags.isValid()) {
      auto it = self->transitionMap_.find({name, expectedFlags});

      if (it != self->transitionMap_.end()) {
        LLVM_DEBUG(
            dbgs() << "Property " << runtime->formatSymbolID(name)
                   << " NOT FOUND in Class:" << self->getDebugAllocationId()
                   << " due to existing transition to Class:"
                   << it->second.get(runtime)->getDebugAllocationId() << "\n");

        return llvm::None;
      }
    }

    auto selfHandle = toHandle(runtime, std::move(self));
    initializeMissingPropertyMap(selfHandle, runtime);
    self = selfHandle;
  }

  auto found =
      DictPropertyMap::find(self->propertyMap_.getNonNull(runtime), name);
  if (!found)
    return llvm::None;

  desc = DictPropertyMap::getDescriptorPair(
             self->propertyMap_.get(runtime), *found)
             ->second;
  return *found;
}

bool HiddenClass::debugIsPropertyDefined(
    HiddenClass *self,
    Runtime *runtime,
    SymbolID name) {
  do {
    // If we happen to have a property map, use it.
    if (self->propertyMap_)
      return DictPropertyMap::find(self->propertyMap_.get(runtime), name)
          .hasValue();
    // Is the property defined in this class?
    if (self->symbolID_ == name)
      return true;
    self = self->parent_.get(runtime);
  } while (self);
  return false;
}

Handle<HiddenClass> HiddenClass::deleteProperty(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime,
    PropertyPos pos) {
  auto newHandle = LLVM_UNLIKELY(!selfHandle->flags_.dictionaryMode)
      ? convertToDictionary(selfHandle, runtime)
      : selfHandle;

  --newHandle->numProperties_;

  DictPropertyMap::erase(newHandle->propertyMap_.get(runtime), pos);

  LLVM_DEBUG(
      dbgs() << "Deleting from Class:" << selfHandle->getDebugAllocationId()
             << " produces Class:" << newHandle->getDebugAllocationId()
             << "\n");

  return newHandle;
}

CallResult<std::pair<Handle<HiddenClass>, SlotIndex>> HiddenClass::addProperty(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime,
    SymbolID name,
    PropertyFlags propertyFlags) {
  assert(propertyFlags.isValid() && "propertyFlags must be valid");

  if (LLVM_UNLIKELY(selfHandle->isDictionary())) {
    if (toArrayIndex(
            runtime->getIdentifierTable().getStringView(runtime, name))) {
      selfHandle->flags_.hasIndexLikeProperties = true;
    }

    // Allocate a new slot.
    // TODO: this changes the property map, so if we want to support OOM
    // handling in the future, and the following operation fails, we would have
    // to somehow be able to undo it, or use an approach where we peek the slot
    // but not consume until we are sure (which is less efficient, but more
    // robust). T31555339.
    SlotIndex newSlot = DictPropertyMap::allocatePropertySlot(
        selfHandle->propertyMap_.get(runtime));

    if (LLVM_UNLIKELY(
            addToPropertyMap(
                selfHandle,
                runtime,
                name,
                NamedPropertyDescriptor(propertyFlags, newSlot)) ==
            ExecutionStatus::EXCEPTION)) {
      return ExecutionStatus::EXCEPTION;
    }

    ++selfHandle->numProperties_;
    return std::make_pair(selfHandle, newSlot);
  }

  // Do we already have a transition for that property+flags pair?
  auto optChildHandle =
      selfHandle->transitionMap_.lookup(runtime, {name, propertyFlags});
  if (LLVM_LIKELY(optChildHandle)) {
    // If the child doesn't have a property map, but we do, update our map and
    // move it to the child.
    if (!optChildHandle.getValue()->propertyMap_ && selfHandle->propertyMap_) {
      LLVM_DEBUG(
          dbgs() << "Adding property " << runtime->formatSymbolID(name)
                 << " to Class:" << selfHandle->getDebugAllocationId()
                 << " transitions Map to existing Class:"
                 << optChildHandle.getValue()->getDebugAllocationId() << "\n");

      if (LLVM_UNLIKELY(
              addToPropertyMap(
                  selfHandle,
                  runtime,
                  name,
                  NamedPropertyDescriptor(
                      propertyFlags, selfHandle->numProperties_)) ==
              ExecutionStatus::EXCEPTION)) {
        return ExecutionStatus::EXCEPTION;
      }
      optChildHandle.getValue()->propertyMap_.set(
          runtime, selfHandle->propertyMap_.get(runtime), &runtime->getHeap());
    } else {
      LLVM_DEBUG(
          dbgs() << "Adding property " << runtime->formatSymbolID(name)
                 << " to Class:" << selfHandle->getDebugAllocationId()
                 << " transitions to existing Class:"
                 << optChildHandle.getValue()->getDebugAllocationId() << "\n");
    }

    // In any case, clear our own map.
    selfHandle->propertyMap_ = nullptr;

    return std::make_pair(*optChildHandle, selfHandle->numProperties_);
  }

  // Do we need to convert to dictionary?
  if (LLVM_UNLIKELY(selfHandle->numProperties_ == kDictionaryThreshold)) {
    // Do it.
    auto childHandle = convertToDictionary(selfHandle, runtime);

    if (toArrayIndex(
            runtime->getIdentifierTable().getStringView(runtime, name))) {
      childHandle->flags_.hasIndexLikeProperties = true;
    }

    // Add the property to the child.
    if (LLVM_UNLIKELY(
            addToPropertyMap(
                childHandle,
                runtime,
                name,
                NamedPropertyDescriptor(
                    propertyFlags, childHandle->numProperties_)) ==
            ExecutionStatus::EXCEPTION)) {
      return ExecutionStatus::EXCEPTION;
    }
    return std::make_pair(childHandle, childHandle->numProperties_++);
  }

  // Allocate the child.
  auto childHandle = runtime->makeHandle<HiddenClass>(
      runtime->ignoreAllocationFailure(HiddenClass::create(
          runtime,
          selfHandle->flags_,
          selfHandle,
          name,
          propertyFlags,
          selfHandle->numProperties_ + 1)));

  // Add it to the transition table.
  auto inserted = selfHandle->transitionMap_.insertNew(
      &runtime->getHeap(), Transition(name, propertyFlags), childHandle);
  (void)inserted;
  assert(
      inserted &&
      "transition already exists when adding a new property to hidden class");

  if (toArrayIndex(
          runtime->getIdentifierTable().getStringView(runtime, name))) {
    childHandle->flags_.hasIndexLikeProperties = true;
  }

  if (selfHandle->propertyMap_) {
    assert(
        !DictPropertyMap::find(selfHandle->propertyMap_.get(runtime), name) &&
        "Adding an existing property to hidden class");

    LLVM_DEBUG(
        dbgs() << "Adding property " << runtime->formatSymbolID(name)
               << " to Class:" << selfHandle->getDebugAllocationId()
               << " transitions Map to new Class:"
               << childHandle->getDebugAllocationId() << "\n");

    // Move the map to the child class.
    childHandle->propertyMap_.set(
        runtime, selfHandle->propertyMap_.get(runtime), &runtime->getHeap());
    selfHandle->propertyMap_ = nullptr;

    if (LLVM_UNLIKELY(
            addToPropertyMap(
                childHandle,
                runtime,
                name,
                NamedPropertyDescriptor(
                    propertyFlags, selfHandle->numProperties_)) ==
            ExecutionStatus::EXCEPTION)) {
      return ExecutionStatus::EXCEPTION;
    }
  } else {
    LLVM_DEBUG(
        dbgs() << "Adding property " << runtime->formatSymbolID(name)
               << " to Class:" << selfHandle->getDebugAllocationId()
               << " transitions to new Class:"
               << childHandle->getDebugAllocationId() << "\n");
  }

  return std::make_pair(childHandle, selfHandle->numProperties_);
}

Handle<HiddenClass> HiddenClass::updateProperty(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime,
    PropertyPos pos,
    PropertyFlags newFlags) {
  assert(newFlags.isValid() && "newFlags must be valid");

  // In dictionary mode we simply update our map (which must exist).
  if (LLVM_UNLIKELY(selfHandle->flags_.dictionaryMode)) {
    assert(
        selfHandle->propertyMap_ &&
        "propertyMap must exist in dictionary mode");
    DictPropertyMap::getDescriptorPair(
        selfHandle->propertyMap_.get(runtime), pos)
        ->second.flags = newFlags;
    return selfHandle;
  }

  assert(
      selfHandle->propertyMap_ && "propertyMap must exist in updateProperty()");

  auto *descPair = DictPropertyMap::getDescriptorPair(
      selfHandle->propertyMap_.get(runtime), pos);
  // If the property flags didn't change, do nothing.
  if (descPair->second.flags == newFlags)
    return selfHandle;

  auto name = descPair->first;

  // The transition flags must indicate that it is a "flags transition".
  PropertyFlags transitionFlags = newFlags;
  transitionFlags.flagsTransition = 1;

  // Do we already have a transition for that property+flags pair?
  auto optChildHandle =
      selfHandle->transitionMap_.lookup(runtime, {name, transitionFlags});
  if (LLVM_LIKELY(optChildHandle)) {
    // If the child doesn't have a property map, but we do, update our map and
    // move it to the child.
    if (!optChildHandle.getValue()->propertyMap_) {
      LLVM_DEBUG(
          dbgs() << "Updating property " << runtime->formatSymbolID(name)
                 << " in Class:" << selfHandle->getDebugAllocationId()
                 << " transitions Map to existing Class:"
                 << optChildHandle.getValue()->getDebugAllocationId() << "\n");

      descPair->second.flags = newFlags;
      optChildHandle.getValue()->propertyMap_.set(
          runtime, selfHandle->propertyMap_.get(runtime), &runtime->getHeap());
    } else {
      LLVM_DEBUG(
          dbgs() << "Updating property " << runtime->formatSymbolID(name)
                 << " in Class:" << selfHandle->getDebugAllocationId()
                 << " transitions to existing Class:"
                 << optChildHandle.getValue()->getDebugAllocationId() << "\n");
    }

    // In any case, clear our own map.
    selfHandle->propertyMap_ = nullptr;

    return *optChildHandle;
  }

  // We are updating the existing property and adding a transition to a new
  // hidden class.
  descPair->second.flags = newFlags;

  // Allocate the child.
  auto childHandle = runtime->makeHandle<HiddenClass>(
      runtime->ignoreAllocationFailure(HiddenClass::create(
          runtime,
          selfHandle->flags_,
          selfHandle,
          name,
          transitionFlags,
          selfHandle->numProperties_)));

  // The child has the same "shape" as we do, because it has the same fields.
  childHandle->family_.set(
      runtime, selfHandle->family_.get(runtime), &runtime->getHeap());

  // Add it to the transition table.
  auto inserted = selfHandle->transitionMap_.insertNew(
      &runtime->getHeap(), Transition(name, transitionFlags), childHandle);
  (void)inserted;
  assert(
      inserted &&
      "transition already exists when updating a property in hidden class");

  LLVM_DEBUG(
      dbgs() << "Updating property " << runtime->formatSymbolID(name)
             << " in Class:" << selfHandle->getDebugAllocationId()
             << " transitions Map to new Class:"
             << childHandle->getDebugAllocationId() << "\n");

  // Move the updated map to the child class.
  childHandle->propertyMap_.set(
      runtime, selfHandle->propertyMap_.get(runtime), &runtime->getHeap());
  selfHandle->propertyMap_ = nullptr;

  return childHandle;
}

Handle<HiddenClass> HiddenClass::makeAllNonConfigurable(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime) {
  if (selfHandle->flags_.allNonConfigurable)
    return selfHandle;

  if (!selfHandle->propertyMap_)
    initializeMissingPropertyMap(selfHandle, runtime);

  LLVM_DEBUG(
      dbgs() << "Class:" << selfHandle->getDebugAllocationId()
             << " making all non-configurable\n");

  // Keep a handle to our initial map. The order of properties in it will
  // remain the same as long as we are only doing property updates.
  auto mapHandle = runtime->makeHandle(selfHandle->propertyMap_);

  MutableHandle<HiddenClass> curHandle{runtime, *selfHandle};

  // TODO: this can be made much more efficient at the expense of moving some
  // logic from updateOwnProperty() here.
  DictPropertyMap::forEachProperty(
      mapHandle,
      runtime,
      [runtime, &curHandle](SymbolID id, NamedPropertyDescriptor desc) {
        if (!desc.flags.configurable)
          return;
        PropertyFlags newFlags = desc.flags;
        newFlags.configurable = 0;

        assert(
            curHandle->propertyMap_ &&
            "propertyMap must exist after updateOwnProperty()");

        auto found =
            DictPropertyMap::find(curHandle->propertyMap_.get(runtime), id);
        assert(found && "property not found during enumeration");
        curHandle = *updateProperty(curHandle, runtime, *found, newFlags);
      });

  curHandle->flags_.allNonConfigurable = true;

  return std::move(curHandle);
}

Handle<HiddenClass> HiddenClass::makeAllReadOnly(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime) {
  if (selfHandle->flags_.allReadOnly)
    return selfHandle;

  if (!selfHandle->propertyMap_)
    initializeMissingPropertyMap(selfHandle, runtime);

  LLVM_DEBUG(
      dbgs() << "Class:" << selfHandle->getDebugAllocationId()
             << " making all read-only\n");

  // Keep a handle to our initial map. The order of properties in it will
  // remain the same as long as we are only doing property updates.
  auto mapHandle = runtime->makeHandle(selfHandle->propertyMap_);

  MutableHandle<HiddenClass> curHandle{runtime, *selfHandle};

  // TODO: this can be made much more efficient at the expense of moving some
  // logic from updateOwnProperty() here.
  DictPropertyMap::forEachProperty(
      mapHandle,
      runtime,
      [runtime, &curHandle](SymbolID id, NamedPropertyDescriptor desc) {
        PropertyFlags newFlags = desc.flags;
        if (!newFlags.accessor) {
          newFlags.writable = 0;
          newFlags.configurable = 0;
        } else {
          newFlags.configurable = 0;
        }
        if (desc.flags == newFlags)
          return;

        assert(
            curHandle->propertyMap_ &&
            "propertyMap must exist after updateOwnProperty()");

        auto found =
            DictPropertyMap::find(curHandle->propertyMap_.get(runtime), id);
        assert(found && "property not found during enumeration");
        curHandle = *updateProperty(curHandle, runtime, *found, newFlags);
      });

  curHandle->flags_.allNonConfigurable = true;
  curHandle->flags_.allReadOnly = true;

  return std::move(curHandle);
}

Handle<HiddenClass> HiddenClass::updatePropertyFlagsWithoutTransitions(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime,
    PropertyFlags flagsToClear,
    PropertyFlags flagsToSet,
    OptValue<llvm::ArrayRef<SymbolID>> props) {
  // Allocate the property map.
  if (LLVM_UNLIKELY(!selfHandle->propertyMap_))
    initializeMissingPropertyMap(selfHandle, runtime);

  MutableHandle<HiddenClass> classHandle{runtime};
  if (selfHandle->isDictionary()) {
    classHandle = *selfHandle;
  } else {
    // To create an orphan hidden class with updated properties, first clone the
    // old one, and make it a root.
    classHandle = vmcast<HiddenClass>(
        runtime->ignoreAllocationFailure(HiddenClass::create(
            runtime,
            selfHandle->flags_,
            runtime->makeNullHandle<HiddenClass>(),
            SymbolID{},
            PropertyFlags{},
            selfHandle->numProperties_)));
    // Move the property map to the new hidden class.
    classHandle->propertyMap_.set(
        runtime, selfHandle->propertyMap_.get(runtime), &runtime->getHeap());
    selfHandle->propertyMap_ = nullptr;
  }

  auto mapHandle =
      runtime->makeHandle<DictPropertyMap>(classHandle->propertyMap_);

  auto changeFlags = [&flagsToClear,
                      &flagsToSet](NamedPropertyDescriptor &desc) {
    desc.flags.changeFlags(flagsToClear, flagsToSet);
  };

  // If we have the subset of properties to update, only update them; otherwise,
  // update all properties.
  if (props) {
    // Iterate over the properties that exist on the property map.
    for (auto id : *props) {
      auto pos = DictPropertyMap::find(*mapHandle, id);
      if (!pos) {
        continue;
      }
      auto descPair = DictPropertyMap::getDescriptorPair(*mapHandle, *pos);
      changeFlags(descPair->second);
    }
  } else {
    DictPropertyMap::forEachMutablePropertyDescriptor(
        mapHandle, runtime, changeFlags);
  }

  return std::move(classHandle);
}

bool HiddenClass::areAllNonConfigurable(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime) {
  if (selfHandle->flags_.allNonConfigurable)
    return true;

  if (!forEachPropertyWhile(
          selfHandle,
          runtime,
          [](Runtime *, SymbolID, NamedPropertyDescriptor desc) {
            return !desc.flags.configurable;
          })) {
    return false;
  }

  selfHandle->flags_.allNonConfigurable = true;
  return true;
}

bool HiddenClass::areAllReadOnly(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime) {
  if (selfHandle->flags_.allReadOnly)
    return true;

  if (!forEachPropertyWhile(
          selfHandle,
          runtime,
          [](Runtime *, SymbolID, NamedPropertyDescriptor desc) {
            if (!desc.flags.accessor && desc.flags.writable)
              return false;
            return !desc.flags.configurable;
          })) {
    return false;
  }

  selfHandle->flags_.allNonConfigurable = true;
  selfHandle->flags_.allReadOnly = true;
  return true;
}

ExecutionStatus HiddenClass::addToPropertyMap(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime,
    SymbolID name,
    NamedPropertyDescriptor desc) {
  assert(selfHandle->propertyMap_ && "the property map must be initialized");

  // Add the new field to the property map.
  MutableHandle<DictPropertyMap> updatedMap{
      runtime, selfHandle->propertyMap_.get(runtime)};

  if (LLVM_UNLIKELY(
          DictPropertyMap::add(updatedMap, runtime, name, desc) ==
          ExecutionStatus::EXCEPTION)) {
    return ExecutionStatus::EXCEPTION;
  }

  selfHandle->propertyMap_.set(runtime, *updatedMap, &runtime->getHeap());
  return ExecutionStatus::RETURNED;
}

void HiddenClass::initializeMissingPropertyMap(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime) {
  assert(!selfHandle->propertyMap_ && "property map is already initialized");

  // Check whether we can steal our parent's map. If we can, we only need
  // to add or update a single property.
  if (selfHandle->parent_ &&
      selfHandle->parent_.get(runtime)->propertyMap_.get(runtime))
    return stealPropertyMapFromParent(selfHandle, runtime);

  LLVM_DEBUG(
      dbgs() << "Class:" << selfHandle->getDebugAllocationId()
             << " allocating new map\n");

  // First collect all entries in reverse order. This avoids recursion.
  using MapEntry = std::pair<SymbolID, PropertyFlags>;
  llvm::SmallVector<MapEntry, 4> entries;
  entries.reserve(selfHandle->numProperties_);
  for (auto *cur = *selfHandle; cur->numProperties_ > 0;
       cur = cur->parent_.get(runtime)) {
    auto tmpFlags = cur->propertyFlags_;
    tmpFlags.flagsTransition = 0;
    entries.emplace_back(cur->symbolID_, tmpFlags);
  }

  assert(
      entries.size() <= DictPropertyMap::getMaxCapacity() &&
      "There shouldn't ever be this many properties");
  // Allocate the map with the correct size.
  auto res = DictPropertyMap::create(
      runtime,
      std::max(
          (DictPropertyMap::size_type)entries.size(),
          toRValue(DictPropertyMap::DEFAULT_CAPACITY)));
  assert(
      res != ExecutionStatus::EXCEPTION &&
      "Since the entries would fit, there shouldn't be an exception");
  MutableHandle<DictPropertyMap> mapHandle{runtime, res->get()};

  // Add the collected entries in reverse order. Note that there could be
  // duplicates.
  SlotIndex slotIndex = 0;
  for (auto it = entries.rbegin(), e = entries.rend(); it != e; ++it) {
    auto inserted = DictPropertyMap::findOrAdd(mapHandle, runtime, it->first);
    assert(
        inserted != ExecutionStatus::EXCEPTION &&
        "Space was already reserved, this couldn't have grown");

    inserted->first->flags = it->second;
    // If it is a new property, allocate the next slot.
    if (LLVM_LIKELY(inserted->second))
      inserted->first->slot = slotIndex++;
  }

  selfHandle->propertyMap_.set(runtime, *mapHandle, &runtime->getHeap());
}

void HiddenClass::stealPropertyMapFromParent(
    Handle<HiddenClass> selfHandle,
    Runtime *runtime) {
  auto *self = *selfHandle;
  assert(
      self->parent_ && self->parent_.get(runtime)->propertyMap_ &&
      !self->propertyMap_ &&
      "stealPropertyMapFromParent() must be called with a valid parent with a property map");

  LLVM_DEBUG(
      dbgs() << "Class:" << self->getDebugAllocationId()
             << " stealing map from parent Class:"
             << self->parent_.get(runtime)->getDebugAllocationId() << "\n");

  // Success! Just steal our parent's map and add our own property.
  self->propertyMap_.set(
      runtime,
      self->parent_.get(runtime)->propertyMap_.get(runtime),
      &runtime->getHeap());
  self->parent_.get(runtime)->propertyMap_ = nullptr;

  // Does our class add a new property?
  if (LLVM_LIKELY(!self->propertyFlags_.flagsTransition)) {
    // This is a new property that we must now add.
    assert(
        self->numProperties_ - 1 == self->propertyMap_.get(runtime)->size() &&
        "propertyMap->size() must match HiddenClass::numProperties-1 in "
        "new prop transition");

    // Create a descriptor for our property.
    NamedPropertyDescriptor desc{selfHandle->propertyFlags_,
                                 selfHandle->numProperties_ - 1};
    addToPropertyMap(selfHandle, runtime, selfHandle->symbolID_, desc);
  } else {
    // Our class is updating the flags of an existing property. So we need
    // to find it and update it.

    assert(
        self->numProperties_ == self->propertyMap_.get(runtime)->size() &&
        "propertyMap->size() must match HiddenClass::numProperties in "
        "flag update transition");

    auto pos =
        DictPropertyMap::find(self->propertyMap_.get(runtime), self->symbolID_);
    assert(pos && "property must exist in flag update transition");
    auto tmpFlags = self->propertyFlags_;
    tmpFlags.flagsTransition = 0;
    DictPropertyMap::getDescriptorPair(self->propertyMap_.get(runtime), *pos)
        ->second.flags = tmpFlags;
  }
}

} // namespace vm
} // namespace hermes
