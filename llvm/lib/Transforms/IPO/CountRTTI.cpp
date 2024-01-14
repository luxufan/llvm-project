#include "llvm/Transforms/IPO/CountRTTI.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/FileSystem.h"

#define DEBUG_TYPE "rtti-count"

STATISTIC(NumZTI, "Number of ZTI global variable");
STATISTIC(NumZTIBytes, "Number of ZTI bytes");
STATISTIC(NumZTS, "Number of ZTS strings");
STATISTIC(NumZTSBytes, "Number of ZTS bytes");
STATISTIC(NumPreZTI, "Number of ZTI global variable");
STATISTIC(NumPreZTIBytes, "Number of ZTI bytes");
STATISTIC(NumPreZTS, "Number of ZTS strings");
STATISTIC(NumPreZTSBytes, "Number of ZTS bytes");
STATISTIC(NumInternalizedZTV, "");
STATISTIC(NumZTV, "");
STATISTIC(NumUsedByEH, "");
STATISTIC(NumUsedByCast, "");
STATISTIC(NumFinal, "");

namespace llvm {
void CountRTTIPass::recordRttis(StringRef Name, bool UsedByCast) {
  std::string TypeId = CXXABIManager<Itanium>::GetTypeIdFromTypeInfo(Name);
  auto AddRttis = [&](StringRef Name, bool UsedByCast) {
    if (UsedByCast)
      RttiUsedByCast.insert(Name);
    else
      RttiUsedByEH.insert(Name);
  };

  AddRttis(Name, UsedByCast);

  if (!CHAInfo->hasCompatibleAddressPoints(TypeId))
    return;

  for (auto &AP : CHAInfo->getTypeIdCompatibleVTableInfo(TypeId)) {
    GlobalVariable *TypeInfoGV = M->getNamedGlobal(CXXABIManager<Itanium>::GetTypeInfoFromVTable(AP.VTableName));
    if (!TypeInfoGV)
      continue;

    AddRttis(TypeInfoGV->getName(), UsedByCast);
  }
}


PreservedAnalyses CountRTTIPass::run(Module &M, ModuleAnalysisManager &MAM) {
  CHAInfo = &MAM.getResult<ClassHierarchyAnalysis>(M);
  CHAInfo->init(nullptr);
  this->M = &M;

  using Itanium = CXXABIManager<Itanium>;
  const DataLayout &DL = M.getDataLayout();
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasInitializer() || !GV.hasName())
      continue;

    Constant *Initializer = GV.getInitializer();
    Type *Ty = Initializer->getType();
    uint64_t Size = DL.getTypeAllocSize(Ty);

    StringRef Name = GV.getName();

    if (Name.starts_with(Itanium::GetVTablePrefix())) {
      if (PreOpt) {
        NumZTV++;
        bool Local = GV.hasInternalLinkage() || GV.getVCallVisibility()
          != GlobalValue::VCallVisibilityPublic;

        if (!Local && ImportSummary)
          if (auto *GVS = dyn_cast<GlobalVarSummary>(ImportSummary->getGlobalValueSummary(GV, false)))
            Local = GVS->getVCallVisibility() != GlobalValue::VCallVisibilityPublic;

        if (Local)
          NumInternalizedZTV++;

        if (GV.hasMetadata(LLVMContext::MD_final))
          NumFinal++;
      }
    }

    if (Name.starts_with(Itanium::GetTypeInfoPrefix())) {
      if (PreOpt) {
        for (auto *U : GV.users()) {
          if (auto *II = dyn_cast<IntrinsicInst>(U)) {
            if (II->getIntrinsicID() == Intrinsic::eh_typeid_for)
              recordRttis(Name, false);
          }

          if (auto *CB = dyn_cast<CallBase>(U))
            if (CB->getCalledFunction() && CB->getCalledFunction()->getName() == "__dynamic_cast")
              recordRttis(Name);
        }
      }
      if (!PreOpt) {
        NumZTI++;
        NumZTIBytes += Size;
      } else {
        NumPreZTI++;
        NumPreZTIBytes += Size;
      }
    } else if (Name.starts_with(Itanium::GetTypeIdPrefix())) {
      if (!PreOpt) {
        NumZTS++;
        NumZTSBytes += Size;
      } else {
        NumPreZTS++;
        NumPreZTSBytes += Size;
      }
    }
  }

  if (PreOpt) {
    NumUsedByEH += RttiUsedByEH.size();
    NumUsedByCast += RttiUsedByCast.size();
  }

  return PreservedAnalyses::all();
}
}
