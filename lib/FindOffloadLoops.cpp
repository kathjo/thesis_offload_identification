//=============================================================================
// FILE:
//    FindOffloadLoops.cpp
//
// DESCRIPTION:
//    Visits all functions in a module, computes regularity of data-access in
//    all loops to create a regularity score and based on this heuristic
//    determines code regions for offloading.
//
// USAGE (new passmanager only):
//
//      opt -load-pass-plugin ./libFindOffloadLoops.so
//      -passes=find-offload-loops `\`
//        -disable-output <input-llvm-file>
//
//
// License: MIT
//=============================================================================
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/LoopCacheAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

#include <string>

#include "AccessNode.h"
#include "FindRanges.h"
#include "LoopNode.h"

using namespace llvm;

//-----------------------------------------------------------------------------
// FindOffloadLoops implementation
//-----------------------------------------------------------------------------
namespace offload {

// magic constant determining threshold for offloading
const float OffloadThreshold = 0.7;

using SPtrLN = std::shared_ptr<LoopNode>;

void loopNodeDFSPrinter(SPtrLN ThisNode, unsigned Level) {
  errs() << "\n";
  for (unsigned i = 0; i < Level; ++i)
    errs() << "\t";
  errs() << ThisNode->Name << " Ratio: " << ThisNode->OffloadRatio
         << " No.ofAttached: " << ThisNode->AttachedAccesses.size();
  ++Level;
  for (auto Child : ThisNode->InnerLoops)
    loopNodeDFSPrinter(Child, Level);

  errs() << "\n";
}

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
  void reduceLoadPtr(std::list<std::shared_ptr<AccessNode>> *NodeWorkList) {
    bool Reduce = true;
    std::vector<std::shared_ptr<AccessNode>> ToRemove;
    while (Reduce) {
      Reduce = false;
      for (auto Iter1 = NodeWorkList->begin(); Iter1 != NodeWorkList->end();
           ++Iter1) {
        for (auto Iter2 = Iter1; Iter2 != NodeWorkList->end(); ++Iter2) {
          if (Iter1 == Iter2)
            continue;
          auto A = *Iter1;
          auto B = *Iter2;
          if (A->isReducible(B)) {
            if ((A->isRegular() != B->isRegular()) && A->isRegular()) {
              ToRemove.push_back(B);
            } else {
              ToRemove.push_back(A);
            }
            Reduce = true;
            break;
          }
        }
        if (Reduce == true)
          break;
      }
      for (auto R : ToRemove)
        NodeWorkList->remove(R);
      ToRemove.clear();
    }
  }

  // stash pointers to Load Instructions into a set
  void findLoads(SmallPtrSet<llvm::LoadInst *, 5> &LoadPtrSet) {
    ArrayRef<llvm::BasicBlock *> BlockArray = TLL->getBlocks();
    for (auto BBPtr = BlockArray.begin(); BBPtr != BlockArray.end(); ++BBPtr) {
      llvm::BasicBlock &BB = **BBPtr;
      for (auto &Ins : BB) {
        if (isa<llvm::LoadInst>(Ins)) {
          LoadInst *LoadIns = dyn_cast<LoadInst>(&Ins);
          LoadPtrSet.insert(LoadIns);
        }
      }
    }
  }

