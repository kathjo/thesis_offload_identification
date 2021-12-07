#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

#include "llvm/IR/Dominators.h"

#include "InductionLoopVisitor.h"

using namespace llvm;

namespace offload {

bool InductionLoopVisitor::isDone() { return false; }
bool InductionLoopVisitor::follow(const SCEV *S) {
  if (isa<SCEVAddRecExpr>(S)) {
    const SCEVAddRecExpr *SCEVAddRecExprPtr = dyn_cast<SCEVAddRecExpr>(S);
    InductionLoop.push_back(SCEVAddRecExprPtr->getLoop());
  }
  return true;
}
InductionLoopVisitor::InductionLoopVisitor(
    SmallVector<const Loop *, 4> &InductionLoop)
    : InductionLoop(InductionLoop){};
} // namespace offload