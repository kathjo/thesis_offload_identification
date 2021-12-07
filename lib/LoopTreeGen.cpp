#include "LoopTreeGen.h"

using namespace llvm;

namespace offload {

// compare all access nodes in the workload with each other
// and remove one node if it is reducible another
void LoopTreeGen::reduceLoadPtr(
    std::list<std::shared_ptr<AccessNode>> *NodeWorkList) {
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
void LoopTreeGen::findLoads(SmallPtrSet<llvm::LoadInst *, 5> &LoadPtrSet) {
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

void LoopTreeGen::populateLoopTree() {
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
    for (auto Iter = NewLoopNodes.begin(); Iter != NewLoopNodes.end(); Iter++) {
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
void LoopTreeGen::insertLoadsIntoLoopTree(
    SmallPtrSet<llvm::LoadInst *, 5> InitialSet) {
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
void LoopTreeGen::calcRegularity() {
  for (auto RevIter = LoopTreeVector.rbegin(); RevIter != LoopTreeVector.rend();
       RevIter++) {
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

// constructor
LoopTreeGen::LoopTreeGen(llvm::LoopInfo *LI, llvm::ScalarEvolution *SE,
                         llvm::DominatorTree &DT,
                         std::vector<SPtrLN> &LoopTreeVector)
    : LI(LI), SE(SE), DT(DT), LoopTreeVector(LoopTreeVector) {}

void LoopTreeGen::addLoops(Loop *nextTLL) {
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
} // namespace offload