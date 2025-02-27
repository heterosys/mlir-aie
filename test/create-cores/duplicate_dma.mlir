//===- duplicate_dma.mlir --------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2021 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

// RUN: aie-opt --aie-create-cores --aie-lower-memcpy %s 2>&1 | FileCheck %s
// CHECK: error: 'AIE.dmaStart' op Duplicate DMA channel MM2S0 detected in MemOp!

module @duplicate_dma  {
  %0 = AIE.tile(1, 1)
  %1 = AIE.buffer(%0) : memref<256xi32>
  %2 = AIE.mem(%0)  {
    %15 = AIE.dmaStart(MM2S0, ^bb1, ^bb4)
  ^bb1:  // pred: ^bb0
    AIE.useToken @token0(Acquire, 1)
    AIE.dmaBd(<%1 : memref<256xi32>, 0, 256>, 0)
    AIE.useToken @token0(Release, 2)
    cf.br ^bb2
  ^bb2:  
    %16 = AIE.dmaStart(MM2S0, ^bb3, ^bb4)
  ^bb3:  
    AIE.useToken @token1(Acquire, 1)
    AIE.dmaBd(<%1 : memref<256xi32>, 0, 256>, 0)
    AIE.useToken @token1(Release, 2)
    cf.br ^bb4
  ^bb4:  // 4 preds: ^bb0, ^bb1, ^bb2, ^bb3
    AIE.end
  }
}