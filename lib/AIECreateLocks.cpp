//===- AIECreateLocks.cpp ---------------------------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2022 Xilinx Inc.
//
//===----------------------------------------------------------------------===//

#include "aie/AIEConversionPatterns.h"
#include "aie/AIEDialect.h"
#include "aie/AIETokenAnalysis.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "mlir/Transforms/DialectConversion.h"

#define DEBUG_TYPE "aie-create-locks"
using namespace mlir;
using namespace xilinx;
using namespace xilinx::AIE;

struct Token2LockLowering : public OpConversionPattern<UseTokenOp> {
  using OpConversionPattern<UseTokenOp>::OpConversionPattern;
  ModuleOp &module;
  DenseMap<std::pair<StringRef, int>, std::pair<LockOp, int>>
      &tokenUser2lockState;

  Token2LockLowering(MLIRContext *context, ModuleOp &m,
                     DenseMap<std::pair<StringRef, int>, std::pair<LockOp, int>>
                         &tokenUser2lockState,
                     PatternBenefit benefit = 1)
      : OpConversionPattern<UseTokenOp>(context, benefit), module(m),
        tokenUser2lockState(tokenUser2lockState) {}

  LogicalResult
  matchAndRewrite(UseTokenOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto lockState =
        tokenUser2lockState[std::make_pair(op.getTokenName(), op.getUser())];

    LockAction action;
    if (op.acquire()) {
      action = LockAction::Acquire;
      LLVM_DEBUG(llvm::dbgs() << "Replacing Acquire: " << op << "\n");
    } else {
      action = LockAction::Release;
      LLVM_DEBUG(llvm::dbgs() << "Replacing Release: " << op << "\n");
    }

    rewriter.create<UseLockOp>(op.getLoc(), lockState.first, lockState.second,
                               action);
    LLVM_DEBUG(llvm::dbgs() << "with lock " << lockState.first << " in state "
                            << lockState.second << "\n");
    rewriter.eraseOp(op.getOperation());

    return success();
  }
};

struct LockStateMapping : public OpRewritePattern<LockOp> {
  ModuleOp &module;
  DenseMap<std::pair<StringRef, int>, std::pair<LockOp, int>>
      &tokenUser2lockState;

  LockStateMapping(MLIRContext *context, ModuleOp &m,
                   DenseMap<std::pair<StringRef, int>, std::pair<LockOp, int>>
                       &tokenUser2lockState,
                   PatternBenefit benefit = 1)
      : OpRewritePattern<LockOp>(context, benefit), module(m),
        tokenUser2lockState(tokenUser2lockState) {}

  LogicalResult matchAndRewrite(LockOp op,
                                PatternRewriter &rewriter) const override {
    rewriter.startRootUpdate(op);

    int states[2] = {-1, -1};
    bool mappedToToken = false;
    
    for (auto &pair : tokenUser2lockState) {
      auto tokenUser = pair.first;
      auto lockState = pair.second;

      if (lockState.first != op)
        continue;

      // Mapping the symbol name to the corresponding token name
      op->setAttr("access_name", rewriter.getStringAttr(tokenUser.first));
      mappedToToken = true;
      states[lockState.second] = tokenUser.second;
    }

    // Mapping the physical lock state to the corresponding logical states
    if (mappedToToken) {
      op->setAttr("zero_state", rewriter.getI32IntegerAttr(states[0]));
      op->setAttr("one_state", rewriter.getI32IntegerAttr(states[1]));
    }

    rewriter.finalizeRootUpdate(op);
    return success();
  }
};

static LockOp
allocateFullLock(DenseMap<int, DenseMap<int, StringRef>> &lockUsedStates,
                 DenseMap<int, LockOp> &tileLocks, TileOp tile,
                 OpBuilder &builder) {
  for (unsigned i = 0; i < 16; i++) {
    if (lockUsedStates.count(i)) {
      LLVM_DEBUG(llvm::dbgs() << "Lock " << i << " at " << tile
                              << "was already used, skipping...\n");
      continue;
    }
    auto lock = builder.create<LockOp>(builder.getUnknownLoc(), tile, i);
    tileLocks[i] = lock;
    LLVM_DEBUG(llvm::dbgs() << "lock " << lock << " allocated for tokens.\n");
    return lock;
  }
  return nullptr;
}

