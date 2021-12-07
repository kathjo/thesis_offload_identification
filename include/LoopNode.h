#ifndef LLVM_PASS_LOOP_NODE
#define LLVM_PASS_LOOP_NODE

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "AccessNode.h"

using namespace llvm;

//-----------------------------------------------------------------------------
// FindOffloadLoops implementation
//-----------------------------------------------------------------------------
namespace offload {
struct LoopNode {
  std::string Name;
  std::shared_ptr<LoopNode> ParentLoop = nullptr;
  Loop *LoopPtr = nullptr;
  std::list<std::shared_ptr<LoopNode>>
      InnerLoops; // this can be separate from accesses
  std::vector<std::shared_ptr<AccessNode>>
      AttachedAccesses; // actually just LoadInst for now
  float Regular = 0;
  float Irregular = 0;
  float OffloadRatio = 0;
};

} // namespace offload
#endif