/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#ifndef HERMES_VM_JSGENERATOR_H
#define HERMES_VM_JSGENERATOR_H

#include "hermes/VM/Callable.h"
#include "hermes/VM/JSObject.h"

namespace hermes {
namespace vm {

/// ES6.0 25.3 Generator Objects.
/// Stores the GeneratorInnerFunction associated with the generator.
/// The GeneratorInnerFunction is stored separately from the JSGenerator
/// due to the fact that it needs to store the same information as a standard
/// JSFunction, but must not be directly accessible by user code.
/// If the GeneratorInnerFunction was merged into JSGenerator, it would result
/// in large amounts of code duplication in terms of calling convention and
/// field storage.
class JSGenerator final : public JSObject {
  using Super = JSObject;
  friend void GeneratorBuildMeta(const GCCell *cell, Metadata::Builder &mb);

  const static ObjectVTable vt;

 public:
  /// Number of property slots the class reserves for itself. Child classes
  /// should override this value by adding to it and defining a constant with
  /// the same name.
  static const PropStorage::size_type NEEDED_PROPERTY_SLOTS =
      Super::NEEDED_PROPERTY_SLOTS;

  static bool classof(const GCCell *cell) {
    return cell->getKind() == CellKind::GeneratorKind;
  }

  static CallResult<PseudoHandle<JSGenerator>> create(
      Runtime *runtime,
      Handle<GeneratorInnerFunction> innerFunction,
      Handle<JSObject> parentHandle);

  /// \return the inner function.
  static PseudoHandle<GeneratorInnerFunction> getInnerFunction(
      Runtime *runtime,
      JSGenerator *self) {
    return createPseudoHandle(self->innerFunction_.get(runtime));
  }

 protected:
#ifdef HERMESVM_SERIALIZE
  explicit JSGenerator(Deserializer &d);

  friend void GeneratorSerialize(Serializer &s, const GCCell *cell);
  friend void GeneratorDeserialize(Deserializer &d, CellKind kind);
#endif

  JSGenerator(Runtime *runtime, JSObject *parent, HiddenClass *clazz)
      : JSObject(runtime, &vt.base, parent, clazz) {}

 private:
  /// The GeneratorInnerFunction that is called when this generator is advanced.
  GCPointer<GeneratorInnerFunction> innerFunction_;
};

} // namespace vm
} // namespace hermes

#endif
