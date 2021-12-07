#ifndef LLVM_PASS_FIND_RANGES
#define LLVM_PASS_FIND_RANGES

#include <list>
#include <vector>

#include "LoopNode.h"

using namespace llvm;

namespace offload {

using SPtrLN = std::shared_ptr<LoopNode>;

class FindRanges {
private:
  const std::list<SPtrLN> &InnerLoopNodes;
  float Threshold;

  struct Tuple {
    int begin;
    int end;
    int delta;
    float avg;
  };
  std::vector<Tuple> TupleList;

  static bool compareByDelta(const Tuple &A, const Tuple &B);
  static bool compareByBegin(const Tuple &A, const Tuple &B);
  // sums up the OffloadRatios of all LoopNodes pointed to in the interval
  // [Iter1, Iter2] Iter1 and Iter2 can point to the same element
  float sumUp(std::list<SPtrLN>::const_iterator Iter1,
              std::list<SPtrLN>::const_iterator Iter2);
  void reduceTuples(std::vector<Tuple> &TupleList);

public:
  FindRanges(const std::list<SPtrLN> &InnerLoopNodes, float Threshold = 0.1);
  void printRanges();
  void getRangesAsList(std::list<SPtrLN> *Worklist, std::list<Loop *> *Ranges);
};
} // namespace offload
#endif