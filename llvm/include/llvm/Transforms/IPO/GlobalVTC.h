#ifndef LLVM_TRANSFORMS_IPO_GLOBALVTC_H
#define LLVM_TRANSFORMS_IPO_GLOBALVTC_H

#include "llvm/IR/PassManager.h"

namespace llvm {
struct GlobalVTCPass : PassInfoMixin<GlobalVTCPass> {
  size_t VTableSize;

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
}
#endif LLVM_TRANSFORMS_IPO_GLOBALVTC_H
