// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the LICENSE
// file in the root directory of this source tree.
//
// RUN: %hermesc -dump-ast -pretty-json %s | %FileCheck %s --match-full-lines

// CHECK: {
// CHECK-NEXT:   "type": "Program",
// CHECK-NEXT:   "body": [

function foo() {
  return new.target;
}
// CHECK-NEXT:     {
// CHECK-NEXT:       "type": "FunctionDeclaration",
// CHECK-NEXT:       "id": {
// CHECK-NEXT:         "type": "Identifier",
// CHECK-NEXT:         "name": "foo",
// CHECK-NEXT:         "typeAnnotation": null
// CHECK-NEXT:       },
// CHECK-NEXT:       "params": [],
// CHECK-NEXT:       "body": {
// CHECK-NEXT:         "type": "BlockStatement",
// CHECK-NEXT:         "body": [
// CHECK-NEXT:           {
// CHECK-NEXT:             "type": "ReturnStatement",
// CHECK-NEXT:             "argument": {
// CHECK-NEXT:               "type": "MetaProperty",
// CHECK-NEXT:               "meta": {
// CHECK-NEXT:                 "type": "Identifier",
// CHECK-NEXT:                 "name": "new",
// CHECK-NEXT:                 "typeAnnotation": null
// CHECK-NEXT:               },
// CHECK-NEXT:               "property": {
// CHECK-NEXT:                 "type": "Identifier",
// CHECK-NEXT:                 "name": "target",
// CHECK-NEXT:                 "typeAnnotation": null
// CHECK-NEXT:               }
// CHECK-NEXT:             }
// CHECK-NEXT:           }
// CHECK-NEXT:         ]
// CHECK-NEXT:       },
// CHECK-NEXT:       "returnType": null,
// CHECK-NEXT:       "generator": false
// CHECK-NEXT:     }

// CHECK-NEXT:   ]
// CHECK-NEXT: }
