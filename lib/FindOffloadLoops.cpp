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
#include "LoopTreeGen.h"

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
