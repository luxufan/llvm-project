#ifndef ANALYSISRTTI_H_
#define ANALYSISRTTI_H_

#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Analysis/ClassHierarchyAnalysis.h"

namespace llvm {

class CountRTTIPass : public PassInfoMixin<CountRTTIPass> {
  bool PreOpt;
  const ModuleSummaryIndex *ImportSummary;
  ClassHierarchyInfo *CHAInfo;
  Module *M;

  DenseSet<StringRef> RttiUsedByEH;
  DenseSet<StringRef> RttiUsedByCast;

  void recordRttis(StringRef Name, bool UsedByCast = true);

public:
  CountRTTIPass() {}
  CountRTTIPass(const ModuleSummaryIndex *ImportSummary, bool PreOpt = false) :
  PreOpt(PreOpt), ImportSummary(ImportSummary) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);

};

}


#endif // ANALYSISRTTI_H_
