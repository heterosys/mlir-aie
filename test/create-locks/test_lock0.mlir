//===- test_lock0.mlir -----------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2022 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

// RUN: aie-opt --aie-create-locks %s | FileCheck %s

// CHECK-LABEL: module @test_lock0 {
// CHECK:  %0 = AIE.tile(3, 3)
// CHECK:  %1 = AIE.lock(%0, 0) {access_name = "token0", one_state = -1 : i32, zero_state = 0 : i32}
// CHECK:  AIE.useLock(%1, Release, 0)
// CHECK:  %2 = AIE.tile(2, 3)
// CHECK:  %3 = AIE.lock(%2, 0) {access_name = "token0", one_state = 2 : i32, zero_state = 1 : i32}
// CHECK:  AIE.useLock(%3, Acquire, 0)
// CHECK:  %6 = AIE.core(%0) {
// CHECK:    AIE.useLock(%1, Acquire, 0)
// CHECK:    AIE.useLock(%3, Release, 0)
// CHECK:  }
// CHECK:  %7 = AIE.core(%2) {
// CHECK:    AIE.useLock(%3, Acquire, 0)
// CHECK:    AIE.useLock(%3, Release, 1)
// CHECK:  }

// Generate LockOp in the top-level module
// Lower UseTokenOp to UseLockOp
// Tile-Tile
module @test_lock0 {
  %t33 = AIE.tile(3, 3)
  %t23 = AIE.tile(2, 3)

  AIE.token(0) {sym_name = "token0"}

  %m33 = AIE.mem(%t33) {
      AIE.end
  }

  %m23 = AIE.mem(%t23) {
      AIE.end
  }

  %c33 = AIE.core(%t33) {
    AIE.useToken @token0(Acquire, 0)
    AIE.useToken @token0(Release, 1)
    AIE.end
  }

  %c23 = AIE.core(%t23) {
    AIE.useToken @token0(Acquire, 1)
    AIE.useToken @token0(Release, 2)
    AIE.end
  }
}