static std::pair<LockOp, int>
allocateLockState(DenseMap<int, DenseMap<int, StringRef>> &lockUsedStates,
                  DenseMap<int, LockOp> &tileLocks, TileOp tile,
                  StringRef tokenName, OpBuilder &builder) {

  // Try reusing a lock first
  for (unsigned i = 0; i < 16; i++) {
    if (!lockUsedStates.count(i))
      continue;

    // Avoid using the same physical lock for different tokens
    bool usedForDifferentToken = false;
    for (unsigned s = 0; s < 2; s++) {
      if (lockUsedStates[i].count(s) && lockUsedStates[i][s] != tokenName)
        usedForDifferentToken = true;
    }
    if (usedForDifferentToken)
      continue;

    for (unsigned s = 0; s < 2; s++) {
      if (lockUsedStates[i].count(s))
        continue;
      lockUsedStates[i][s] = tokenName;
      return std::make_pair(tileLocks[i], s);
    }
  }

  // Otherwise, allocate a new lock
  auto lock = allocateFullLock(lockUsedStates, tileLocks, tile, builder);
  if (lock == nullptr)
    return std::make_pair(lock, -1);
  lockUsedStates[lock.getLockID().value()][0] = tokenName;
  return std::make_pair(lock, 0 /* state */);
}

struct AIECreateLocksPass : public AIECreateLocksBase<AIECreateLocksPass> {

  // tileLockUsedStates[tile][lockId] = {state: user tokenName}
  DenseMap<TileOp, DenseMap<int, DenseMap<int, StringRef>>> tileLockUsedStates;
  // tileLocks[tileOp] = {lockId: LockOp}
  DenseMap<TileOp, DenseMap<int, LockOp>> tileLocks;
  // lockInitialized = {LockOp: initialized or not}
  DenseMap<LockOp, bool> lockInitialized;

  // tokenUser2lockState[(tokenName, user)] = (lockOp, state)
  DenseMap<std::pair<StringRef, int>, std::pair<LockOp, int>>
      tokenUser2lockState;

  void reserveExistingLocks(TokenAnalysis &TA) {
    for (auto existingLocks : TA.getTileLocks()) {
      auto tile = existingLocks.first;
      auto locks = existingLocks.second;
      for (auto lock : locks) {
        int lockId = lock.first;
        // all states of exsiting locks shall be reserved
        tileLockUsedStates[tile][lockId][0] = "";
        tileLockUsedStates[tile][lockId][1] = "";
        tileLocks[tile][lockId] = lock.second;
      }
    }
  }

