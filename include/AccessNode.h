#ifndef LLVM_PASS_ACCESS_NODE
#define LLVM_PASS_ACCESS_NODE

// #include "llvm/Analysis/LoopInfo.h"
// #include "llvm/Analysis/ScalarEvolutionExpressions.h"

// #include "llvm/IR/Dominators.h"

// #include "InductionLoopVisitor.h"

using namespace llvm;

namespace offload {

// magic constants determining threshold for offloading
const float LoopFactor = 100;

class AccessNode {
private:
  Loop *LoopPtr;
  Loop *FirstNonInductionLoop;
  ScalarEvolution *SE;
  DominatorTree *DT;
  bool Regular;
  bool TemporalReuse;
  llvm::LoadInst *LoadPtr;
  float TripcountFactor;
  // indirection here beeing defined as use beeing defined by another load
  SmallPtrSet<llvm::Instruction *, 16> Visited;
  bool hasIndirection(Instruction *Inst);

public:
  // this constructor does a lot of the analysis to create the information
  // contained in AccessNode: Regularity of Access Insertion Point for
  // LoopNodeTree Maybe SCEV Expression for multipliers - for now just constant
  // multiplier based on nesting level
  AccessNode(Loop *LoopPtr, LoadInst *LoadPtr, ScalarEvolution *SE,
             DominatorTree *DT);
  bool isRegular();
  LoadInst *getLoadPtr();
  Loop *getFirstNonInduction();
  float getTripcountFactor();
  // check if this node and other node's AccessInst have same PointerOperand
  bool isReducible(std::shared_ptr<AccessNode> OtherNode);
};
} // namespace offload
#endif