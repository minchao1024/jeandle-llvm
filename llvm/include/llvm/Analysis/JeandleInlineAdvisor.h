//===- JeandleInlineAdvisor.h - Jeandle callback-driven inliner ---*- C++ -*-==//
//
// Copyright (c) 2026, the Jeandle-LLVM Authors. All Rights Reserved.
//
// Part of the Jeandle-LLVM project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the JeandleInlineAdvisor, a custom inline advisor for
// Jeandle JVM JIT that delegates inlining decisions to an external callback.
//
// The callback is set via jeandle::setShouldInlineCallback() and receives
// the caller and callee names, returning true if inlining should be performed.
//
// Previously, this advisor implemented several heuristics:
//   - Adaptive Decision Thresholds (hotness-based threshold adjustment)
//   - Callsite Clustering (consistent decisions for similar callsites)
//   - Deep Inlining Trials (more aggressive inlining for hot call chains)
//   - Benefit Analysis (devirtualization, escape analysis, lock elision)
// These have been removed in favor of the callback-based approach.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_JEANDLEINLINEADVISOR_H
#define LLVM_ANALYSIS_JEANDLEINLINEADVISOR_H

#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Jeandle/GCStrategy.h"

namespace llvm {

class JeandleInlineAdvisor;

class JeandleInlineAdvice : public InlineAdvice {
public:
  JeandleInlineAdvice(JeandleInlineAdvisor *Advisor, CallBase &CB,
                      OptimizationRemarkEmitter &ORE, bool ShouldInline);

private:
  void recordInliningImpl() override;
  void recordInliningWithCalleeDeletedImpl() override;
  void recordUnsuccessfulInliningImpl(const InlineResult &Result) override;

  CallBase *const OriginalCB;
};

class LLVM_ABI JeandleInlineAdvisor : public InlineAdvisor {
public:
  JeandleInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                       InlineParams Params, InlineContext IC);

  bool shouldTryInlineDeclaration(const Function &F) const override {
    return F.isDeclaration() && F.hasGC() && F.getGC() == jeandle::JeandleGC;
  }

private:
  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

  void print(raw_ostream &OS) const override;

  InlineParams Params;
};

InlineAdvisor *createJeandleInlineAdvisor(Module &M,
                                          FunctionAnalysisManager &FAM,
                                          InlineParams Params,
                                          InlineContext IC);

} // namespace llvm

#endif // LLVM_ANALYSIS_JEANDLEINLINEADVISOR_H