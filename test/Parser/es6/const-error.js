// Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the LICENSE
// file in the root directory of this source tree.
//
// RUN: (! %hermesc %s 2>&1 ) | %FileCheck --match-full-lines %s

const abc;
// CHECK: {{.*}}:8:7: error: missing initializer in const declaration
// CHECK: const abc;
// CHECK:       ^~~
