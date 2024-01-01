#include "llvm/Transforms/IPO/GlobalVTC.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/EHUtils.h"
#include "llvm/IR/CFG.h"

using namespace llvm;

#define DEBUG_TYPE "globalvtc"

STATISTIC(NumVTableSize, "Size of virtual tables in LTO unit");
STATISTIC(NumEHInstr, "Number of exception handling instructions");
STATISTIC(NumInstr, "Number of instructions in LTO Unit");
STATISTIC(NumDynmaicCast, "Number of dynamic cast");

PreservedAnalyses GlobalVTCPass::run(Module &M, ModuleAnalysisManager &AM) {
  SmallVector<MDNode *, 2> Types;
  VTableSize = 0;
  for (auto &GV : M.globals()) {
    Types.clear();
    GV.getMetadata(LLVMContext::MD_type, Types);
    if (!Types.empty())
      VTableSize += (M.getDataLayout().getTypeSizeInBits(GV.getValueType()) / 8);
  }

  NumVTableSize = VTableSize;

  for (auto &F : M.functions()) {
    if (F.empty())
      continue;
    DenseSet<BasicBlock *> EHBlocks;
    computeEHOnlyBlocks(F, EHBlocks);
    for (auto &B : F) {
      for (auto &I : B) {
        if (!I.isDebugOrPseudoInst())
          NumInstr++;
        if (CallInst *C = dyn_cast<CallInst>(&I)) {
          Function *F = C->getCalledFunction();
          if (F && F->hasName() && F->getName() == "__dynamic_cast")
            NumDynmaicCast++;
        }
      }
    }

    for (auto *B : EHBlocks)
      for (auto &I : *B)
        if (!I.isDebugOrPseudoInst())
          NumEHInstr++;

  }
  return PreservedAnalyses::none();
}
