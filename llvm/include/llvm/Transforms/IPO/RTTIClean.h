#ifndef LLVM_TRANSFORMS_IPO_RTTICLEAN_H
#define LLVM_TRANSFORMS_IPO_RTTICLEAN_H

#include "llvm/Analysis/ClassHierarchyAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class RTTICleanPass : public PassInfoMixin<RTTICleanPass> {
  using ABIManager = CXXABIManager<Itanium>;
  Module *M;
  bool UseCommandLine = false;
  ClassHierarchyInfo *CHAInfo;
  FunctionAnalysisManager *FAM;
  const DataLayout *DL;
  const ModuleSummaryIndex *ImportSummary = nullptr;
  ModuleSummaryIndex *ExportSummary = nullptr;
  DenseSet<Constant *> Initializers;
  // Map from virtual table global variable to all of its address
  // points.
  DenseMap<GlobalVariable *, SmallVector<AddressPoint, 2>> VTable2APs;

  void checkSummary(std::set<AddressPoint> &DeadSet);
  void checkTypeTest(std::set<AddressPoint> &DeadSet);
  void checkDynamicCast(std::set<AddressPoint> &DeadSet);
  void adjustOffsets();
  void analysisSameVTables();

public:
  RTTICleanPass() : UseCommandLine(true), ImportSummary(nullptr), ExportSummary(nullptr) {}

  RTTICleanPass(ModuleSummaryIndex *Export, const ModuleSummaryIndex *Import) : ImportSummary(Import), ExportSummary(Export) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);

};

}

#endif // LLVM_TRANSFORMS_IPO_RTTICLEAN_H