  void mapDMAAndMemcpyPairs(TokenAnalysis &TA, OpBuilder &builder) {
    // Phase 1: DMA and Memcpy pairs shall be mapped into 0/1 states of
    // a physical lock.

    for (auto pair : TA.getTokenPairs()) {
      auto acquire = pair.first;
      auto release = pair.second;
      auto acqUser = TA.getTokenUserOp(acquire);
      auto relUser = TA.getTokenUserOp(release);
      auto acqPair = TA.getTokenUseNameUser(acquire, true);
      auto relPair = TA.getTokenUseNameUser(release, false);

      auto isAcqInitialOwner =
          (acqPair.second == TA.getTokenSymbols()[acqPair.first]);
      auto isRelInitialOwner =
          (relPair.second == TA.getTokenSymbols()[relPair.first]);

      // skip pairs if they are not in DMA or are MemcpyOp
      if ((!isa<MemcpyOp>(acquire) || !isa<MemcpyOp>(release)) &&
          (!isa<MemOp>(acqUser) || !isa<MemOp>(relUser)) &&
          (!isa<ShimDMAOp>(acqUser) || !isa<ShimDMAOp>(relUser)))
        continue;

      // if one of them is mapped but the other is not, it is impossible to
      // implement this scheme since the unmapped cannot be mapped to the
      // opposite state of the physical lock of the mapped
      assert(tokenUser2lockState.count(acqPair) ==
                 tokenUser2lockState.count(relPair) &&
             "Unable to resolve: DMA/memcpy chaining not supported.");

      // if both are mapped, they need to be in the same physical lock.
      if (tokenUser2lockState.count(acqPair)) {
        assert(tokenUser2lockState[acqPair].first ==
                   tokenUser2lockState[relPair].first &&
               "Unable to resolve: DMA/memcpy mapped to different locks.");
        continue; // no need to allocate :-)
      }

      // select the tile to place the physical lock
      auto tileOp = *(TA.getAccessibleTileOp(acqUser).begin());
      // the legality of the tile selection will be check in the next phase.
      // for now, we make a best selection to be located at the user tile,
      // as the user is the only possible sharable mem selection.

      // allocate a physical lock for the users, and use 0/1 as their states
      builder.setInsertionPointAfter(tileOp);
      auto lock = allocateFullLock(tileLockUsedStates[tileOp],
                                   tileLocks[tileOp], tileOp, builder);
      assert(lock && "No more locks to allocate!");
      tileLockUsedStates[tileOp][lock.getLockID().value()][1] = acqPair.first;
      tileLockUsedStates[tileOp][lock.getLockID().value()][0] = relPair.first;
      tokenUser2lockState[acqPair] = std::make_pair(lock, 1 /* state */);
      tokenUser2lockState[relPair] = std::make_pair(lock, 0 /* state */);

      // create lock initialization
      if (isAcqInitialOwner) {
        builder.create<UseLockOp>(lock.getLoc(), lock, 1, LockAction::Release);
        lockInitialized[lock] = true;
      } else if (isRelInitialOwner) {
        builder.create<UseLockOp>(lock.getLoc(), lock, 0, LockAction::Release);
        lockInitialized[lock] = true;
      } else {
        lockInitialized[lock] = false;
      }

      LLVM_DEBUG(llvm::dbgs()
                 << "DMA token " << acqPair.first << " user " << acqPair.second
                 << " is replaced with lock " << lock << " state 1\n");
      LLVM_DEBUG(llvm::dbgs()
                 << "DMA token " << relPair.first << " user " << relPair.second
                 << " is replaced with lock " << lock << " state 0\n");
    }
  }