  void populateLoopTree() {
    std::vector<SPtrLN> NewLoopNodes;
    // create list of loop nodes in preorder, same as loop-info tree structure
    // loop nodes form a link tree
    for (auto L : Loops) {
      errs() << "\n ----------------------------------------------- \n";
      auto BB = L->getHeader();
      L->print(errs());
      errs() << " ----------------------------------------------- \n";
      errs() << BB->getName();
      errs() << "\n";
      const std::string Name = (std::string)BB->getName();
      SPtrLN NewNode = SPtrLN(new LoopNode{
          Name,
          LoopTreeVector.front(), // ParentLoop, default is Function RootNode;
          L                       // LoopPtr
      });
      for (auto Iter = NewLoopNodes.begin(); Iter != NewLoopNodes.end();
           Iter++) {
        SPtrLN PreviousNode = *Iter;
        Loop *Temp = PreviousNode->LoopPtr;
        if (Temp == L->getParentLoop()) {
          NewNode->ParentLoop = PreviousNode;
          break;
        }
      }
      NewLoopNodes.push_back(NewNode);
    }
    // go over list of LoopTreeNodes and insert them into their Parents'
    // InnerLoop lists
    for (auto RevIter = NewLoopNodes.rbegin(); RevIter != NewLoopNodes.rend();
         RevIter++) {
      auto Node = *RevIter;
      LoopTreeVector.push_back(Node);
      auto ParentNode = Node->ParentLoop;
      if (ParentNode)
        ParentNode->InnerLoops.push_front(Node);
    }
    NewLoopNodes.clear();
  }
  // iteratively create a set of loads that are only in one loop,
  // then reduce them to all that have unique adresses
  // then add them to the loop tree at the right position
  void insertLoadsIntoLoopTree(SmallPtrSet<llvm::LoadInst *, 5> InitialSet) {
    SmallPtrSet<llvm::LoadInst *, 5> WorkingSet;
    std::list<std::shared_ptr<AccessNode>> NodeWorkList;
    for (auto rvIter = Loops.rbegin(); rvIter != Loops.rend(); ++rvIter) {
      auto L = (*rvIter);
      // go over all loads in TLL and add the ones contained in L to working
      // set, then remove them from inital set
      for (auto LoadPtr : InitialSet) {
        if (L->contains(LoadPtr))
          WorkingSet.insert(LoadPtr);
      }
      for (auto LoadPtr : WorkingSet) {
        InitialSet.erase(LoadPtr);
        std::shared_ptr<AccessNode> NewNodePtr;
        // AccessNodes are created on the heap and handled with shared pointers
        NewNodePtr =
            std::shared_ptr<AccessNode>(new AccessNode(L, LoadPtr, SE, &DT));
        NodeWorkList.push_back(NewNodePtr);
      }
      WorkingSet.clear();
      // remove AccessNodes which have the same Address as other Nodes
      reduceLoadPtr(&NodeWorkList);
      // insert into LoopNodeTree
      // TODO assert that insertion has happened for every AccessNode
      for (auto SharedNodePtr : NodeWorkList) {
        llvm::Loop *FirstNonInd = SharedNodePtr->getFirstNonInduction();
        for (auto LoopNode : LoopTreeVector) {
          if (FirstNonInd == LoopNode->LoopPtr)
            // transfer AccessNode SharedPtr to LoopNode AttachedAcces List
            LoopNode->AttachedAccesses.push_back(SharedNodePtr);
        }
      }
      // Nodes live on in LoopNode Tree
      NodeWorkList.clear();
    }
  }
  void calcRegularity() {
    for (auto RevIter = LoopTreeVector.rbegin();
         RevIter != LoopTreeVector.rend(); RevIter++) {
      auto Node = *RevIter;
      if (Node->InnerLoops.empty() && Node->AttachedAccesses.empty()) {
        continue;
      }
      for (auto Inner : Node->InnerLoops) {
        Node->Regular = Node->Regular + Inner->Regular;
        Node->Irregular = Node->Irregular + Inner->Irregular;
      }
      for (auto Access : Node->AttachedAccesses) {
        if (Access->isRegular()) {
          Node->Regular = Node->Regular + Access->getTripcountFactor();
        } else {
          Node->Irregular = Node->Irregular + Access->getTripcountFactor();
        }
      }
      Node->OffloadRatio = Node->Irregular / (Node->Irregular + Node->Regular);
      if (std::isnan(Node->OffloadRatio))
        Node->OffloadRatio = 0.0;
    }
  }

public:
  // constructor
  LoopTreeGen(llvm::LoopInfo *LI, llvm::ScalarEvolution *SE,
              llvm::DominatorTree &DT, std::vector<SPtrLN> &LoopTreeVector)
      : LI(LI), SE(SE), DT(DT), LoopTreeVector(LoopTreeVector) {}

  void addLoops(Loop *nextTLL) {
    TLL = nextTLL;
    // vector of loops nested inside this top-level-loop
    Loops = TLL->getLoopsInPreorder();
    // stash pointers to Load Instructions into a set
    SmallPtrSet<llvm::LoadInst *, 5> LoadPtrSet;
    // construct loop tree with loads
    populateLoopTree();
    findLoads(LoadPtrSet);
    insertLoadsIntoLoopTree(LoadPtrSet);
    // calculate regularity of loads and based on that loops
    calcRegularity();
    // avoid side-effects
    TLL = nullptr;
  }
};

