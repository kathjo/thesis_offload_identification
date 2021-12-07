#ifndef LLVM_PASS_INDUCTION_LOOP_VISITOR
#define LLVM_PASS_INDUCTION_LOOP_VISITOR

using namespace llvm;

namespace offload {

// Visitor implementation that pushes all AddRecExpr it finds into a vector
class InductionLoopVisitor {
private:
  SmallVector<const Loop *, 4> &InductionLoop;

public:
  InductionLoopVisitor(SmallVector<const Loop *, 4> &InductionLoop);
  bool isDone();
  bool follow(const SCEV *S);
};
} // namespace offload
#endif