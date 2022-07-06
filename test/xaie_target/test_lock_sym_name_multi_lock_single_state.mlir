//===- test_lock_sym_name.mlir ---------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2022 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

// RUN: aie-translate --aie-generate-xaie %s | FileCheck %s
// CHECK: inline bool mlir_aie_release_my_lock(aie_libxaie_ctx_t* ctx, int state, int timeout) {
// CHECK:   if (state == 0) {
// CHECK:     if (!XAieTile_LockRelease(&(ctx->TileInst[3][3]), 0, 0, timeout))
// CHECK:       return false;
// CHECK:     return true;
// CHECK:   } else if (state == 2) {
// CHECK:     if (!XAieTile_LockRelease(&(ctx->TileInst[3][3]), 1, 1, timeout))
// CHECK:       return false;
// CHECK:     return true;
// CHECK:   } else if (state == 1) {
// CHECK:     if (!XAieTile_LockRelease(&(ctx->TileInst[3][3]), 0, 1, timeout))
// CHECK:       return false;
// CHECK:     if (!XAieTile_LockRelease(&(ctx->TileInst[3][3]), 1, 0, timeout))
// CHECK:       return false;
// CHECK:     return true;
// CHECK:   } else return false;
// CHECK: }
// CHECK: inline bool mlir_aie_acquire_my_lock(aie_libxaie_ctx_t* ctx, int state, int timeout) {
// CHECK:   if (state == 0) {
// CHECK:     if (!XAieTile_LockAcquire(&(ctx->TileInst[3][3]), 0, 0, timeout))
// CHECK:       return false;
// CHECK:     return true;
// CHECK:   } else if (state == 2) {
// CHECK:     if (!XAieTile_LockAcquire(&(ctx->TileInst[3][3]), 1, 1, timeout))
// CHECK:       return false;
// CHECK:     return true;
// CHECK:   } else if (state == 1) {
// CHECK:     if (!XAieTile_LockAcquire(&(ctx->TileInst[3][3]), 0, 1, timeout))
// CHECK:       return false;
// CHECK:     if (!XAieTile_LockAcquire(&(ctx->TileInst[3][3]), 1, 0, timeout))
// CHECK:       return false;
// CHECK:     return true;
// CHECK:   } else return false;
// CHECK: }

module @test_lock_sym_name {
  %t33 = AIE.tile(3, 3)
  %l33_0 = AIE.lock(%t33, 0) { access_name = "my_lock" }
  %l33_1 = AIE.lock(%t33, 1) { access_name = "my_lock", zero_state = 1, one_state = 2 }
  // Releasing my_lock@0 will release %l33_0@0 and acquire %l33_1,
  //   so that my_lock cannot be in other states at the same time.
  // Releasing my_lock@1 will release %l33_0@1 and %l33_1@0.
  // Releasing my_lock@2 will release %l33_1@1 and acquire %l33_0.
}
