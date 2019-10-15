/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
//===----------------------------------------------------------------------===//
/// \file
/// ES6.0 23.2 Initialize the Set constructor.
//===----------------------------------------------------------------------===//

#include "JSLibInternal.h"

#include "hermes/VM/StringPrimitive.h"

namespace hermes {
namespace vm {

Handle<JSObject> createSetConstructor(Runtime *runtime) {
  auto setPrototype = Handle<JSSet>::vmcast(&runtime->setPrototype);

  // Set.prototype.xxx methods.
  defineMethod(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::add),
      nullptr,
      setPrototypeAdd,
      1);

  defineMethod(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::clear),
      nullptr,
      setPrototypeClear,
      0);

  defineMethod(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::deleteStr),
      nullptr,
      setPrototypeDelete,
      1);

  defineMethod(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::entries),
      nullptr,
      setPrototypeEntries,
      0);

  defineMethod(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::forEach),
      nullptr,
      setPrototypeForEach,
      1);

  defineMethod(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::has),
      nullptr,
      setPrototypeHas,
      1);

  defineAccessor(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::size),
      nullptr,
      setPrototypeSizeGetter,
      nullptr,
      false,
      true);

  defineMethod(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::values),
      nullptr,
      setPrototypeValues,
      0);

  DefinePropertyFlags dpf{};
  dpf.setEnumerable = 1;
  dpf.setWritable = 1;
  dpf.setConfigurable = 1;
  dpf.setValue = 1;
  dpf.enumerable = 0;
  dpf.writable = 1;
  dpf.configurable = 1;

  // Use the same valuesMethod for both keys() and values().
  auto propValue = runtime->makeHandle<NativeFunction>(
      runtime->ignoreAllocationFailure(JSObject::getNamed_RJS(
          setPrototype, runtime, Predefined::getSymbolID(Predefined::values))));
  runtime->ignoreAllocationFailure(JSObject::defineOwnProperty(
      setPrototype,
      runtime,
      Predefined::getSymbolID(Predefined::keys),
      dpf,
      propValue));
  runtime->ignoreAllocationFailure(JSObject::defineOwnProperty(
      setPrototype,
      runtime,
      Predefined::getSymbolID(Predefined::SymbolIterator),
      dpf,
      propValue));

  dpf = DefinePropertyFlags::getDefaultNewPropertyFlags();
  dpf.writable = 0;
  dpf.enumerable = 0;
  defineProperty(
      runtime,
      setPrototype,
      Predefined::getSymbolID(Predefined::SymbolToStringTag),
      runtime->getPredefinedStringHandle(Predefined::Set),
      dpf);

  auto cons = defineSystemConstructor<JSSet>(
      runtime,
      Predefined::getSymbolID(Predefined::Set),
      setConstructor,
      setPrototype,
      0,
      CellKind::SetKind);

  return cons;
}

CallResult<HermesValue>
setConstructor(void *, Runtime *runtime, NativeArgs args) {
  GCScope gcScope{runtime};
  if (LLVM_UNLIKELY(!args.isConstructorCall())) {
    return runtime->raiseTypeError("Constructor Set requires 'new'");
  }
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Set Constructor only applies to Set object");
  }

  JSSet::initializeStorage(selfHandle, runtime);

  if (args.getArgCount() == 0 || args.getArg(0).isUndefined() ||
      args.getArg(0).isNull()) {
    return selfHandle.getHermesValue();
  }

  auto propRes = JSObject::getNamed_RJS(
      selfHandle, runtime, Predefined::getSymbolID(Predefined::add));
  if (LLVM_UNLIKELY(propRes == ExecutionStatus::EXCEPTION)) {
    return ExecutionStatus::EXCEPTION;
  }

  // ES6.0 23.2.1.1.7: Cache adder across all iterations of the loop.
  auto adder =
      Handle<Callable>::dyn_vmcast(runtime, runtime->makeHandle(*propRes));
  if (!adder) {
    return runtime->raiseTypeError("Property 'add' for Set is not callable");
  }

  auto iterRes = getIterator(runtime, args.getArgHandle(runtime, 0));
  if (LLVM_UNLIKELY(iterRes == ExecutionStatus::EXCEPTION)) {
    return ExecutionStatus::EXCEPTION;
  }
  auto iteratorRecord = *iterRes;

  // Iterate the array and add every element.
  MutableHandle<JSObject> tmpHandle{runtime};
  auto marker = gcScope.createMarker();

  // Check the length of the array after every iteration,
  // to allow for the fact that the length could be modified during iteration.
  for (;;) {
    gcScope.flushToMarker(marker);
    CallResult<Handle<JSObject>> nextRes =
        iteratorStep(runtime, iteratorRecord);
    if (LLVM_UNLIKELY(nextRes == ExecutionStatus::EXCEPTION)) {
      return ExecutionStatus::EXCEPTION;
    }
    if (!*nextRes) {
      // Done with iteration.
      return selfHandle.getHermesValue();
    }
    tmpHandle = vmcast<JSObject>(nextRes->getHermesValue());
    auto nextValueRes = JSObject::getNamed_RJS(
        tmpHandle, runtime, Predefined::getSymbolID(Predefined::value));
    if (LLVM_UNLIKELY(nextValueRes == ExecutionStatus::EXCEPTION)) {
      return ExecutionStatus::EXCEPTION;
    }

    if (LLVM_UNLIKELY(
            Callable::executeCall1(adder, runtime, selfHandle, *nextValueRes) ==
            ExecutionStatus::EXCEPTION)) {
      return iteratorCloseAndRethrow(runtime, iteratorRecord.iterator);
    }
  }

  return selfHandle.getHermesValue();
}

