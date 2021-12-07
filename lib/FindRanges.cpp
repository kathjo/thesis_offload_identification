#include <list>
#include <vector>

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"

#include "FindRanges.h"

using namespace llvm;

namespace offload {

bool FindRanges::compareByDelta(const Tuple &A, const Tuple &B) {
  return A.delta < B.delta;
}
bool FindRanges::compareByBegin(const Tuple &A, const Tuple &B) {
  return A.begin < B.begin;
}

// sums up the OffloadRatios of all LoopNodes pointed to in the interval
// [Iter1, Iter2] Iter1 and Iter2 can point to the same element
float FindRanges::sumUp(std::list<SPtrLN>::const_iterator Iter1,
                        std::list<SPtrLN>::const_iterator Iter2) {
  float Sum = (*Iter1)->OffloadRatio;
  while (Iter1 != Iter2) {
    Iter1++;
    Sum += (*Iter1)->OffloadRatio;
  }
  return Sum;
}

void FindRanges::reduceTuples(std::vector<Tuple> &TupleList) {
  auto Iter1 = TupleList.begin();
  while (Iter1 != TupleList.end()) {
    auto T1 = *Iter1;
    bool deleteTuple = false;
    auto Iter2 = Iter1;
    ++Iter2;
    for (; Iter2 != TupleList.end(); ++Iter2) {
      auto T = *Iter2;
      if ((T1.begin >= T.begin) && (T1.end <= T.end)) {
        deleteTuple = true;
        break;
      }
    }
    if (deleteTuple) {
      Iter1 = TupleList.erase(Iter1);
    } else {
      ++Iter1;
    }
  }
}

FindRanges::FindRanges(const std::list<SPtrLN> &InnerLoopNodes, float Threshold)
    : InnerLoopNodes(InnerLoopNodes), Threshold(Threshold) {
  for (auto Iter1 = InnerLoopNodes.begin(); Iter1 != InnerLoopNodes.end();
       ++Iter1) {
    for (auto Iter2 = Iter1; Iter2 != InnerLoopNodes.end(); ++Iter2) {
      // Only consider ranges that have non-offloading blocks in the middle,
      // not at the beginning or end.
      auto LN1 = **Iter1;
      auto LN2 = **Iter2;
      if ((LN1.OffloadRatio > Threshold) && (LN2.OffloadRatio > Threshold)) {
        float Sum = sumUp(Iter1, Iter2);
        float Distance = (std::distance(Iter1, Iter2) + 1);
        float Average = Sum / Distance;
        if (Average > Threshold) {
          int begin = std::distance(InnerLoopNodes.begin(), Iter1);
          int end = std::distance(InnerLoopNodes.begin(), Iter2);
          assert((begin <= end) && "inverted range!");
          int delta = end - begin;
          Tuple T{begin, end, delta, Average};
          TupleList.push_back(T);
        }
      }
    }
  }
  std::sort(TupleList.begin(), TupleList.end(), compareByDelta);
  reduceTuples(TupleList);
  std::sort(TupleList.begin(), TupleList.end(), compareByBegin);
  // true smaller than because comparing index to size
  if (!TupleList.empty()) {
    assert(((unsigned long)(TupleList.back()).end < InnerLoopNodes.size()) &&
           "tuple list doesn't match inner loop nodes!");
    printRanges();
  } else {
    errs() << "Found no Ranges in this LoopNode \n";
  }
}
void FindRanges::printRanges() {
  errs() << "Found these optimal ranges: \n";
  auto Iter = InnerLoopNodes.begin();
  for (auto T : TupleList) {
    while (std::distance(InnerLoopNodes.begin(), Iter) < T.begin) {
      ++Iter;
    }
    errs() << T.begin << ": ";
    (**Iter).LoopPtr->print(errs());
    errs() << " \n";
    while (std::distance(InnerLoopNodes.begin(), Iter) < T.end) {
      ++Iter;
    }
    errs() << T.end << ": ";
    (**Iter).LoopPtr->print(errs());
    errs() << " \n";
    errs() << "Average: " << T.avg << " \n";
    errs() << " \n";
  }
}
void FindRanges::getRangesAsList(std::list<SPtrLN> *Worklist,
                                 std::list<Loop *> *Ranges) {
  auto Iter = InnerLoopNodes.begin();
  if (!TupleList.empty()) {
    for (auto T : TupleList) {
      while (std::distance(InnerLoopNodes.begin(), Iter) < T.begin) {
        if (!(*Iter)->InnerLoops.empty()) {
          Worklist->push_back((*Iter));
        }
        ++Iter;
      }
      Ranges->push_back((*Iter)->LoopPtr);
      while (std::distance(InnerLoopNodes.begin(), Iter) < T.end) {
        ++Iter;
      }
      Ranges->push_back((*Iter)->LoopPtr);
    }
    // Iterator must point to actual end, not last element
    ++Iter;
  }
  while (Iter != InnerLoopNodes.end()) {
    if (!(*Iter)->InnerLoops.empty()) {
      Worklist->push_back((*Iter));
    }
    ++Iter;
  }
}
} // namespace offload