bool runOnFunction(Function &F, FunctionAnalysisManager &AM) {

  errs() << "Function: " << F.getName() << "\n";
  errs() << "\n";

  // collect information on loops and BBs in function
  llvm::LoopInfo *LI = &AM.getResult<LoopAnalysis>(F);
  llvm::ScalarEvolution *SE = &AM.getResult<ScalarEvolutionAnalysis>(F);
  llvm::DominatorTree DT(F);

  // vector stores all LoopNodes in the LoopTree
  std::vector<SPtrLN> LoopTreeVector;

  // this root node does not actually represent a loop,
  // it represents the function containing the TLLs
  SPtrLN RootNode = SPtrLN(new LoopNode{
      "Root",
      nullptr, // ParentLoop
      nullptr  // LoopPtr
  });
  LoopTreeVector.push_back(RootNode);

  LoopTreeGen treeGenerator(LI, SE, DT, LoopTreeVector);

  const std::vector<llvm::Loop *> TLLVector = LI->getTopLevelLoops();
  errs() << "Number of Top Level Loops " << TLLVector.size() << "\n\n";
  for (auto TLLPtr : TLLVector) {
    treeGenerator.addLoops(TLLPtr);
  }
  // print loops
  loopNodeDFSPrinter(RootNode, 0);

  // Find ranges for offloading:
  // RangeFinder iterates over the loop-node tree
  // in order to find ranges of loops that can be offloaded
  // a range is described by two Loop-pointers,
  // which are added to OffloadRanges as implicit pairs
  // The pointers describe the first and last loop in a given nesting level to
  // be offloaded, the two pointers can point to the same loop, they have to
  // point to loops that are on the same nesting level (meaning they are both
  // nested in the same outer loop, or are both TLLs) one nesting level can have
  // multiple ranges
  std::list<SPtrLN> WorkList;
  std::list<Loop *> OffloadRanges;
  WorkList.push_back(RootNode);
  while (!WorkList.empty()) {
    SPtrLN TraversePtr = WorkList.front();
    WorkList.pop_front();
    FindRanges RangeFinder =
        FindRanges(TraversePtr->InnerLoops, OffloadThreshold);
    RangeFinder.getRangesAsList(&WorkList, &OffloadRanges);
  }

  assert(((OffloadRanges.size() % 2) == 0) &&
         "Offload Ranges not made up out of pairs");
  errs() << "\n----------------------------------------------------------------"
            "--------------- \n\n";
  errs() << "There are " << (OffloadRanges.size() / 2) << " offload ranges in "
         << F.getName() << "\n\n";

  for (auto Iter = OffloadRanges.begin(); Iter != OffloadRanges.end(); ++Iter) {
    Loop *Begin = *Iter;
    ++Iter;
    if (Iter == OffloadRanges.end())
      break;
    Loop *End = *Iter;

    auto StartLoc = Begin->getStartLoc();
    auto EndRange = End->getLocRange();
    auto EndLoc = EndRange.getEnd();

    // print
    errs() << "\nOffload Range: \n";
    errs() << "First: \n\t";
    Begin->print(errs());
    errs() << "\n";
    StartLoc.print(errs());
    errs() << "\nLast: \n\t";
    End->print(errs());
    errs() << "\n";
    EndLoc.print(errs());
  }

  errs() << "\n\n";

  return false;
}

// New PM implementation
struct FindOffloadLoops : PassInfoMixin<FindOffloadLoops> {
  // Main entry point, takes IR unit to run the pass on (&Func) and the
  // corresponding pass manager (to be queried if need be)
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    bool Changed = runOnFunction(F, AM);
    if (Changed) {
      return PreservedAnalyses::none();
    } else {
      return PreservedAnalyses::all();
    }
  }
};

} // namespace offload

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getFindOffloadLoopsPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "FindOffloadLoops", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "find-offload-loops") {
                    FPM.addPass(offload::FindOffloadLoops());
                    return true;
                  }
                  return false;
                });
          }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize FindOffloadLoops when added to the pass pipeline on the
// command line, i.e. via '-passes=find-offload-loops'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getFindOffloadLoopsPluginInfo();
}
