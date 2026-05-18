//===- JeandleInlineAdvisor.cpp - Jeandle callback-driven inliner ---*- C++ -*-==//
//
// Copyright (c) 2026, the Jeandle-LLVM Authors. All Rights Reserved.
//
// Part of the Jeandle-LLVM project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the JeandleInlineAdvisor, a custom inline advisor
// for Jeandle JVM JIT compilation.
//
// The advisor delegates all inlining decisions to an external callback
// registered via jeandle::registerVMCallbacks(). The ShouldInline callback
// receives the caller and callee function names and returns true if inlining
// should be performed. The ResolveCallee callback is invoked on demand when
// a callee has only a declaration, allowing the JVM to provide the IR
// definition at inline time.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/JeandleInlineAdvisor.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Jeandle/VMCallback.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "jeandle-inline"

using namespace llvm;

// ============================================================================
// JeandleInlineAdvice
// ============================================================================

JeandleInlineAdvice::JeandleInlineAdvice(JeandleInlineAdvisor *Advisor,
                                         CallBase &CB,
                                         OptimizationRemarkEmitter &ORE,
                                         bool ShouldInline)
    : InlineAdvice(Advisor, CB, ORE, ShouldInline), OriginalCB(&CB) {}

void JeandleInlineAdvice::recordInliningImpl() {
  using namespace ore;
  if (IsInliningRecommended) {
    ORE.emit([&]() {
      OptimizationRemark Remark(Advisor->getAnnotatedInlinePassName(),
                                "JeandleInlined", DLoc, Block);
      Remark << "'" << NV("Callee", Callee) << "' inlined into '"
             << NV("Caller", Caller) << "' by Jeandle callback";
      return Remark;
    });
  }
}

void JeandleInlineAdvice::recordInliningWithCalleeDeletedImpl() {
  using namespace ore;
  if (IsInliningRecommended) {
    ORE.emit([&]() {
      OptimizationRemark Remark(Advisor->getAnnotatedInlinePassName(),
                                "JeandleInlined", DLoc, Block);
      Remark << "'" << NV("Callee", Callee) << "' inlined into '"
             << NV("Caller", Caller) << "' by Jeandle callback (callee deleted)";
      return Remark;
    });
  }
}

void JeandleInlineAdvice::recordUnsuccessfulInliningImpl(
    const InlineResult &Result) {
  using namespace ore;
  ORE.emit([&]() {
    return OptimizationRemarkMissed(Advisor->getAnnotatedInlinePassName(),
                                    "NotInlined", DLoc, Block)
           << "'" << NV("Callee", Callee) << "' is not inlined into '"
           << NV("Caller", Caller)
           << "': " << NV("Reason", Result.getFailureReason());
  });
}

// ============================================================================
// JeandleInlineAdvisor
// ============================================================================

JeandleInlineAdvisor::JeandleInlineAdvisor(Module &M,
                                           FunctionAnalysisManager &FAM,
                                           InlineParams Params,
                                           InlineContext IC)
    : InlineAdvisor(M, FAM, IC), Params(Params) {
  LLVM_DEBUG(dbgs() << "JeandleInlineAdvisor created (callback-driven)\n");
}

std::unique_ptr<InlineAdvice>
JeandleInlineAdvisor::getAdviceImpl(CallBase &CB) {
  Function *Callee = CB.getCalledFunction();
  if (!Callee)
    return std::make_unique<JeandleInlineAdvice>(
        this, CB,
        FAM.getResult<OptimizationRemarkEmitterAnalysis>(*CB.getCaller()),
        false);

  const jeandle::VMCallbacks *VC = jeandle::getVMCallbacks();

  bool ShouldInline = false;
  if (VC && VC->ShouldInline) {
    ShouldInline = VC->ShouldInline((uintptr_t)CB.getCaller()->getName().data(),
                                    (uintptr_t)Callee->getName().data());
  }

  LLVM_DEBUG(dbgs() << "Jeandle: " << CB.getCaller()->getName() << " -> "
                    << Callee->getName()
                    << " callback decision: " << (ShouldInline ? "inline" : "no-inline")
                    << "\n");

  if (ShouldInline && Callee->isDeclaration()) {
    if (VC && VC->ResolveCallee) {
      LLVM_DEBUG(dbgs() << "Jeandle: callee '" << Callee->getName()
                        << "' is declaration, resolving via callback\n");
      if (VC->ResolveCallee((uintptr_t)Callee->getName().data())) {
        LLVM_DEBUG(dbgs() << "Jeandle: callee '" << Callee->getName()
                          << "' resolved successfully\n");
      } else {
        LLVM_DEBUG(dbgs() << "Jeandle: callee '" << Callee->getName()
                          << "' resolved failed\n");
      }
    }
    if (Callee->isDeclaration()) {
      LLVM_DEBUG(dbgs() << "Jeandle: callee '" << Callee->getName()
                        << "' has no definition, skip\n");
      ShouldInline = false;
    }
  }

  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(*CB.getCaller());
  return std::make_unique<JeandleInlineAdvice>(this, CB, ORE, ShouldInline);
}

// ============================================================================
// Print / Factory
// ============================================================================

void JeandleInlineAdvisor::print(raw_ostream &OS) const {
  OS << "JeandleInlineAdvisor (callback-driven)\n";
}

InlineAdvisor *llvm::createJeandleInlineAdvisor(Module &M,
                                                FunctionAnalysisManager &FAM,
                                                InlineParams Params,
                                                InlineContext IC) {
  return new JeandleInlineAdvisor(M, FAM, Params, IC);
}