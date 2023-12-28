#include "llvm/Transforms/IPO/GlobalVTC.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

#define DEBUG_TYPE "globalvtc"

STATISTIC(NumVTableSize, "Size of virtual tables in LTO unit");

PreservedAnalyses GlobalVTCPass::run(Module &M, ModuleAnalysisManager &AM) {
  SmallVector<MDNode *, 2> Types;
  for (auto &GV : M.globals()) {
    Types.clear();
    GV.getMetadata(LLVMContext::MD_type, Types);
    if (!Types.empty())
      VTableSize += M.getDataLayout().getTypeSizeInBits(GV.getValueType());
  }

  NumVTableSize = VTableSize;
  return PreservedAnalyses::none();
}
