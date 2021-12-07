#ifndef LLVM_PASS_LOOP_TREE_GEN
#define LLVM_PASS_LOOP_TREE_GEN

#include "llvm/Analysis/LoopCacheAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

#include "llvm/IR/Dominators.h"

#include "AccessNode.h"
#include "LoopNode.h"

using namespace llvm;

namespace offload {

using SPtrLN = std::shared_ptr<LoopNode>;

class LoopTreeGen {
private:
  llvm::LoopInfo *LI;
  llvm::ScalarEvolution *SE;
  llvm::DominatorTree &DT;
  std::vector<SPtrLN> &LoopTreeVector;

  Loop *TLL = nullptr;
  llvm::SmallVector<llvm::Loop *, 4> Loops;

  // compare all access nodes in the workload with each other
  // and remove one node if it is reducible another
  void reduceLoadPtr(std::list<std::shared_ptr<AccessNode>> *NodeWorkList);

  // stash pointers to Load Instructions into a set
  void findLoads(SmallPtrSet<llvm::LoadInst *, 5> &LoadPtrSet);

  void populateLoopTree();
  // iteratively create a set of loads that are only in one loop,
  // then reduce them to all that have unique adresses
  // then add them to the loop tree at the right position
  void insertLoadsIntoLoopTree(SmallPtrSet<llvm::LoadInst *, 5> InitialSet);
  void calcRegularity();

public:
  // constructor
  LoopTreeGen(llvm::LoopInfo *LI, llvm::ScalarEvolution *SE,
              llvm::DominatorTree &DT, std::vector<SPtrLN> &LoopTreeVector);

  void addLoops(Loop *nextTLL);
};
} // namespace offload
#endif