  void mapTokenUsers(TokenAnalysis &TA, OpBuilder &builder) {
    // Phase 2: Mapping all ownerships to physical locks.

    for (auto tokenUsers : TA.getTokenUsers()) {
      auto tokenName = tokenUsers.first;
      auto users = tokenUsers.second;
      for (auto user : users) {
        auto valPair = std::make_pair(tokenName, user);
        auto isInitialOwner = (user == TA.getTokenSymbols()[tokenName]);

        SmallVector<Operation *> users;
        // locate all users
        for (auto acquire : TA.getTokenAcqMap()[tokenName]) {
          auto acqPair = TA.getTokenUseNameUser(acquire, true);
          // skip acquires with different users
          if (acqPair.second != valPair.second)
            continue;
          users.push_back(TA.getTokenUserOp(acquire));
        }
        for (auto release : TA.getTokenRelMap()[tokenName]) {
          auto relPair = TA.getTokenUseNameUser(release, false);
          // skip releases with different users
          if (relPair.second != valPair.second)
            continue;
          users.push_back(TA.getTokenUserOp(release));
        }

        // Skipping generation if the token is not used
        if (!users.size())
          continue;

        SmallSet<TileOp, 4> possibleTiles =
            TA.getAccessibleTileOp(*users.begin());

        for (auto user : users) {
          auto currPossibleTiles = TA.getAccessibleTileOp(user);

          // find the intersection of the possible tiles
          SmallSet<TileOp, 4> intersection;
          for (auto tileOp : currPossibleTiles)
            if (possibleTiles.count(tileOp))
              intersection.insert(tileOp);
          possibleTiles = intersection;

          assert(possibleTiles.size() && "Unable to place the lock.");
          LLVM_DEBUG(llvm::dbgs()
                     << "Token " << valPair.first << " user " << valPair.second
                     << " may be placed at " << *possibleTiles.begin()
                     << ", iterating...\n");
        }

        if (tokenUser2lockState.count(valPair)) {
          // if the token is already placed, check if it is a possible tile.
          auto tileOp =
              tokenUser2lockState[valPair].first.getTile().getDefiningOp();
          assert(possibleTiles.count(dyn_cast<TileOp>(tileOp)) &&
                 "Failed to place the token to a physical tile.");

        } else {
          // otherwise, place it to a possible tile
          for (auto tileOp : possibleTiles) {
            // try all possible tiles until it is placed
            builder.setInsertionPointAfter(tileOp);
            auto lockState =
                allocateLockState(tileLockUsedStates[tileOp], tileLocks[tileOp],
                                  tileOp, valPair.first, builder);

            if (lockInitialized.count(lockState.first) == 0)
              lockInitialized[lockState.first] = false;

            // create lock initialization
            if (isInitialOwner) {
              builder.create<UseLockOp>(lockState.first.getLoc(),
                                        lockState.first, lockState.second,
                                        LockAction::Release);
              lockInitialized[lockState.first] = true;
            }

            if (lockState.first != nullptr) {
              tokenUser2lockState[valPair] = lockState;
              LLVM_DEBUG(llvm::dbgs()
                         << "Token " << valPair.first << " user "
                         << valPair.second << "is replaced with lock "
                         << lockState.first << " state " << lockState.second
                         << "\n");
              break;
            }
          }
          assert(tokenUser2lockState.count(valPair) &&
                 "No more locks to allocate!");
        }
      }
    }
  }

  void initializeLocks(OpBuilder &builder) {
    for (auto lock : lockInitialized) {
      // skipping initialized locks
      if (lock.second)
        continue;

      builder.setInsertionPointAfter(lock.first);
      // none of the created user is the initial user, acquiring the lock
      builder.create<UseLockOp>(lock.first.getLoc(), lock.first, 0,
                                LockAction::Acquire);
    }
  }

  void runOnOperation() override {
    ModuleOp m = getOperation();
    OpBuilder builder = OpBuilder::atBlockEnd(m.getBody());

    TokenAnalysis TA(m);
    TA.runAnalysis();
    LLVM_DEBUG(TA.print(llvm::dbgs()));
    reserveExistingLocks(TA);

    mapDMAAndMemcpyPairs(TA, builder);
    mapTokenUsers(TA, builder);
    initializeLocks(builder);

    ConversionTarget target(getContext());
    target.addLegalOp<UseLockOp>();
    target.addDynamicallyLegalOp<LockOp>([&](LockOp op) {
      for (auto &pair : tokenUser2lockState) {
        if (pair.second.first != op)
          continue;
        // The lock is used by a token, check if it already has the accessor
        if (!op.hasName())
          return false;
      }
      return true;
    });

    RewritePatternSet patterns(&getContext());
    // Converting UseTokenOps to UseLockOps
    patterns.insert<Token2LockLowering>(m.getContext(), m, tokenUser2lockState);
    // Removing all TokenOps
    patterns.insert<AIEOpRemoval<TokenOp>>(m.getContext(), m);
    // Mapping LockOps states with tokenUser2lockState
    patterns.insert<LockStateMapping>(m.getContext(), m, tokenUser2lockState);

    if (failed(applyPartialConversion(m, target, std::move(patterns))))
      signalPassFailure();
  }
};

std::unique_ptr<OperationPass<ModuleOp>>
xilinx::AIE::createAIECreateLocksPass() {
  return std::make_unique<AIECreateLocksPass>();
}