CallResult<HermesValue>
setPrototypeAdd(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.add");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.add called on incompatible receiver");
  }
  auto valueHandle = args.getArgHandle(runtime, 0);
  JSSet::addValue(selfHandle, runtime, valueHandle, valueHandle);
  return selfHandle.getHermesValue();
}

CallResult<HermesValue>
setPrototypeClear(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.clear");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.clear called on incompatible receiver");
  }
  JSSet::clear(selfHandle, runtime);
  return HermesValue::encodeUndefinedValue();
}

CallResult<HermesValue>
setPrototypeDelete(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.delete");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.delete called on incompatible receiver");
  }
  return HermesValue::encodeBoolValue(
      JSSet::deleteKey(selfHandle, runtime, args.getArgHandle(runtime, 0)));
}

CallResult<HermesValue>
setPrototypeEntries(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.entries");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.entries called on incompatible receiver");
  }
  auto mapRes = JSSetIterator::create(
      runtime, Handle<JSObject>::vmcast(&runtime->setIteratorPrototype));
  if (LLVM_UNLIKELY(mapRes == ExecutionStatus::EXCEPTION)) {
    return ExecutionStatus::EXCEPTION;
  }
  auto iterator = runtime->makeHandle<JSSetIterator>(*mapRes);
  iterator->initializeIterator(runtime, selfHandle, IterationKind::Entry);
  return iterator.getHermesValue();
}

CallResult<HermesValue>
setPrototypeForEach(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.forEach");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.forEach called on incompatible receiver");
  }
  auto callbackfn = args.dyncastArg<Callable>(runtime, 0);
  if (LLVM_UNLIKELY(!callbackfn)) {
    return runtime->raiseTypeError(
        "callbackfn must be Callable inSet.prototype.forEach");
  }
  auto thisArg = args.getArgHandle(runtime, 1);
  if (LLVM_UNLIKELY(
          JSSet::forEach(selfHandle, runtime, callbackfn, thisArg) ==
          ExecutionStatus::EXCEPTION))
    return ExecutionStatus::EXCEPTION;
  return HermesValue::encodeUndefinedValue();
}

CallResult<HermesValue>
setPrototypeHas(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.has");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.has called on incompatible receiver");
  }
  return HermesValue::encodeBoolValue(
      JSSet::hasKey(selfHandle, runtime, args.getArgHandle(runtime, 0)));
}

CallResult<HermesValue>
setPrototypeSizeGetter(void *, Runtime *runtime, NativeArgs args) {
  auto self = dyn_vmcast<JSSet>(args.getThisArg());
  if (LLVM_UNLIKELY(!self)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.size");
  }
  if (LLVM_UNLIKELY(!self->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.size called on incompatible receiver");
  }
  return HermesValue::encodeNumberValue(JSSet::getSize(self, runtime));
}

CallResult<HermesValue>
setPrototypeValues(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSet>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-Set object called on Set.prototype.values");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method Set.prototype.values called on incompatible receiver");
  }
  auto mapRes = JSSetIterator::create(
      runtime, Handle<JSObject>::vmcast(&runtime->setIteratorPrototype));
  if (LLVM_UNLIKELY(mapRes == ExecutionStatus::EXCEPTION)) {
    return ExecutionStatus::EXCEPTION;
  }
  auto iterator = runtime->makeHandle<JSSetIterator>(*mapRes);
  iterator->initializeIterator(runtime, selfHandle, IterationKind::Value);
  return iterator.getHermesValue();
}

Handle<JSObject> createSetIteratorPrototype(Runtime *runtime) {
  auto parentHandle = toHandle(
      runtime,
      JSObject::create(
          runtime, Handle<JSObject>::vmcast(&runtime->iteratorPrototype)));
  defineMethod(
      runtime,
      parentHandle,
      Predefined::getSymbolID(Predefined::next),
      nullptr,
      setIteratorPrototypeNext,
      0);

  auto dpf = DefinePropertyFlags::getDefaultNewPropertyFlags();
  dpf.writable = 0;
  dpf.enumerable = 0;
  defineProperty(
      runtime,
      parentHandle,
      Predefined::getSymbolID(Predefined::SymbolToStringTag),
      runtime->getPredefinedStringHandle(Predefined::SetIterator),
      dpf);

  return parentHandle;
}

CallResult<HermesValue>
setIteratorPrototypeNext(void *, Runtime *runtime, NativeArgs args) {
  auto selfHandle = args.dyncastThis<JSSetIterator>(runtime);
  if (LLVM_UNLIKELY(!selfHandle)) {
    return runtime->raiseTypeError(
        "Non-SetIterator object called on SetIterator.prototype.next");
  }
  if (LLVM_UNLIKELY(!selfHandle->isInitialized())) {
    return runtime->raiseTypeError(
        "Method SetIterator.prototype.next called on incompatible receiver");
  }
  auto cr = JSSetIterator::nextElement(selfHandle, runtime);
  if (cr == ExecutionStatus::EXCEPTION) {
    return ExecutionStatus::EXCEPTION;
  }
  return *cr;
}
} // namespace vm
} // namespace hermes
