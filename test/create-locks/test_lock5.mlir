//===- test_lock5.mlir -----------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2022 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

// RUN: aie-opt --aie-create-locks %s | FileCheck %s

// CHECK-LABEL: module @test_lock5 {
// CHECK:  %0 = AIE.tile(5, 5)
// CHECK:  %1 = AIE.lock(%0, 1) {access_name = "token3", one_state = -1 : i32, zero_state = 2 : i32}
// CHECK:  AIE.useLock(%1, Acquire, 0)
// CHECK:  %2 = AIE.lock(%0, 0) {access_name = "token3", one_state = 0 : i32, zero_state = 1 : i32}
// CHECK:  AIE.useLock(%2, Release, 1)
// CHECK:  %3 = AIE.tile(4, 4)
// CHECK:  %4 = AIE.lock(%3, 1) {access_name = "token2", one_state = -1 : i32, zero_state = 2 : i32}
// CHECK:  AIE.useLock(%4, Acquire, 0)
// CHECK:  %5 = AIE.lock(%3, 0) {access_name = "token2", one_state = 0 : i32, zero_state = 1 : i32}
// CHECK:  AIE.useLock(%5, Release, 1)
// CHECK:  %6 = AIE.tile(3, 3)
// CHECK:  %7 = AIE.lock(%6, 3) {access_name = "token0", one_state = -1 : i32, zero_state = 0 : i32}
// CHECK:  AIE.useLock(%7, Release, 0)
// CHECK:  %8 = AIE.lock(%6, 2) {access_name = "token1", one_state = -1 : i32, zero_state = 0 : i32}
// CHECK:  AIE.useLock(%8, Release, 0)
// CHECK:  %9 = AIE.lock(%6, 1) {access_name = "token1", one_state = 1 : i32, zero_state = 2 : i32}
// CHECK:  AIE.useLock(%9, Acquire, 0)
// CHECK:  %10 = AIE.lock(%6, 0) {access_name = "token0", one_state = 1 : i32, zero_state = 2 : i32}
// CHECK:  AIE.useLock(%10, Acquire, 0)
// CHECK:  %14 = AIE.mem(%6) {
// CHECK:    AIE.useLock(%10, Acquire, 1)
// CHECK:    AIE.dmaBd(<%11 : memref<256xi32>, 0, 256>, 0)
// CHECK:    AIE.useLock(%10, Release, 0)
// CHECK:    AIE.useLock(%9, Acquire, 1)
// CHECK:    AIE.dmaBd(<%11 : memref<256xi32>, 0, 256>, 0)
// CHECK:    AIE.useLock(%9, Release, 0)
// CHECK:  }
// CHECK:  %15 = AIE.mem(%3) {
// CHECK:    AIE.useLock(%5, Acquire, 1)
// CHECK:    AIE.dmaBd(<%12 : memref<256xi32>, 0, 256>, 0)
// CHECK:    AIE.useLock(%5, Release, 0)
// CHECK:  }
// CHECK:  %16 = AIE.mem(%0) {
// CHECK:    AIE.useLock(%2, Acquire, 1)
// CHECK:    AIE.dmaBd(<%13 : memref<256xi32>, 0, 256>, 0)
// CHECK:    AIE.useLock(%2, Release, 0)
// CHECK:  }
// CHECK:  %17 = AIE.core(%6) {
// CHECK:    AIE.useLock(%8, Acquire, 0)
// CHECK:    AIE.useLock(%7, Acquire, 0)
// CHECK:    AIE.useLock(%10, Release, 1)
// CHECK:    AIE.useLock(%9, Release, 1)
// CHECK:  }
// CHECK:  %18 = AIE.core(%3) {
// CHECK:    AIE.useLock(%5, Acquire, 0)
// CHECK:    AIE.useLock(%4, Release, 0)
// CHECK:  }
// CHECK:  %19 = AIE.core(%0) {
// CHECK:    AIE.useLock(%2, Acquire, 0)
// CHECK:    AIE.useLock(%1, Release, 0)
// CHECK:  }
// CHECK:}

// Generate LockOp in the top-level module
// Lower UseTokenOp to UseLockOp
// [Core-Mem] ---> [Core-Mem] (non-neighboring tiles)
//     |---------> [Core-Mem]
// single producer, multipler consumers
module @test_lock5 {
  %t55 = AIE.tile(5, 5)
  %t44 = AIE.tile(4, 4)
  %t33 = AIE.tile(3, 3)

  %buf33 = AIE.buffer(%t33) : memref<256xi32>
  %buf44 = AIE.buffer(%t44) : memref<256xi32>
  %buf55 = AIE.buffer(%t55) : memref<256xi32>

  AIE.token(0) {sym_name = "token0"}
  AIE.token(0) {sym_name = "token1"}
  AIE.token(0) {sym_name = "token2"}
  AIE.token(0) {sym_name = "token3"}

  %m33 = AIE.mem(%t33) {
      %dmaSt0 = AIE.dmaStart(MM2S0, ^bd0, ^dma0)
    ^dma0:
      %dmaSt1 = AIE.dmaStart(MM2S1, ^bd1, ^end)
    ^bd0:
      AIE.useToken @token0(Acquire, 1)
      AIE.dmaBd(<%buf33 : memref<256xi32>, 0, 256>, 0)
      AIE.useToken @token0(Release, 2)
      cf.br ^end
    ^bd1:
      AIE.useToken @token1(Acquire, 1)
      AIE.dmaBd(<%buf33 : memref<256xi32>, 0, 256>, 0)
      AIE.useToken @token1(Release, 2)
      cf.br ^end
    ^end:
      AIE.end
  }

  %m44 = AIE.mem(%t44) {
      %dmaSt = AIE.dmaStart(S2MM0, ^bd0, ^end)
    ^bd0:
      AIE.useToken @token2(Acquire, 0)
      AIE.dmaBd(<%buf44 : memref<256xi32>, 0, 256>, 0)
      AIE.useToken @token2(Release, 1)
      cf.br ^end
    ^end:
      AIE.end
  }

  %m55 = AIE.mem(%t55) {
      %dmaSt = AIE.dmaStart(S2MM0, ^bd0, ^end)
    ^bd0:
      AIE.useToken @token3(Acquire, 0)
      AIE.dmaBd(<%buf55 : memref<256xi32>, 0, 256>, 0)
      AIE.useToken @token3(Release, 1)
      cf.br ^end
    ^end:
      AIE.end
  }

  %c33 = AIE.core(%t33) {
    AIE.useToken @token1(Acquire, 0)
    AIE.useToken @token0(Acquire, 0)
    AIE.useToken @token0(Release, 1)
    AIE.useToken @token1(Release, 1)
    AIE.end
  }

  %c44 = AIE.core(%t44) {
    AIE.useToken @token2(Acquire, 1)
    AIE.useToken @token2(Release, 2)
    AIE.end
  }

  %c55 = AIE.core(%t55) {
    AIE.useToken @token3(Acquire, 1)
    AIE.useToken @token3(Release, 2)
    AIE.end
  }

  AIE.flow(%t33, DMA : 0, %t44, DMA : 0)
  AIE.flow(%t33, DMA : 1, %t55, DMA : 0)
}
